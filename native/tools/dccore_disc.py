import sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent.parent
sys.path.insert(0, str(ROOT / "tools" / "dc"))
import disc as D, pack as P
def read_pack(iso, name):
    d = D.Disc(iso, readonly=True)
    return P.parse(d.read(name.replace("/", "\\")))
