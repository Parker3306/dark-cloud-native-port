"""(portkit copy of coop/pine.py, kept standalone so that the kit can be dropped into any repository)
Client for PCSX2's PINE interface (localhost TCP): read/write the emulated PS2's memory.

Protocol (PCSX2 v2.8.2, pcsx2/PINE.cpp): request = u32 total length (incl. these 4 bytes) + commands;
reply = u32 total length + u8 result (0 ok, 0xFF fail) + payload. Several commands can share a packet.
All values are little-endian. PINE writes happen on PCSX2's PINE thread, not the EE thread, so they are not
synchronised with the game: don't write to code that is executing, and use sequence counters for shared data.
"""
import socket
import struct

READ8, READ16, READ32, READ64 = 0, 1, 2, 3
WRITE8, WRITE16, WRITE32, WRITE64 = 4, 5, 6, 7
VERSION, SAVE_STATE, LOAD_STATE, TITLE, ID, UUID, GAME_VERSION, STATUS = 8, 9, 10, 11, 12, 13, 14, 15

MAX_REQUEST = 650000
MAX_REPLY = 450000
_READ_FMT = {READ8: "<B", READ16: "<H", READ32: "<I", READ64: "<Q"}
_READ_SIZE = {READ8: 1, READ16: 2, READ32: 4, READ64: 8}
_WRITE_FMT = {WRITE8: "<B", WRITE16: "<H", WRITE32: "<I", WRITE64: "<Q"}


class PineError(Exception):
    pass


class Pine:
    def __init__(self, slot=28011, host="127.0.0.1", timeout=2.0):
        self.sock = socket.create_connection((host, slot), timeout=timeout)
        self.sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

    def close(self):
        self.sock.close()

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.close()

    def _recv(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise PineError("PCSX2 closed the PINE connection")
            buf += chunk
        return buf

    def _xfer(self, body):
        self.sock.sendall(struct.pack("<I", len(body) + 4) + body)
        total, = struct.unpack("<I", self._recv(4))
        rest = self._recv(total - 4)
        if rest[0] != 0:
            raise PineError("PINE command failed (is a game running?)")
        return rest[1:]

    # --- memory -----------------------------------------------------------------------------
    def read(self, addr, size=4):
        return self.read_many([(addr, size)])[0]

    def read_many(self, items):
        """items: [(addr, size)] with size in 1/2/4/8. One round trip. Returns a list of ints."""
        op = {1: READ8, 2: READ16, 4: READ32, 8: READ64}
        body = b"".join(struct.pack("<BI", op[s], a) for a, s in items)
        reply = self._xfer(body)
        out, pos = [], 0
        for _, s in items:
            out.append(int.from_bytes(reply[pos:pos + s], "little"))
            pos += s
        return out

    def read_bytes(self, addr, n):
        """Read n bytes (batched 8 at a time)."""
        data = b""
        per_packet = (MAX_REPLY - 64) // 8
        pos = 0
        while pos < n:
            words = min(per_packet, (n - pos + 7) // 8)
            vals = self.read_many([(addr + pos + 8 * i, 8) for i in range(words)])
            data += b"".join(v.to_bytes(8, "little") for v in vals)
            pos += 8 * words
        return data[:n]

    def read_float(self, addr):
        return struct.unpack("<f", struct.pack("<I", self.read(addr, 4)))[0]

    def write(self, addr, value, size=4):
        self.write_many([(addr, value, size)])

    def write_many(self, items):
        """items: [(addr, value, size)]. One round trip."""
        op = {1: WRITE8, 2: WRITE16, 4: WRITE32, 8: WRITE64}
        fmt = {1: "<B", 2: "<H", 4: "<I", 8: "<Q"}
        body = b"".join(struct.pack("<BI", op[s], a) + struct.pack(fmt[s], v) for a, v, s in items)
        if len(body) + 4 > MAX_REQUEST:
            raise PineError("batch too large")
        self._xfer(body)

    def write_float(self, addr, value):
        self.write(addr, struct.unpack("<I", struct.pack("<f", value))[0], 4)

    def write_bytes(self, addr, data):
        """Write bytes; the start address and length are handled in 4-byte words plus a byte tail."""
        items, pos = [], 0
        while pos + 4 <= len(data):
            items.append((addr + pos, int.from_bytes(data[pos:pos + 4], "little"), 4))
            pos += 4
        while pos < len(data):
            items.append((addr + pos, data[pos], 1))
            pos += 1
        for i in range(0, len(items), 20000):
            self.write_many(items[i:i + 20000])

    # --- emulator ---------------------------------------------------------------------------
    def _string(self, opcode):
        reply = self._xfer(bytes([opcode]))
        n, = struct.unpack("<I", reply[:4])
        return reply[4:4 + n].split(b"\0")[0].decode("latin1")

    def version(self):
        return self._string(VERSION)

    def title(self):
        return self._string(TITLE)

    def game_id(self):
        return self._string(ID)

    def crc(self):
        return self._string(UUID)

    def status(self):
        """0 running, 1 paused, 2 shut down."""
        return struct.unpack("<I", self._xfer(bytes([STATUS])))[0]

    def save_state(self, slot):
        self._xfer(bytes([SAVE_STATE, slot]))

    def load_state(self, slot):
        self._xfer(bytes([LOAD_STATE, slot]))
