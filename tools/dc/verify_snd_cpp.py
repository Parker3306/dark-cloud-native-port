#!/usr/bin/env python
"""Numeric check of the C++ sound engine (native/dccore: sound.h, tool `dcsound`) against the Python reference (tools/dc/snd.py).

For every case the reference WAV is rendered with `snd.py` and the C++ one with `dcsound` (reference mode: reverb sends from every voice, the
stand-in reverb without diffusion/presets, unlimited voices, no limiter), then `dcsound compare` prints RMS difference / correlation.
Outputs go to work/snd_cpp/{ref,cpp}.  Build first:  powershell -File native/build_core.ps1
usage: python tools/dc/verify_snd_cpp.py [--quick]
"""
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
DC = ROOT / "native" / "build" / "core_release" / "dcsound.exe"
ISO = ROOT / "iso" / "Dark Cloud (USA).iso"
SND = ROOT / "tools" / "dc" / "snd.py"
W = ROOT / "work" / "snd_cpp"


def run(args):
    r = subprocess.run([str(a) for a in args], capture_output=True, text=True, cwd=str(ROOT))
    if r.returncode:
        print("FAILED:", " ".join(str(a) for a in args), "\n", r.stdout, r.stderr)
        sys.exit(1)
    return r.stdout


def case(name, py_args, cpp_args):
    (W / "ref").mkdir(parents=True, exist_ok=True)
    (W / "cpp").mkdir(parents=True, exist_ok=True)
    ref, cpp = W / "ref" / (name + ".wav"), W / "cpp" / (name + ".wav")
    run([sys.executable, "-I", SND] + [a if a != "@" else ref for a in py_args])
    run([DC, ISO] + [a if a != "@" else cpp for a in cpp_args])
    out = run([DC, "compare", ref, cpp])
    print("%-14s %s" % (name, out.strip().splitlines()[-1]))


def main():
    quick = "--quick" in sys.argv
    case("bgm1", ["render-bgm", "1", "@", "--loops", "1"], ["bgm", "1", "0", "@", "--loops", "1"])
    case("bgm100", ["render-bgm", "100", "@", "--loops", "1"], ["bgm", "100", "0", "@", "--loops", "1"])
    case("voice0_400", ["render-se", "100", "400", "@", "--voice", "0"], ["se", "100", "400", "@", "--voice", "0"])
    case("sp1", ["render-special", "1", "@"], ["special", "1", "@"])
    case("se1133", ["render-se", "100", "1133", "@"], ["se", "100", "1133", "@"])
    if quick:
        return
    case("bgm150", ["render-bgm", "150", "@", "--loops", "1"], ["bgm", "150", "0", "@", "--loops", "1"])
    case("amb100", ["render-amb", "100", "0", "@", "--loops", "1"], ["amb", "100", "0", "0", "@", "--loops", "1"])
    case("amb0", ["render-amb", "0", "0", "@", "--loops", "1"], ["amb", "0", "0", "0", "@", "--loops", "1"])
    for i in (0, 1, 101, 150, 206, 296, 500):
        case("se%d" % i, ["render-se", "100", str(i), "@"], ["se", "100", str(i), "@"])
    case("sp47", ["render-special", "47", "@"], ["special", "47", "@"])


if __name__ == "__main__":
    main()
