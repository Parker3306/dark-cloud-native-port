"""Round-trip test for pack files: parse and re-dump every pack in the game; the output must be byte-identical."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import pack as P  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent.parent
ok, bad, total = 0, [], 0
for f in sorted((ROOT / "data").rglob("*")):
    if not f.is_file() or f.suffix.lower().rstrip("_") not in (".chr", ".pak", ".pac", ".mpd"):
        continue
    total += 1
    d = f.read_bytes()
    try:
        p = P.parse(d)
        if not p.entries:
            continue
        out = P.dump(p)
    except Exception as e:                       # noqa: BLE001
        bad.append((f.name, f"exception {e}"))
        continue
    if out == d:
        ok += 1
    else:
        first = next((i for i in range(min(len(out), len(d))) if out[i] != d[i]), min(len(out), len(d)))
        bad.append((str(f.relative_to(ROOT / 'data')), f"differs at {first:#x}, sizes {len(d)} -> {len(out)}"))
print(f"{ok}/{total} packs round-trip identically, {len(bad)} failures")
for n, why in bad[:15]:
    print("  FAIL", n, why)
