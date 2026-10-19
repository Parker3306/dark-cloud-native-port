"""Test the weight-file tools on every .wgt in the game: round-trip, and rebuild from per-vertex weights (must be identical)."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))
import pack as P  # noqa: E402
import wgt as W  # noqa: E402

ok = bad = rebuilt_ok = n = 0
fails = []
for f in sorted((ROOT / "data").rglob("*")):
    if not f.is_file() or f.suffix.lower().rstrip("_") not in (".chr", ".pak", ".pac", ".mpd"):
        continue
    try:
        pk = P.parse(f.read_bytes())
    except Exception:
        continue
    for e in pk.entries:
        if not e.name.lower().endswith(".wgt"):
            continue
        n += 1
        try:
            recs, used = W.parse(e.data)
            same = W.write(recs) == e.data[:used] and used == len(e.data)
            vw = W.vertex_weights(recs)
            rb = W.write(W.build(recs, vw, min_percent=-1)) == e.data
        except Exception as ex:                     # noqa: BLE001
            fails.append((str(f.relative_to(ROOT / "data")), e.name, f"exception {ex}"))
            continue
        ok += same
        rebuilt_ok += rb
        if not (same and rb):
            fails.append((str(f.relative_to(ROOT / "data")), e.name, f"roundtrip={same} rebuild={rb}"))
print(f"{n} .wgt files: {ok} round-trip identical, {rebuilt_ok} rebuild identical from per-vertex weights")
for x in fails[:12]:
    print("  ", x)
