"""Tiny multiplayer transport: one host, any number of guests, JSON lines over TCP.

The host relays every guest's messages to the other guests and stamps each message with the sender's id (guests can't
spoof ids). The game CRC and protocol version are checked in the handshake, so players with a different game build are
refused. Messages are small dicts, e.g. {"t": "state", "pos": [...], ...}; this layer doesn't care about their content.

    net = Net.host(5000, "Ann", crc)      or      net = Net.join("203.0.113.5:5000", "Bob", crc)
    net.send({...})                       # to everyone else
    for msg in net.poll(): ...            # includes {"t": "join"/"leave", "id": n, "name": ...}
"""
import json
import queue
import socket
import threading

PROTOCOL = 1


class Net:
    def __init__(self, role, name, crc):
        self.role, self.name, self.crc = role, name, crc
        self.my_id = 0 if role == "host" else None
        self.inbox = queue.Queue()
        self.peers = {}                  # id -> socket (host: guests; guest: {0: host})
        self.names = {0: name} if role == "host" else {}
        self.lock = threading.Lock()
        self._next_id = 1
        self._server = None
        self.closed = False

    # --- construction ---------------------------------------------------------------------
    @classmethod
    def host(cls, port, name, crc):
        self = cls("host", name, crc)
        self._server = socket.create_server(("0.0.0.0", port), reuse_port=False)
        threading.Thread(target=self._accept_loop, daemon=True).start()
        return self

    @classmethod
    def join(cls, address, name, crc, timeout=10):
        host, _, port = address.rpartition(":")
        self = cls("guest", name, crc)
        sock = socket.create_connection((host, int(port)), timeout=timeout)
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._write(sock, {"t": "hello", "v": PROTOCOL, "crc": crc, "name": name})
        reader = sock.makefile("r", encoding="utf-8", newline="\n")
        welcome = json.loads(reader.readline() or "{}")
        if welcome.get("t") != "welcome":
            raise ConnectionError(f"host refused the connection: {welcome.get('why', 'no reply')}")
        sock.settimeout(None)
        self.my_id = welcome["id"]
        self.names = {int(k): v for k, v in welcome["players"].items()}
        self.peers[0] = sock
        threading.Thread(target=self._read_loop, args=(0, reader), daemon=True).start()
        return self

    # --- wire helpers ---------------------------------------------------------------------
    @staticmethod
    def _write(sock, msg):
        sock.sendall((json.dumps(msg, separators=(",", ":")) + "\n").encode())

    def _accept_loop(self):
        while not self.closed:
            try:
                sock, addr = self._server.accept()
            except OSError:
                return
            threading.Thread(target=self._handshake, args=(sock,), daemon=True).start()

    def _handshake(self, sock):
        sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        reader = sock.makefile("r", encoding="utf-8", newline="\n")
        try:
            sock.settimeout(10)
            hello = json.loads(reader.readline() or "{}")
            if hello.get("t") != "hello" or hello.get("v") != PROTOCOL:
                self._write(sock, {"t": "refused", "why": "protocol version mismatch"})
                return sock.close()
            if hello.get("crc") != self.crc:
                self._write(sock, {"t": "refused", "why": f"game version differs (host {self.crc}, you {hello.get('crc')})"})
                return sock.close()
            sock.settimeout(None)
            with self.lock:
                pid = self._next_id
                self._next_id += 1
                self.peers[pid] = sock
                self.names[pid] = str(hello.get("name", f"player{pid}"))[:24]
                players = dict(self.names)
            self._write(sock, {"t": "welcome", "id": pid, "players": players})
            join = {"t": "join", "id": pid, "name": self.names[pid]}
            self.inbox.put(join)
            self._relay(join, exclude=pid)
            self._read_loop(pid, reader)
        except (OSError, ValueError):
            pass

    def _read_loop(self, pid, reader):
        try:
            for line in reader:
                try:
                    msg = json.loads(line)
                except ValueError:
                    continue
                if not isinstance(msg, dict):
                    continue
                if self.role == "host":
                    msg["id"] = pid                       # authoritative sender id
                    self.inbox.put(msg)
                    self._relay(msg, exclude=pid)
                else:
                    self.inbox.put(msg)
        except (OSError, ValueError):
            pass
        finally:
            self._drop(pid)

    def _relay(self, msg, exclude=None):
        for pid, sock in list(self.peers.items()):
            if pid != exclude:
                try:
                    self._write(sock, msg)
                except OSError:
                    self._drop(pid)

    def _drop(self, pid):
        with self.lock:
            sock = self.peers.pop(pid, None)
            name = self.names.pop(pid, None)
        if sock is None:
            return
        try:
            sock.close()
        except OSError:
            pass
        leave = {"t": "leave", "id": pid, "name": name}
        self.inbox.put(leave)
        if self.role == "host":
            self._relay(leave)

    # --- public ---------------------------------------------------------------------------
    def send(self, msg):
        msg = dict(msg, id=self.my_id)
        if self.role == "host":
            self._relay(msg)
        elif 0 in self.peers:
            try:
                self._write(self.peers[0], msg)
            except OSError:
                self._drop(0)

    def poll(self):
        out = []
        while True:
            try:
                out.append(self.inbox.get_nowait())
            except queue.Empty:
                return out

    def close(self):
        self.closed = True
        for pid in list(self.peers):
            self._drop(pid)
        if self._server:
            self._server.close()
