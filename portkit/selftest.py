"""Self test of portkit: python portkit/selftest.py   (needs nothing but Python; makes a throw-away project in a temp directory)."""
import os
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PK = os.path.join(HERE, 'portkit.py')
sys.path.insert(0, HERE)
import pk_png  # noqa: E402

PY = sys.executable.replace('\\', '/')
GIT_BASH = 'C:/Program Files/Git/bin/bash.exe'   # (plain `bash` can be the WSL stub on Windows)
BASH = '"%s"' % GIT_BASH if os.name == 'nt' and os.path.exists(GIT_BASH) else '"bash"'
MANIFEST = '''
[project]
name = "selftest"
shell = [%(bash)s, "-c"]
[defaults]
timeout = 20
[[check]]
name = "passes"
cmd = "%(py)s -c \\"print('ok: one'); print('ok: two')\\""
count_regex = "^ok:"
count_min = 2
[[check]]
name = "fails_by_regex"
cmd = "%(py)s -c \\"print('FAIL: broken thing')\\""
fail_regex = ["^FAIL"]
[[check]]
name = "fails_by_exit"
cmd = "%(py)s -c \\"import sys; sys.exit(3)\\""
[[check]]
name = "times_out"
cmd = "%(py)s -c \\"import time; time.sleep(30)\\""
timeout = 2
[[check]]
name = "retries_then_passes"
cmd = "%(py)s -c \\"import os,sys; p='flaky.flag'; ok=os.path.exists(p); open(p,'w').close(); print('Traceback' if not ok else 'ok: fine'); sys.exit(0 if ok else 1)\\""
retries = 1
retry_regex = "Traceback"
pass_regex = ["ok: fine"]
[[check]]
name = "skipped"
cmd = "echo never"
requires = ["no/such/file"]
[shots]
max_changed_pct = 0.1
[[shot]]
name = "pic"
cmd = "%(py)s make_png.py {out} 0"
'''


def sh(*args, cwd):
    return subprocess.run([sys.executable, PK] + list(args), cwd=cwd, capture_output=True, text=True)


def main():
    bad = []

    def expect(cond, what):
        print(('  ok  ' if cond else ' FAIL ') + what)
        if not cond:
            bad.append(what)
    with tempfile.TemporaryDirectory() as d:
        open(os.path.join(d, 'portkit.toml'), 'w').write(MANIFEST % dict(py=PY, bash=BASH))
        open(os.path.join(d, 'make_png.py'), 'w').write(
            "import sys\nsys.path.insert(0, %r)\nimport pk_png\nw, h = 200, 120\nrgb = bytearray(w * h * 3)\nfor i in range(0, len(rgb), 3):\n    rgb[i + 1] = 90\n"
            "if sys.argv[2] == '1':\n    for y in range(30, 90):\n        for x in range(80, 110):\n            rgb[(y * w + x) * 3] = 255\npk_png.write_png(sys.argv[1], pk_png.Img(w, h, bytes(rgb)))\n" % HERE)
        r = sh('check', cwd=d)
        out = r.stdout
        expect(r.returncode == 1, 'check run returns 1 when something fails')
        expect(' PASS  passes' in out and '2 ok' in out, 'passing check with a count summary')
        expect(' FAIL  fails_by_regex' in out and 'FAIL: broken thing' in out, 'regex failure shows the excerpt')
        expect(' FAIL  fails_by_exit' in out and 'exit code 3' in out, 'exit code failure')
        expect(' TIMEOUT times_out' in out, 'timeout is killed and reported')
        expect(' PASS  retries_then_passes' in out and '2 attempts' in out, 'retry on a matching log')
        expect(' SKIP  skipped' in out, 'missing requirement skips')
        r = sh('check', '--failed', cwd=d)
        expect('retries_then_passes' not in r.stdout and 'fails_by_exit' in r.stdout, '--failed reruns only the failures')
        r = sh('shots', cwd=d)
        expect(' NEW ' in r.stdout, 'first capture has no baseline')
        sh('shots', 'accept', '--new', cwd=d)
        r = sh('shots', cwd=d)
        expect(r.returncode == 0 and '1 same' in r.stdout, 'identical capture is SAME')
        open(os.path.join(d, 'portkit.toml'), 'a').write('\n')
        txt = open(os.path.join(d, 'portkit.toml')).read().replace('make_png.py {out} 0', 'make_png.py {out} 1')
        open(os.path.join(d, 'portkit.toml'), 'w').write(txt)
        r = sh('shots', cwd=d)
        expect(r.returncode == 1 and 'CHANGED' in r.stdout and os.path.exists(os.path.join(d, '.portkit', 'shots', 'report', 'pic.png')), 'a changed picture is reported with a diff image')
        a = pk_png.read_png(os.path.join(d, '.portkit', 'shots', 'baseline', 'pic.png'))
        b = pk_png.read_png(os.path.join(d, '.portkit', 'shots', 'current', 'pic.png'))
        c = pk_png.compare(a, b, 8, [(80, 30, 30, 60)])
        expect(c['changed'] == 0, 'a mask hides the changed region')
    import selftest_tools
    selftest_tools.run(expect)
    print('selftest: %s' % ('all passed' if not bad else '%d failed' % len(bad)))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
