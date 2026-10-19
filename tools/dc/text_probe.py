"""Explores the message bank format: decodes data/meswin/_systeme.bin and compares with the developer's text source data/meswin/ReadBuff.txt to learn the code -> character table.
usage: text_probe.py"""
import os
import re
import struct
import sys
from collections import Counter, defaultdict

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..')


def load_bank(path):
    b = open(path, 'rb').read()
    sh = struct.unpack('<%dh' % (len(b) // 2), b[:len(b) // 2 * 2])
    n = sh[0]
    entries = []
    for i in range(n // 2):
        entries.append((sh[2 + 2 * i], sh[3 + 2 * i]))
    base = n + 1
    texts = {}
    for ident, off in entries:
        out = []
        p = base + off
        while p < len(sh) and sh[p] != -0xff:
            out.append(sh[p])
            p += 1
        texts[ident] = out
    return sh, entries, texts


def load_source(path):
    raw = open(path, 'rb').read().decode('latin1')
    res = {}
    offset = 0
    cur = None
    buf = []
    for line in raw.replace('\r', '').split('\n'):
        m = re.match(r'^(?:file_)?id_offset\s*=\s*(\d+)', line)
        if m:
            if cur is not None:
                res[cur] = buf
            cur = None
            offset = int(m.group(1))
            continue
        m = re.match(r'^@(\d+)\s*$', line)
        if m:
            if cur is not None:
                res[cur] = buf
            cur = offset + int(m.group(1))
            buf = []
            continue
        if line.startswith('//'):
            continue
        if cur is not None:
            buf.append(line)
    if cur is not None:
        res[cur] = buf
    return res


def main():
    sh, entries, texts = load_bank(os.path.join(ROOT, 'data', 'meswin', '_systeme.bin'))
    print('table shorts', sh[0], 'second word', hex(sh[1] & 0xffff), 'entries', len(entries), 'first', entries[:8])
    src = load_source(os.path.join(ROOT, 'data', 'meswin', 'ReadBuff.txt'))
    print('source messages', len(src))
    cnt = defaultdict(Counter)
    matched = 0
    for ident, codes in texts.items():
        s = src.get(ident)
        if s is None:
            continue
        line = '\n'.join(s)
        flat = [c for c in codes if c != -0x100]
        chars = [ch for ch in line if ch != '\n']
        if len(flat) != len(chars):
            continue
        matched += 1
        for c, ch in zip(flat, chars):
            cnt[c & 0xffff][ch] += 1
    print('messages with equal length', matched)
    table = {c: v.most_common(1)[0][0] for c, v in cnt.items()}
    conflicts = {c: v.most_common(3) for c, v in cnt.items() if len(v) > 1}
    print('codes mapped', len(table), 'conflicts', len(conflicts))
    for c in sorted(table):
        print('%04x -> %r' % (c, table[c]), end='   ')
    print()
    for ident in sorted(texts)[:12]:
        print(ident, ''.join(table.get(c & 0xffff, '<%04x>' % (c & 0xffff)) if c != -0x100 else '\\n' for c in texts[ident]))


if __name__ == '__main__':
    main()
