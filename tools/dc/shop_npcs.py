"""Find which event scripts open shops: scans gedit/**/event.stb with stbtext.py and prints, for every
_SET_TALK_SELECT_MES(kind 1..4, hdr) the _SET_TALK_MES(...) calls found within the next 8 lines (the talk list; for shop kinds entry 0 = shop number, see EdTalkMode).
usage: shop_npcs.py [glob-substring]"""
import glob, os, re, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__)); ROOT = os.path.join(HERE, '..', '..')
sub = sys.argv[1] if len(sys.argv) > 1 else ''
for f in sorted(glob.glob(os.path.join(ROOT, 'data', 'gedit', '**', 'event.stb'), recursive=True)):
    rel = os.path.relpath(f, os.path.join(ROOT, 'data')).replace(chr(92), '/')
    if sub not in rel:
        continue
    out = subprocess.run([sys.executable, '-I', os.path.join(HERE, 'stbtext.py'), rel], capture_output=True, text=True).stdout.split(chr(10))
    fn = ''
    for i, l in enumerate(out):
        if l.startswith('fn_'):
            fn = l.split()[0]
        m = re.search(r'_SET_TALK_SELECT_MES\(\s*([1-4])\s*,\s*([^)]*)\)', l)
        if m:
            near = [x.strip() for x in out[max(0, i - 8):i + 8] if '_SET_TALK_MES' in x and 'v' not in x.split('(')[1].split(',')[0]]
            print(rel, fn, 'kind', m.group(1), 'hdr', m.group(2), '|', ' ; '.join(near)[:200], flush=True)
