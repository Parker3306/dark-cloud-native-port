"""Exploration: the full record table of c01d.wgt, and whether repeated bone records hold identical entries."""
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(Path(__file__).parent))
import wgt as W  # noqa: E402
import pack as P  # noqa: E402
import mds as M  # noqa: E402

pk = P.parse((ROOT / "data/dun/mainchara/c01d.chr").read_bytes())
recs, _ = W.parse(pk.get("c01d.wgt").data)
mds = M.parse(pk.get("c01d.mds").data)
names = [o.name for o in mds.objects]
parent = [o.parent for o in mds.objects]
print("idx bone name(parent)  n   [more/unk3/type]")
for i, r in enumerate(recs):
    print(f"{i:3} {r.bone:3} {names[r.bone]:9}(p{parent[r.bone]:2}) n={len(r.entries):3} more={r.more} unk3={r.unk3:#x} type={r.type}")
seen = {}
for i, r in enumerate(recs):
    sig = tuple((e.vertex, round(e.weight, 2)) for e in r.entries)
    if r.entries and r.bone in seen:
        print(f"record {i} (bone {r.bone}) vs record {seen[r.bone][0]}: identical entries = {seen[r.bone][1] == sig}")
    elif r.entries:
        seen[r.bone] = (i, sig)
