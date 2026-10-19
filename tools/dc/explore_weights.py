"""Exploration: per-vertex chains of (bone, percent) in record order for Toan's skin mesh."""
import sys
from collections import Counter, defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "scripts"))
sys.path.insert(0, str(Path(__file__).parent))
import wgt as W  # noqa: E402
import pack as P  # noqa: E402
import mds as M  # noqa: E402

pk = P.parse((ROOT / "data/dun/mainchara/c01d.chr").read_bytes())
recs, _ = W.parse(pk.get("c01d.wgt").data)
mds = M.parse(pk.get("c01d.mds").data)
names = [o.name for o in mds.objects]
chains = defaultdict(list)
for r in recs:
    for e in r.entries:
        chains[e.vertex].append((r.bone, round(e.weight, 1)))
nverts = len(next(o for o in mds.objects if o.name == "skin").mesh.positions)
print(f"skin has {nverts} vertices; {len(chains)} appear in weight records; record order of bones:",
      [(r.bone, len(r.entries)) for r in recs if r.entries])
print("chain length histogram:", dict(Counter(len(c) for c in chains.values())))
print("vertices never weighted:", sorted(set(range(nverts)) - set(chains))[:20], "...", nverts - len(chains), "total")
for v in sorted(chains)[:25]:
    print(f" v{v:3}:", " -> ".join(f"{names[b]}({b}):{w:g}%" for b, w in chains[v]))
# do the chains end at 100%?
last = Counter(c[-1][1] for c in chains.values())
print("last weight in chain:", dict(last.most_common(8)))
print("bone names with weights:", sorted({names[b] for c in chains.values() for b, _ in c}))
