"""Round-trip test: parse and re-write every MDS in the game (standalone and inside packs); output must be byte-identical.

Usage: python -I tools/dc/test_roundtrip.py
"""
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(Path(__file__).parent))
import mds as M  # noqa: E402
from pack_list import entries  # noqa: E402


def mds_blobs():
    for f in sorted((ROOT / "data").rglob("*")):
        if not f.is_file():
            continue
        d = f.read_bytes()
        if f.suffix.lower() == ".mds":
            if d[:4] == b"MDS\0":
                yield str(f.relative_to(ROOT / "data")), d
        elif f.suffix.lower().rstrip("_") in (".chr", ".pak", ".pac", ".mpd"):
            try:
                for n, o, s in entries(d):
                    if n.lower().endswith(".mds") and d[o:o + 4] == b"MDS\0":
                        yield f"{f.relative_to(ROOT / 'data')}::{n}", d[o:o + s]
            except Exception:
                pass


def main():
    t0, ok, bad, nmesh, ntri = time.time(), 0, [], 0, 0
    seen = set()
    for name, blob in mds_blobs():
        key = hash(blob)
        if key in seen:
            continue
        seen.add(key)
        try:
            mds = M.parse(blob)
            out = M.dump(mds)
        except Exception as e:                      # noqa: BLE001
            bad.append((name, f"exception {e}"))
            continue
        if out == blob:
            ok += 1
            for o in mds.objects:
                if o.mesh:
                    nmesh += 1
                    ntri += sum(len(g.tuples) for g in o.mesh.groups)
        else:
            first = next((i for i in range(min(len(out), len(blob))) if out[i] != blob[i]), min(len(out), len(blob)))
            bad.append((name, f"differs at {first:#x} (sizes {len(blob)} -> {len(out)})"))
    print(f"{ok} unique MDS files round-trip identically ({nmesh} meshes, {ntri} vertex tuples), {len(bad)} failures, {time.time() - t0:.1f}s")
    from collections import Counter
    fails = Counter("/".join(n.replace("\\", "/").split("::")[0].split("/")[:2]) for n, _ in bad)
    print("failures by folder:", dict(fails.most_common(12)))
    wanted = ("monstor", "mainchara", "chara\\")
    chars = [(n, w) for n, w in bad if any(k in n for k in wanted)]
    print(f"failures in monsters/characters: {len(chars)}")
    for n, why in chars[:20]:
        print("  FAIL", n, why)
    kinds = Counter(why.split(" at ")[0].split(":")[0][:60] for _, why in bad)
    print("failure kinds:", dict(kinds.most_common(6)))


if __name__ == "__main__":
    main()
