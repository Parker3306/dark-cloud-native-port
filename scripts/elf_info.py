"""Print the ELF header, program headers and section headers of a PS2 executable.

Usage: python -I scripts/elf_info.py extracted/SCUS_971.11
"""
import struct
import sys

SHT_NAMES = {0: "NULL", 1: "PROGBITS", 2: "SYMTAB", 3: "STRTAB", 4: "RELA", 8: "NOBITS",
             9: "REL", 11: "DYNSYM", 0x70000005: "MIPS_DEBUG", 0x70000006: "MIPS_REGINFO"}


def main(path):
    data = open(path, "rb").read()
    if data[:4] != b"\x7fELF":
        sys.exit("not an ELF file")
    (e_type, e_machine, _, e_entry, e_phoff, e_shoff, _, _, e_phentsize, e_phnum,
     e_shentsize, e_shnum, e_shstrndx) = struct.unpack_from("<HHIIIIIHHHHHH", data, 16)
    print(f"type={e_type} machine={e_machine} entry=0x{e_entry:08X} "
          f"phnum={e_phnum} shnum={e_shnum}")

    print("\nProgram headers (what gets loaded into RAM):")
    for i in range(e_phnum):
        p_type, p_offset, p_vaddr, _, p_filesz, p_memsz, p_flags, _ = struct.unpack_from(
            "<8I", data, e_phoff + i * e_phentsize)
        print(f"  type={p_type} vaddr=0x{p_vaddr:08X} filesz=0x{p_filesz:X} "
              f"memsz=0x{p_memsz:X} flags={p_flags}")

    if not e_shnum:
        print("\nNo section headers (stripped).")
        return
    sections = [struct.unpack_from("<10I", data, e_shoff + i * e_shentsize) for i in range(e_shnum)]
    strtab = sections[e_shstrndx]
    names = data[strtab[4]:strtab[4] + strtab[5]]

    print("\nSections:")
    for s in sections:
        name = names[s[0]:names.index(b"\0", s[0])].decode(errors="replace")
        kind = SHT_NAMES.get(s[1], hex(s[1]))
        print(f"  {name:<20} {kind:<13} addr=0x{s[3]:08X} size=0x{s[5]:X}")


if __name__ == "__main__":
    main(sys.argv[1])
