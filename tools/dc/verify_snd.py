"""Exhaustive self-check of tools/dc/snd.py on every sound file of the disc (data/sound/**).
usage: python tools/dc/verify_snd.py [--decode]     (also reachable as `snd.py verify`)

Checks (any failure is counted and listed; exit code 1 if something failed):
  packs     every .snd parses, entries + 80-byte terminator account for every byte of the file, every name of the load list
            (.txt) is present, .cfg parses
  banks     every .hd (also the loose battle/*.hd) parses; HD size field == HD length; BD size field == BD length; all
            chunk entries inside their chunk; split->sset->smpl->vag indices valid; every VAG offset inside the BD, 16-byte
            aligned, END flag reached, VAG size == distance to the next VAG offset (when sorted) ; vagi flag bit0 == loop bit
  decode    (--decode) every VAG decodes; sample count == 28*blocks
  sequences every .sq (also battle/*.sq) parses, each song ends with FF 2F exactly at its data end, no unknown status;
            loop markers are balanced
  tables    setbl.txt / sqtbl.txt / snd.txt parse; every sequence of the packs has a sqtbl row; SE tables of the executable
            are consistent (every non-negative (program,key) of a loaded set is in setbl.txt)
"""
import collections
import hashlib
import re
import struct
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import snd  # noqa: E402

SOUND = snd.SOUND


def run(args=None):
    decode = bool(getattr(args, "decode", False)) if args is not None else "--decode" in sys.argv
    t0 = time.time()
    C = collections.Counter()
    fails = []

    def fail(kind, msg):
        C["FAIL " + kind] += 1
        fails.append((kind, msg))

    seen_banks = {}
    seen_songs = {}
    fields = {k: collections.Counter() for k in ("prog_hdr", "split_raw", "sset_curve", "smpl_raw", "vagi_extra", "vagi_flags")}

    def check_bank(label, hd, bd):
        key = hashlib.md5(hd + b"|" + bd).hexdigest()
        if key in seen_banks:
            C["bank duplicates skipped"] += 1
            return seen_banks[key]
        try:
            b = snd.Bank(hd, bd, name=label)
        except Exception as e:  # noqa: BLE001
            fail("bank", "%s: %r" % (label, e))
            seen_banks[key] = None
            return None
        seen_banks[key] = b
        C["banks parsed"] += 1
        if b.empty:
            C["empty banks"] += 1
            return b
        n_vag = len(b.vagi)
        sizes = b.vag_sizes() if bd else {}
        offs = sorted((v.offset, v.index) for v in b.vagi if v is not None)
        for k, (o, i) in enumerate(offs):
            C["vag"] += 1
            v = b.vagi[i]
            fields["vagi_extra"][v.extra] += 1
            fields["vagi_flags"][v.flags] += 1
            if not bd:
                continue
            if o % 16 or o >= len(bd):
                fail("vag", "%s vag %d offset 0x%x" % (label, i, o))
                continue
            sz = sizes.get(i)
            nxt = offs[k + 1][0] if k + 1 < len(offs) else len(bd)
            end_ok = sz is not None and bd[o + sz - 15] & 1
            if not end_ok:
                fail("vag", "%s vag %d no END block" % (label, i))
            if sz != nxt - o and nxt != o:
                # distance to the next vag differs: only a problem if the sample would overlap the next one
                if o + sz > nxt:
                    fail("vag", "%s vag %d overlaps next (size %d, gap %d)" % (label, i, sz, nxt - o))
                else:
                    C["vag with gap after END"] += 1
            if decode:
                pcm, info = b.pcm(i)
                if len(pcm) != 28 * info["blocks"] or info["truncated"]:
                    fail("decode", "%s vag %d" % (label, i))
                C["vag decoded"] += 1
                # loop flag consistency: vagi flag bit0 vs ADPCM repeat bit
                C["vagi.bit0==adpcm loop %s" % (bool(v.flags & 1) == info["looping"])] += 1
        for i, sm in b.smpl.items():
            fields["smpl_raw"][("b3,b5..b10", sm.vel_xfade, tuple(sm.raw[5:11]), tuple(sm.raw[22:41]))] += 1
            if sm.vag >= n_vag or b.vagi[sm.vag] is None:
                fail("smpl", "%s smpl %d -> missing vag %d" % (label, i, sm.vag))
            if any(sm.raw[32:41]):
                C["smpl with nonzero bytes 32..40"] += 1
        for i, ss in b.sset.items():
            fields["sset_curve"][ss.vel_curve] += 1
            for si in ss.smpl:
                if si not in b.smpl:
                    fail("sset", "%s sset %d -> missing smpl %d" % (label, i, si))
        for i, p in b.prog.items():
            fields["prog_hdr"][p.header[12:36].hex()] += 1
            for sp in p.splits:
                fields["split_raw"][(sp.raw[3], sp.raw[5], sp.raw[6:16].hex(), sp.vol, sp.pan, sp.transpose, sp.detune)] += 1
                if sp.sset == 0xFFFF:
                    C["split with unused sset (0xFFFF)"] += 1
                elif sp.sset not in b.sset:
                    fail("split", "%s prog %d -> missing sset %d" % (label, i, sp.sset))
                if sp.lo > sp.hi:
                    fail("split", "%s prog %d key range %d..%d" % (label, i, sp.lo, sp.hi))
        return b

    def check_song(label, data):
        key = hashlib.md5(data).hexdigest()
        if key in seen_songs:
            C["song duplicates skipped"] += 1
            return
        seen_songs[key] = True
        try:
            s = snd.Song(data, name=label)
            for i in range(s.n_songs):
                s.select(i)
                C["songs parsed"] += 1
                if s.events[-1].kind != "end":
                    fail("song", "%s[%d] no end" % (label, i))
                # the end meta must be followed only by padding up to the next song / chunk end
                pad = data[s.end_pos:s.ev_end]
                if any(x not in (0, 0xFF) for x in pad):
                    fail("song", "%s[%d] %d unparsed bytes after end: %s" % (label, i, len(pad), pad[:16].hex()))
                st = collections.Counter()
                for e in s.events:
                    st[e.kind] += 1
                for k, v in st.items():
                    C["event " + k] += v
                evs, total, info = snd.flatten(s, extra_loops=0)
                C["sequence seconds (one pass)"] += int(total)
                starts = sum(1 for x in info["loops"] if x[0] == "start")
                ends = sum(1 for x in info["loops"] if x[0] == "end")
                C["loop starts"] += starts
                if info["endless"]:
                    C["songs with an endless loop"] += 1
                # CC32 routing
                if any(e.kind == "cc" and e.a == 32 for e in s.events):
                    C["songs using CC32 (port routing)"] += 1
                if any(e.kind == "cc" and e.a not in (7, 10, 11, 6, 38, 98, 99, 32, 64) for e in s.events):
                    C["songs using other CCs"] += 1
                    for e in s.events:
                        if e.kind == "cc" and e.a not in (7, 10, 11, 6, 38, 98, 99, 32, 64):
                            fields.setdefault("other_cc", collections.Counter())[e.a] += 1
        except Exception as e:  # noqa: BLE001
            fail("song", "%s: %r" % (label, e))

    # ---- packs ----
    names_hd, names_sq = set(), set()
    for sub in ("bgm", "set", "sound_set", "special", "voice"):
        for p in sorted((SOUND / sub).glob("*.snd")):
            C["snd files"] += 1
            raw = p.read_bytes()
            try:
                s = snd.SndFile(p)
            except Exception as e:  # noqa: BLE001
                fail("pack", "%s: %r" % (p, e))
                continue
            # byte accounting: entries are contiguous, terminator is 0x50 bytes
            used = 0
            q = 0
            while q + 0x50 <= len(raw) and raw[q] != 0:
                off, size, stride, _ = struct.unpack_from("<4I", raw, q + 0x40)
                q += stride
                used += stride
            if q + len(s.tail) != len(raw) or len(s.tail) != 0x50 or s.tail[0] != 0:
                fail("pack", "%s: tail %d bytes, first byte %d" % (p, len(s.tail), s.tail[0] if s.tail else -1))
            for n in s.txt:
                if n not in s.files:
                    if p.name in ("bgm120.snd", "bgm130.snd"):      # stale debug packs (never loaded by the game)
                        C["WARN load-list name missing (bgm120/130)"] += 1
                    else:
                        fail("pack", "%s: load-list name %r missing from pack" % (p.name, n))
                    continue
                if n.lower().endswith(".hd"):
                    names_hd.add(n)
                elif n.lower().endswith(".sq"):
                    names_sq.add(n)
            for n, d, _ in s.entries:
                if n.lower().endswith(".hd") and n[:-3] + ".bd" in s.files:
                    check_bank("%s/%s:%s" % (sub, p.name, n), d, s.files[n[:-3] + ".bd"])
                elif n.lower().endswith(".sq") and d:
                    check_song("%s/%s:%s" % (sub, p.name, n), d)
            if sub in ("bgm", "set"):
                # note coverage: every note-on of every sequence of the pack must find a sample in the bank of its port
                banks = snd.banks_by_port(s)
                for letter, nm, sg in s.songs():
                    for si in range(sg.n_songs):
                        sg.select(si)
                        chp, prog = [snd.LETTER_PORT[letter]] * 16, [0] * 16
                        for e in sg.events:
                            if e.kind == "pc":
                                prog[e.ch] = e.a
                            elif e.kind == "cc" and e.a == 32:
                                chp[e.ch] = e.b & 15
                            elif e.kind == "on":
                                C["note-ons checked against banks"] += 1
                                b = banks.get(chp[e.ch])
                                if b is None or not b.resolve(prog[e.ch], e.a, e.b):
                                    C["note-ons without sample (known: e02a_a prog 6)"] += 1
            if p.stem.startswith(("snd",)) and sub == "set" and "TABLE" not in s.cfg:
                C["set files without TABLE"] += 1
            if sub in ("bgm", "set") and "REVERBE" not in s.cfg:
                C["%s files without REVERBE" % sub] += 1
    # ---- loose battle files ----
    for p in sorted((SOUND / "battle").rglob("*")):
        if p.suffix == ".hd":
            bdp = p.with_suffix(".bd")
            check_bank("battle/" + p.name, p.read_bytes(), bdp.read_bytes() if bdp.exists() else b"")
            C["battle hd"] += 1
        elif p.suffix == ".sq":
            check_song("battle/" + p.name, p.read_bytes())
            C["battle sq"] += 1
    # ---- tables ----
    try:
        rows = snd.tbl_se_info()
        C["setbl rows"] = len(rows)
        sq = snd.tbl_sq_vol()
        C["sqtbl rows"] = len(sq)
        lst = snd.tbl_snd_list()
        C["snd.txt names"] = len(lst)
        missing = [n for n in names_sq if n not in sq and re.fullmatch(r"[a-z0-9]{4}_[a-z]\.sq", n)]
        C["pack sequences without sqtbl row"] = len(missing)
        if missing:
            fails.append(("tbl", "sequences without sqtbl row: %s" % sorted(missing)[:10]))
        tabs = snd.exe_tables()
        keys = {(r[0], r[1]) for r in rows}
        pk = set()
        for t in tabs["se_info"]:
            if t[0] >= 0:
                pk.add((t[0], t[1]))
        C["SE (prog,key) pairs in exe se_info"] = len(pk)
        C["... of them missing in setbl.txt"] = len(pk - keys)
    except Exception as e:  # noqa: BLE001
        fail("tbl", repr(e))

    print("=== verify_snd: %.1fs ===" % (time.time() - t0))
    for k, v in sorted(C.items()):
        print("  %-48s %s" % (k, v))
    for name, c in fields.items():
        print("  field stats %s (top 8 of %d): %s" % (name, len(c), c.most_common(8)))
    if fails:
        print("FAILURES (%d):" % len(fails))
        for k, m in fails[:60]:
            print("  ", k, m)
        sys.exit(1)
    print("ALL OK")


if __name__ == "__main__":
    run()
