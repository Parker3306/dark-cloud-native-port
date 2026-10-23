"""Self test of the second group of portkit tools (status, find, logs, assets, decomp, multi, replay, live, bisect, emu). Called by selftest.py; needs only Python and git."""
import os
import socket
import struct
import subprocess
import sys
import tempfile
import threading

HERE = os.path.dirname(os.path.abspath(__file__))
PK = os.path.join(HERE, 'portkit.py')
PY = sys.executable.replace('\\', '/')
GIT_BASH = 'C:/Program Files/Git/bin/bash.exe'
BASH = '"%s"' % GIT_BASH if os.name == 'nt' and os.path.exists(GIT_BASH) else '"bash"'


def sh(*args, cwd, timeout=120):
    return subprocess.run([sys.executable, PK] + list(args), cwd=cwd, capture_output=True, text=True, timeout=timeout)


def w(d, name, text):
    p = os.path.join(d, name)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    open(p, 'w', encoding='utf-8').write(text)


def fake_pine(port, stop):
    """Answers STATUS (15) with 0 (running) and READ32 with a counter, enough for `emu status`."""
    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(('127.0.0.1', port))
    srv.listen(4)
    srv.settimeout(0.2)
    while not stop.is_set():
        try:
            c, _ = srv.accept()
        except OSError:
            continue
        c.settimeout(1)
        try:
            while not stop.is_set():
                hdr = c.recv(4)
                if len(hdr) < 4:
                    break
                n = struct.unpack('<I', hdr)[0]
                body = b''
                while len(body) < n - 4:
                    body += c.recv(n - 4 - len(body))
                out = b''
                p = 0
                while p < len(body):
                    op = body[p]
                    p += 1
                    if op == 15:
                        out += b'\x00' + struct.pack('<I', 0)
                    elif op == 11:
                        out += bytes([0]) + struct.pack('<I', 5) + b'test' + bytes([0])
                    elif op == 2:
                        p += 4
                        out += b'\x00' + struct.pack('<I', 1)
                    else:
                        out += b'\x00'
                c.sendall(struct.pack('<I', len(out) + 4) + out)
        except OSError:
            pass
        c.close()
    srv.close()


def run(expect):
    with tempfile.TemporaryDirectory() as d:
        port = 28999
        w(d, 'notes/topic.md', '# Topic\n\nThe frobnicator spins up when the lock-on target is a boss.\n\nOther text.\n')
        w(d, 'src/a.c', 'int frobnicate(void) { return 1; }\n')
        w(d, 'TODO.md', '# Open\n\n## Now\n- [ ] first thing\n- [x] done thing\n- [ ] second thing\n')
        w(d, 'pickup.md', 'start here\nline two\n')
        w(d, 'dump.c', '// ==== alpha @ 00100000\nvoid alpha(void){ beta(); beta(); puts("hello"); }\n// ==== beta @ 00100100\nvoid beta(void){ gamma(); }\n// ==== gamma @ 00100200\nint gamma(void){ return 7; }\n')
        w(d, 'live_mod.py', 'def trial(ctx, i, rng):\n    v = rng.randint(0, 9)\n    return {"live": {"v": v, "f": 1.0}, "port": {"v": v + (1 if i == 99 else 0), "f": 1.004}, "params": {"i": i}}\n')
        w(d, 'prog.py', 'import sys,os,time\nrole=sys.argv[1]\nprint("START", role)\nsys.stdout.flush()\npad=os.environ.get("PK_RECORD_PAD")\nif pad:\n    open(pad,"w").write("128 128 128 128 0 0\\n"*5)\n    print("RECORDED")\nif len(sys.argv)>2:\n    print("replaying", sys.argv[2])\nprint("DONE", role)\n')
        w(d, 'img.py', 'import sys\nsys.path.insert(0, %r)\nimport pk_png\npk_png.write_png(sys.argv[1], 4, 4, [(10, 20, 30)] * 16) if hasattr(pk_png, "write_png") else open(sys.argv[1], "wb").write(b"x")\n' % HERE.replace('\\', '/'))
        w(d, 'portkit.toml', '''
[project]
name = "selftest2"
shell = [%(bash)s, "-c"]
[status]
todo = "TODO.md"
pickup = "pickup.md"
[find]
notes = ["notes"]
code = ["src"]
[logfilter]
drop = ["^NOISE"]
[decomp]
source = "dump.c"
[emu]
exe = "no/such/emu.exe"
pine_port = %(port)d
[replays]
dir = ".portkit/replays"
[[replay]]
name = "case1"
record_cmd = "%(py)s prog.py rec"
cmd = "%(py)s prog.py play {input}"
pass_regex = ["DONE play"]
[[multi]]
name = "pair"
timeout = 30
start_gap = 0.2
done_regex = "DONE"
expect = [ {role = "host", regex = "START host"}, {role = "guest", regex = "START guest"} ]
forbid = ["Traceback"]
[[multi.instance]]
role = "host"
cmd = "%(py)s prog.py host"
[[multi.instance]]
role = "guest"
cmd = "%(py)s prog.py guest"
[[live]]
name = "cmp"
module = "live_mod.py"
connect = "none"
trials = 5
seed = 1
[[check]]
name = "good_if_flag_absent"
cmd = "%(py)s -c \\"import os,sys; sys.exit(1 if os.path.exists('bug.flag') else 0)\\""
''' % dict(bash=BASH, py=PY, port=port))
        r = sh('status', cwd=d)
        expect('2 open' in r.stdout and 'start here' in r.stdout, 'status shows open items and the pickup note')
        r = sh('find', 'frobnicator', 'boss', cwd=d)
        expect('topic.md' in r.stdout, 'find locates a note')
        r = sh('find', 'frobnicate', '--code', cwd=d)
        expect('a.c' in r.stdout, 'find --code locates source')
        w(d, 'big.log', 'NOISE a\nline 1 value 5\nline 1 value 6\nline 1 value 7\nERROR: boom 12\nERROR: boom 13\nend\n')
        r = sh('logs', os.path.join(d, 'big.log'), cwd=d)
        expect('NOISE' not in r.stdout and 'x3' in r.stdout.replace(' ', '').replace('[', '').replace(']', '') or 'x3' in r.stdout or '3x' in r.stdout, 'logs drops noise and counts repeats')
        r = sh('logs', os.path.join(d, 'big.log'), '--errors', cwd=d)
        expect('boom' in r.stdout and ('2' in r.stdout), 'logs --errors lists the distinct errors with counts')
        r = sh('assets', 'inventory', d, cwd=d)
        expect('files' in r.stdout and '.md' in r.stdout, 'assets inventory counts files')
        r = sh('decomp', 'index', cwd=d)
        expect('3 functions' in r.stdout, 'decomp index finds the functions')
        r = sh('decomp', 'callers', 'gamma', cwd=d)
        expect('beta' in r.stdout, 'decomp callers')
        r = sh('decomp', 'dossier', 'alpha', cwd=d)
        expect('beta' in r.stdout and 'hello' in r.stdout, 'decomp dossier shows callees and strings')
        r = sh('multi', 'run', 'pair', cwd=d)
        expect(r.returncode == 0, 'multi run passes when both instances start (%s)' % (r.stdout + r.stderr)[-200:].replace('\n', ' | '))
        r = sh('replay', 'record', 'case1', cwd=d)
        expect(os.path.exists(os.path.join(d, '.portkit', 'replays', 'case1.pad')), 'replay record stores the input file')
        r = sh('replay', 'run', cwd=d)
        expect(r.returncode == 0 and 'case1' in r.stdout, 'replay run substitutes {input} and judges the log')
        r = sh('live', 'run', 'cmp', cwd=d)
        expect(r.returncode == 0, 'live run: equal data within tolerance passes')
        r = sh('live', 'run', 'cmp', '--trials', '100', cwd=d)
        expect(r.returncode != 0, 'live run: a differing trial fails')
        stop = threading.Event()
        t = threading.Thread(target=fake_pine, args=(port, stop), daemon=True)
        t.start()
        r = sh('emu', 'status', cwd=d)
        expect('PINE' in r.stdout and 'down' not in r.stdout.split('PINE')[1][:8], 'emu status talks to PINE (%s)' % r.stdout.strip().replace('\n', ' | ')[:120])
        stop.set()
        t.join(2)
        # bisect: three commits, the second one introduces the flag
        env = dict(os.environ, GIT_AUTHOR_NAME='t', GIT_AUTHOR_EMAIL='t@t', GIT_COMMITTER_NAME='t', GIT_COMMITTER_EMAIL='t@t')
        g = lambda *a: subprocess.run(['git'] + list(a), cwd=d, env=env, capture_output=True, text=True)
        g('init', '-q')
        g('add', 'portkit.toml', 'prog.py')
        g('commit', '-qm', 'c1')
        good = g('rev-parse', 'HEAD').stdout.strip()
        w(d, 'bug.flag', 'x')
        g('add', 'bug.flag')
        g('commit', '-qm', 'c2 bug')
        bad = g('rev-parse', 'HEAD').stdout.strip()
        w(d, 'other.txt', 'x')
        g('add', 'other.txt')
        g('commit', '-qm', 'c3')
        r = sh('bisect', 'good_if_flag_absent', '--good', good, cwd=d, timeout=180)
        expect(bad[:7] in r.stdout, 'bisect names the first bad commit (%s)' % (r.stdout + r.stderr)[-200:].replace('\n', ' | '))
