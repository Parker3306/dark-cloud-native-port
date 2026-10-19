"""Dump the .symtab of a PS2 ELF to CSV (address, size, type, section, name).

Usage: python -I scripts/dump_symbols.py extracted/SCUS_971.11 notes/symbols.csv
"""
import csv
import struct
import sys

STT = {0: "NOTYPE", 1: "OBJECT", 2: "FUNC", 3: "SECTION", 4: "FILE"}


def cstr(blob, off):
    return blob[off:blob.index(b"\0", off)].decode(errors="replace")


def main(path, out_path):
    data = open(path, "rb").read()
    e_shoff, = struct.unpack_from("<I", data, 32)
    e_shentsize, e_shnum, e_shstrndx = struct.unpack_from("<HHH", data, 46)
    sections = [struct.unpack_from("<10I", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    shstr = data[sections[e_shstrndx][4]:][:sections[e_shstrndx][5]]
    sec_names = [cstr(shstr, s[0]) for s in sections]

    symtab = next(s for s in sections if s[1] == 2)
    strtab = sections[symtab[6]]  # sh_link points at the symbol string table
    names = data[strtab[4]:strtab[4] + strtab[5]]

    rows = []
    for off in range(symtab[4], symtab[4] + symtab[5], 16):
        st_name, st_value, st_size, st_info, _, st_shndx = struct.unpack_from("<IIIBBH", data, off)
        if not st_name:
            continue
        sec = sec_names[st_shndx] if st_shndx < len(sec_names) else hex(st_shndx)
        rows.append((f"0x{st_value:08X}", st_size, STT.get(st_info & 0xF, st_info & 0xF), sec,
                     cstr(names, st_name)))

    rows.sort(key=lambda r: (r[3], r[0]))
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["address", "size", "type", "section", "name"])
        w.writerows(rows)

    by_type = {}
    for r in rows:
        by_type[r[2]] = by_type.get(r[2], 0) + 1
    print(f"{len(rows)} symbols -> {out_path}")
    for t, n in sorted(by_type.items(), key=lambda x: -x[1]):
        print(f"  {t}: {n}")


if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2])
