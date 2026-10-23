"""portkit assets: get to know a game's data without opening files one by one.

  assets iso FILE [--find REGEX] [--extract DIR] [--max N]   list the files of an ISO 9660 disc image (size, path); extract all or the ones matching --find into DIR
  assets inventory DIR [--top N]                              what is in a folder: files and bytes per extension, per magic number (first 4 bytes), the largest files, unknown magics
  assets sheet PATH... --out FILE [--max N] [--cell PX] [--tim2]   a contact sheet (numbered cells, a text list printed next to it) of the images found: PNG / BMP / JPG files and,
                                                              with --tim2, every PS2 TIM2 texture found inside any file (needs numpy + Pillow)
"""
import os
import re
import struct
import sys
from collections import Counter

SECTOR = 2048
MAGICS = {b'TIM2': 'TIM2 texture', b'MDS\0': 'MDS model', b'RIFF': 'RIFF (wav/avi)', b'\x89PNG': 'PNG', b'BM': 'BMP', b'ELF\x7f': 'ELF', b'\x7fELF': 'ELF', b'IECS': 'IECS (Sony IOP)', b'SShd': 'SShd (PS2 audio)',
          b'VAGp': 'VAG audio', b'AFS\0': 'AFS archive', b'PK\x03\x04': 'zip', b'\x00\x00\x01\xba': 'MPEG PS', b'\x1f\x8b': 'gzip', b'OggS': 'Ogg', b'GIF8': 'GIF', b'IMG\0': 'IMG'}


def _magic_name(head):
    for k, v in MAGICS.items():
        if head.startswith(k):
            return v
    return None


# ------------------------------------------------------------------------------------------------------------------------------ ISO 9660
def iso_entries(path):
    """Yields (path, lba, size) of every file of an ISO 9660 image (Joliet and Rock Ridge names are ignored: the plain names, without ;1)."""
    f = open(path, 'rb')
    f.seek(16 * SECTOR)
    pvd = f.read(SECTOR)
    if pvd[1:6] != b'CD001':
        raise SystemExit('not an ISO 9660 image: %s' % path)
    root_lba, root_len = struct.unpack_from('<I', pvd, 158)[0], struct.unpack_from('<I', pvd, 166)[0]
    stack = [('', root_lba, root_len)]
    while stack:
        prefix, lba, length = stack.pop()
        f.seek(lba * SECTOR)
        data = f.read(length)
        p = 0
        while p < len(data):
            n = data[p]
            if n == 0:
                p = (p // SECTOR + 1) * SECTOR
                continue
            rec = data[p:p + n]
            p += n
            elba, esize = struct.unpack_from('<I', rec, 2)[0], struct.unpack_from('<I', rec, 10)[0]
            flags = rec[25]
            name = rec[33:33 + rec[32]]
            if name in (b'\x00', b'\x01'):
                continue
            name = name.decode('latin1').split(';')[0]
            full = prefix + '/' + name if prefix else name
            if flags & 2:
                stack.append((full, elba, esize))
            else:
                yield full, elba, esize
    f.close()


def cmd_iso(args):
    entries = sorted(iso_entries(args.file))
    pat = re.compile(args.find, re.I) if args.find else None
    chosen = [e for e in entries if not pat or pat.search(e[0])]
    total = sum(e[2] for e in entries)
    if not args.extract:
        for name, lba, size in chosen[:args.max]:
            print('%10d  lba %7d  %s' % (size, lba, name))
        print('(%d of %d files shown, %.1f MB in %d files)' % (min(len(chosen), args.max), len(chosen), total / 1e6, len(entries)))
        return 0
    f = open(args.file, 'rb')
    n = 0
    for name, lba, size in chosen:
        out = os.path.join(args.extract, *name.split('/'))
        os.makedirs(os.path.dirname(out), exist_ok=True)
        f.seek(lba * SECTOR)
        with open(out, 'wb') as o:
            left = size
            while left > 0:
                chunk = f.read(min(left, 1 << 20))
                if not chunk:
                    break
                o.write(chunk)
                left -= len(chunk)
        n += 1
    print('extracted %d file(s) to %s' % (n, args.extract))
    return 0


# ------------------------------------------------------------------------------------------------------------------------------ inventory
def cmd_inventory(args):
    by_ext, by_magic, unknown, sizes = Counter(), Counter(), Counter(), []
    ext_bytes = Counter()
    nfiles = 0
    for d, _, files in os.walk(args.dir):
        for fn in files:
            fp = os.path.join(d, fn)
            try:
                size = os.path.getsize(fp)
                with open(fp, 'rb') as f:
                    head = f.read(8)
            except OSError:
                continue
            nfiles += 1
            ext = os.path.splitext(fn)[1].lower() or '(none)'
            by_ext[ext] += 1
            ext_bytes[ext] += size
            sizes.append((size, os.path.relpath(fp, args.dir)))
            name = _magic_name(head)
            if name:
                by_magic[name] += 1
            elif len(head) >= 4:
                unknown[(ext, head[:4].hex() if not all(32 <= b < 127 for b in head[:4]) else head[:4].decode('ascii'))] += 1
    print('%d files, %.1f MB' % (nfiles, sum(s for s, _ in sizes) / 1e6))
    print('by extension: ' + ', '.join('%s x%d (%.1f MB)' % (e, c, ext_bytes[e] / 1e6) for e, c in by_ext.most_common(args.top)))
    print('known magics: ' + (', '.join('%s x%d' % (m, c) for m, c in by_magic.most_common()) or '-'))
    print('most common unknown (extension, first 4 bytes): ' + ', '.join('%s %s x%d' % (e, m, c) for (e, m), c in unknown.most_common(args.top)))
    print('largest: ' + ', '.join('%s (%.1f MB)' % (n, s / 1e6) for s, n in sorted(sizes, reverse=True)[:args.top]))
    return 0


# ------------------------------------------------------------------------------------------------------------------------------ contact sheet
def _images(paths, tim2, limit):
    from PIL import Image
    found = []
    for base in paths:
        files = [base] if os.path.isfile(base) else [os.path.join(d, f) for d, _, fs in os.walk(base) for f in sorted(fs)]
        for fp in files:
            ext = os.path.splitext(fp)[1].lower()
            try:
                if ext in ('.png', '.bmp', '.jpg', '.jpeg', '.gif'):
                    found.append((fp, Image.open(fp).convert('RGBA')))
                elif tim2 and os.path.getsize(fp) <= 64 * 1024 * 1024:
                    import pk_tim2
                    data = open(fp, 'rb').read()
                    for k, t in enumerate(pk_tim2.scan(data)):
                        found.append(('%s#%d' % (fp, k), t.decode()))
                        if len(found) >= limit:
                            return found
            except Exception:
                continue
            if len(found) >= limit:
                return found
    return found


def cmd_sheet(args):
    from PIL import Image, ImageDraw
    imgs = _images(args.paths, args.tim2, args.max)
    if not imgs:
        print('no images found')
        return 1
    cell = args.cell
    cols = max(1, min(len(imgs), args.cols))
    rows = (len(imgs) + cols - 1) // cols
    sheet = Image.new('RGB', (cols * cell, rows * cell), (40, 40, 48))
    draw = ImageDraw.Draw(sheet)
    for i, (name, im) in enumerate(imgs):
        w, h = im.size
        sc = min((cell - 4) / w, (cell - 4) / h, 4.0)
        small = im.resize((max(1, int(w * sc)), max(1, int(h * sc))), Image.NEAREST if sc >= 1 else Image.LANCZOS)
        x, y = (i % cols) * cell, (i // cols) * cell
        bg = Image.new('RGB', small.size, (90, 90, 100))
        bg.paste(small, mask=small.split()[3])
        sheet.paste(bg, (x + (cell - small.width) // 2, y + (cell - small.height) // 2))
        draw.text((x + 3, y + 2), str(i), fill=(255, 255, 0))
    sheet.save(args.out)
    for i, (name, im) in enumerate(imgs):
        print('#%d  %s  %dx%d' % (i, name.replace('\\', '/'), im.size[0], im.size[1]))
    print('sheet: %s (%d images, %dx%d px)' % (args.out, len(imgs), sheet.width, sheet.height))
    return 0


def run(manifest, args):
    if args.action == 'iso':
        return cmd_iso(args)
    if args.action == 'inventory':
        return cmd_inventory(args)
    return cmd_sheet(args)
