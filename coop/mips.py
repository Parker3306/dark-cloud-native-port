"""Minimal MIPS (R5900 / o32-style) assembler for the few instructions the co-op stubs need.

assemble(text, base) -> bytes. Supports labels ("name:"), '#' or ';' comments and:
  lui ori addiu addu move li nop lw sw ld sd lwc1 swc1 beq bne j jal jalr jr
`li` expands to addiu (16-bit signed) or lui+ori. Branch/jump targets are labels or absolute addresses.
selftest() checks the encoder against instruction words taken from the game's own code.
"""
import re
import struct

REGS = {n: i for i, n in enumerate(
    "zero at v0 v1 a0 a1 a2 a3 t0 t1 t2 t3 t4 t5 t6 t7 s0 s1 s2 s3 s4 s5 s6 s7 t8 t9 k0 k1 gp sp fp ra".split())}
FREGS = {f"f{i}": i for i in range(32)}


def reg(tok):
    tok = tok.strip().lstrip("$")
    if tok in REGS:
        return REGS[tok]
    if tok.isdigit():
        return int(tok)
    raise ValueError(f"bad register {tok!r}")


def freg(tok):
    return FREGS[tok.strip()]


def num(tok):
    return int(tok.strip(), 0)


def _i(op, rs, rt, imm):
    return (op << 26) | (rs << 21) | (rt << 16) | (imm & 0xFFFF)


def _r(rs, rt, rd, sa, funct):
    return (rs << 21) | (rt << 16) | (rd << 11) | (sa << 6) | funct


MEM = {"lw": 0x23, "sw": 0x2B, "ld": 0x37, "sd": 0x3F}
FMEM = {"lwc1": 0x31, "swc1": 0x39}
_MEMOP = re.compile(r"^\s*(-?\w+)?\s*\(\s*(\$?\w+)\s*\)\s*$")


def _mem(operand):
    m = _MEMOP.match(operand)
    if not m:
        raise ValueError(f"bad memory operand {operand!r}")
    off = num(m.group(1)) if m.group(1) else 0
    if not -0x8000 <= off < 0x8000:
        raise ValueError(f"offset out of range: {off}")
    return off, reg(m.group(2))


def _expand(mn, ops):
    """Pseudo-instructions -> list of (mnemonic, operands) with a fixed size, so label addresses are known."""
    if mn == "li":
        r, v = ops[0], num(ops[1])
        if -0x8000 <= v < 0x8000:
            return [("addiu", [r, "zero", str(v)])]
        v &= 0xFFFFFFFF
        return [("lui", [r, hex(v >> 16)]), ("ori", [r, r, hex(v & 0xFFFF)])]
    if mn == "nop":
        return [("sll0", [])]
    if mn == "move":
        return [("addu", [ops[0], ops[1], "zero"])]
    return [(mn, ops)]


def assemble(text, base):
    lines = []
    for raw in text.splitlines():
        line = re.split(r"[#;]", raw)[0].strip()
        if not line:
            continue
        while ":" in line.split(None, 1)[0] if line else False:
            label, line = line.split(":", 1)
            lines.append(("label", label.strip()))
            line = line.strip()
            if not line:
                break
        if line:
            mn, _, rest = line.partition(" ")
            ops = [o.strip() for o in re.split(r",(?![^()]*\))", rest)] if rest.strip() else []
            lines.extend(("insn", x) for x in _expand(mn.lower(), ops))
    # pass 1: addresses
    labels, pc = {}, base
    for kind, item in lines:
        if kind == "label":
            labels[item] = pc
        else:
            pc += 4

    def target(tok):
        tok = tok.strip()
        return labels[tok] if tok in labels else num(tok)

    out, pc = [], base
    for kind, (mn, ops) in [(k, i) for k, i in lines if k == "insn"]:
        if mn == "sll0":
            w = 0
        elif mn == "lui":
            w = _i(0xF, 0, reg(ops[0]), num(ops[1]))
        elif mn in ("ori", "addiu"):
            w = _i(0xD if mn == "ori" else 9, reg(ops[1]), reg(ops[0]), num(ops[2]))
        elif mn == "addu":
            w = _r(reg(ops[1]), reg(ops[2]), reg(ops[0]), 0, 0x21)
        elif mn in MEM:
            off, rs = _mem(ops[1])
            w = _i(MEM[mn], rs, reg(ops[0]), off)
        elif mn in FMEM:
            off, rs = _mem(ops[1])
            w = _i(FMEM[mn], rs, freg(ops[0]), off)
        elif mn in ("beq", "bne"):
            off = (target(ops[2]) - (pc + 4)) >> 2
            if not -0x8000 <= off < 0x8000:
                raise ValueError("branch too far")
            w = _i(4 if mn == "beq" else 5, reg(ops[0]), reg(ops[1]), off)
        elif mn in ("j", "jal"):
            w = ((2 if mn == "j" else 3) << 26) | ((target(ops[0]) >> 2) & 0x3FFFFFF)
        elif mn == "jr":
            w = _r(reg(ops[0]), 0, 0, 0, 8)
        elif mn == "jalr":
            w = _r(reg(ops[-1]), 0, reg(ops[0]) if len(ops) == 2 else 31, 0, 9)
        else:
            raise ValueError(f"unsupported instruction {mn}")
        out.append(w & 0xFFFFFFFF)
        pc += 4
    return b"".join(struct.pack("<I", w) for w in out)


# (word, source) pairs copied from the game's disassembly (see scripts/ghidra output), all at a known pc.
_KNOWN = [
    (0x27BDFFE0, 0x01DAD980, "addiu sp,sp,-0x20"),
    (0xFFA30000, 0x001D0C44, "sd v1,0x0(sp)"),
    (0x8F839D6C, 0x01DAD988, "lw v1,-0x6294(gp)"),
    (0x24020002, 0x01DAD98C, "li v0,2"),
    (0x1062000A, 0x01DAD990, "beq v1,v0,0x01dad9bc"),
    (0x0C76C228, 0x01DAD9AC, "jal 0x01db08a0"),
    (0x3C0201D7, 0x001D0BF0, "lui v0,0x1d7"),
    (0x2442B644, 0x001D0BF4, "addiu v0,v0,-0x49bc"),
    (0x00501021, 0x001D0BF8, "addu v0,v0,s0"),
    (0xC44C0000, 0x001D0BFC, "lwc1 f12,0x0(v0)"),
    (0xE4410000, 0x001D0B94, "swc1 f1,0x0(v0)"),
    (0x03E00008, 0, "jr ra"),
    (0x1460FFE4, 0x001D0BE8, "bne v1,zero,0x001d0b7c"),
    (0x72402628, 0, None),  # EE-specific `moveq` (or): not used by our code
]


def selftest():
    for word, pc, src in _KNOWN:
        if src is None:
            continue
        got = struct.unpack("<I", assemble(src, pc))[0]
        if got != word:
            raise AssertionError(f"{src!r} @0x{pc:08X}: got {got:08X}, game has {word:08X}")
    # the call sequence the game uses for jalr-style calls: jalr ra,t9 = 0x0320F809
    assert struct.unpack("<I", assemble("jalr ra, t9", 0))[0] == 0x0320F809
    return len(_KNOWN) - 1


if __name__ == "__main__":
    print(f"mips assembler self-test passed on {selftest()} words from the game")
