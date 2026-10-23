"""PNG reading / writing and image comparison for portkit shots. numpy and Pillow are used when they are installed (fast); otherwise a small pure-Python PNG codec
(8-bit gray / RGB / palette / gray+alpha / RGBA, not interlaced: what Godot, PCSX2 and most tools write) does the work."""
import os
import struct
import zlib

try:
    import numpy as np
except Exception:   # pragma: no cover
    np = None
try:
    from PIL import Image
except Exception:   # pragma: no cover
    Image = None


if os.environ.get('PORTKIT_PURE'):   # (tests: force the pure-Python paths)
    np = None
    Image = None


class Img:
    """An RGB image: width, height and w*h*3 bytes."""
    def __init__(self, w, h, rgb):
        self.w, self.h, self.rgb = w, h, rgb


def read_png(path):
    if Image is not None:
        im = Image.open(path).convert('RGB')
        return Img(im.width, im.height, im.tobytes())
    return _read_png_pure(path)


def write_png(path, img):
    if Image is not None:
        Image.frombytes('RGB', (img.w, img.h), bytes(img.rgb)).save(path)
        return
    raw = bytearray()
    stride = img.w * 3
    for y in range(img.h):
        raw.append(0)
        raw += img.rgb[y * stride:(y + 1) * stride]

    def chunk(tag, data):
        c = struct.pack('>I', len(data)) + tag + data
        return c + struct.pack('>I', zlib.crc32(tag + data) & 0xFFFFFFFF)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', img.w, img.h, 8, 2, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(bytes(raw), 6)) + chunk(b'IEND', b''))


def _read_png_pure(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('not a PNG: %s' % path)
    pos, idat, plte, ihdr = 8, b'', None, None
    while pos < len(data):
        n, tag = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        pos += 12 + n
        if tag == b'IHDR':
            ihdr = struct.unpack('>IIBBBBB', body)
        elif tag == b'PLTE':
            plte = body
        elif tag == b'IDAT':
            idat += body
        elif tag == b'IEND':
            break
    w, h, depth, ctype, _, _, interlace = ihdr
    if depth != 8 or interlace != 0:
        raise ValueError('only 8-bit non-interlaced PNGs are supported without Pillow (%s)' % path)
    ch = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * ch
    out = bytearray(h * stride)
    prev = bytearray(stride)
    p = 0
    for y in range(h):
        f = raw[p]
        row = bytearray(raw[p + 1:p + 1 + stride])
        p += 1 + stride
        if f == 1:
            for i in range(ch, stride):
                row[i] = (row[i] + row[i - ch]) & 255
        elif f == 2:
            for i in range(stride):
                row[i] = (row[i] + prev[i]) & 255
        elif f == 3:
            for i in range(stride):
                left = row[i - ch] if i >= ch else 0
                row[i] = (row[i] + ((left + prev[i]) >> 1)) & 255
        elif f == 4:
            for i in range(stride):
                a = row[i - ch] if i >= ch else 0
                b = prev[i]
                c = prev[i - ch] if i >= ch else 0
                pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                pred = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                row[i] = (row[i] + pred) & 255
        out[y * stride:(y + 1) * stride] = row
        prev = row
    rgb = bytearray(w * h * 3)
    if ctype == 2:
        rgb = out
    elif ctype == 6:
        for i in range(w * h):
            rgb[i * 3:i * 3 + 3] = out[i * 4:i * 4 + 3]
    elif ctype == 0:
        for i in range(w * h):
            rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = out[i]
    elif ctype == 4:
        for i in range(w * h):
            rgb[i * 3] = rgb[i * 3 + 1] = rgb[i * 3 + 2] = out[i * 2]
    else:
        for i in range(w * h):
            k = out[i] * 3
            rgb[i * 3:i * 3 + 3] = plte[k:k + 3]
    return Img(w, h, bytes(rgb))


def compare(a, b, threshold=8, masks=()):
    """Returns dict(changed, pct, mean, bbox, mask): changed = pixels whose largest channel difference exceeds `threshold` (outside the masks, [x, y, w, h] each)."""
    if (a.w, a.h) != (b.w, b.h):
        return dict(size_mismatch=True, a=(a.w, a.h), b=(b.w, b.h))
    if np is not None:
        x = np.frombuffer(a.rgb, np.uint8).reshape(a.h, a.w, 3).astype(np.int16)
        y = np.frombuffer(b.rgb, np.uint8).reshape(b.h, b.w, 3).astype(np.int16)
        d = np.abs(x - y).max(axis=2)
        keep = np.ones(d.shape, bool)
        for mx, my, mw, mh in masks:
            keep[my:my + mh, mx:mx + mw] = False
        ch = (d > threshold) & keep
        n = int(ch.sum())
        total = int(keep.sum()) or 1
        res = dict(changed=n, pct=100.0 * n / total, mean=float(d[keep].mean()) if keep.any() else 0.0)
        if n:
            ys, xs = np.nonzero(ch)
            res['bbox'] = (int(xs.min()), int(ys.min()), int(xs.max()), int(ys.max()))
        res['mask'] = ch
        return res
    n, total, s = 0, 0, 0
    x0 = y0 = 10 ** 9
    x1 = y1 = -1
    ch = bytearray(a.w * a.h)
    ar, br = a.rgb, b.rgb
    for yy in range(a.h):
        row = yy * a.w * 3
        if ar[row:row + a.w * 3] == br[row:row + a.w * 3] and not masks:
            total += a.w
            continue
        for xx in range(a.w):
            if any(mx <= xx < mx + mw and my <= yy < my + mh for mx, my, mw, mh in masks):
                continue
            i = row + xx * 3
            d = max(abs(ar[i] - br[i]), abs(ar[i + 1] - br[i + 1]), abs(ar[i + 2] - br[i + 2]))
            total += 1
            s += d
            if d > threshold:
                n += 1
                ch[yy * a.w + xx] = 1
                x0, x1, y0, y1 = min(x0, xx), max(x1, xx), min(y0, yy), max(y1, yy)
    res = dict(changed=n, pct=100.0 * n / max(total, 1), mean=s / max(total, 1), mask=ch)
    if n:
        res['bbox'] = (x0, y0, x1, y1)
    return res


def side_by_side(a, b, cmp, path):
    """before | after | difference (changed pixels red over the dimmed after-image), halved when wider than 700 px so that one look at the file is cheap."""
    w, h = a.w, a.h
    if np is not None:
        x = np.frombuffer(a.rgb, np.uint8).reshape(h, w, 3)
        y = np.frombuffer(b.rgb, np.uint8).reshape(h, w, 3)
        ch = cmp['mask']
        diff = (y // 3).copy()
        diff[ch] = (255, 0, 0)
        sheet = np.concatenate([x, y, diff], axis=1)
        if w > 700:
            sheet = sheet[::2, ::2]
        write_png(path, Img(sheet.shape[1], sheet.shape[0], np.ascontiguousarray(sheet).tobytes()))
        return
    step = 2 if w > 700 else 1
    ch = cmp['mask']
    rows = bytearray()
    ow = (w // step) * 3
    for yy in range(0, h, step):
        row_a = bytearray()
        row_b = bytearray()
        row_d = bytearray()
        for xx in range(0, w, step):
            i = (yy * w + xx) * 3
            row_a += a.rgb[i:i + 3]
            row_b += b.rgb[i:i + 3]
            row_d += bytes((255, 0, 0)) if ch[yy * w + xx] else bytes(v // 3 for v in b.rgb[i:i + 3])
        rows += row_a[:ow] + row_b[:ow] + row_d[:ow]
    write_png(path, Img(3 * (w // step), len(range(0, h, step)), bytes(rows)))
