# Dark Cloud (PS2) – reverse engineering workspace

Personal project for understanding how Dark Cloud (USA) works by decompiling it.

## Layout

| Folder | Contents |
|---|---|
| `iso/` | `Dark Cloud (USA).iso` (copy of the original in Downloads) |
| `extracted/` | Files pulled out of the ISO with 7-Zip |
| `ghidra/` | Ghidra project files (create the project here) |
| `notes/` | Findings; `symbols.csv` is the dumped symbol table |
| `scripts/` | Helper scripts (run with `python -I`) |
| `tools/` | Third-party tools (Ghidra plugins, etc.) |

`iso/`, `extracted/` and `ghidra/` are git-ignored: they hold game data and should never be pushed.

## Disc facts

- ISO SHA-1: `FF3D6656B1403AAE3156E5600F1AEF1489D704B9`
- Boot executable: `SCUS_971.11` (SYSTEM.CNF: `VER = 1.02`, `VMODE = NTSC`)
- Game assets: `DATA.DAT` (1.7 GB archive) indexed by `DATA.HED` / `DATA.HD2`
- IOP modules (sound, memory card, pad, CD) in `MODULES/*.IRX`

## The executable

- MIPS R5900 ELF, entry point `0x00100008`, built with **Metrowerks CodeWarrior** (`.mwcats` sections).
- **Not stripped**: `.symtab` has 12,640 symbols, including 4,181 functions with C++ mangled names
  (e.g. `SetGameFlag__9CSaveDataFii` = `CSaveData::SetGameFlag(int, int)`). See `notes/symbols.csv`.
- Sections:
  - `main` at `0x00100000` – core game code, in the ELF.
  - `title` and `dun` both at `0x01DABD00` – **overlays** (empty in the ELF, with relocation tables
    `.reltitle` / `.reldun`). Their code is most likely `TITLE.BIN` (title screen) and `DUN.BIN`
    (dungeon), swapped into the same address at runtime. Load them into Ghidra as separate overlay blocks.

## Scripts

```
python -I scripts/elf_info.py extracted/SCUS_971.11
python -I scripts/dump_symbols.py extracted/SCUS_971.11 notes/symbols.csv
```

## Installed tools (all portable, in `tools/`)

| Tool | Version | Source |
|---|---|---|
| Eclipse Temurin JDK | 21.0.12.1 | adoptium.net |
| Ghidra | 12.1.4 | github.com/NationalSecurityAgency/ghidra |
| ghidra-emotionengine-reloaded | v2.1.38 (for Ghidra 12.1.4) | github.com/chaoticgd/ghidra-emotionengine-reloaded |
| PCSX2 | 2.8.2 (portable mode) | github.com/PCSX2/pcsx2 |

All downloads were checked against the publisher's SHA-256. Ghidra is pointed at the bundled JDK through
`JAVA_HOME_OVERRIDE` in `tools/ghidra_12.1.4_PUBLIC/support/launch.properties`, and the PS2 plugin is
unpacked into `Ghidra/Extensions`. Installer zips are kept in `tools/_downloads` and can be deleted.

- **`Start Ghidra.bat`** – then File > Open Project > `ghidra/DarkCloud.gpr`.
  The program was imported with language `r5900:LE:32:default` (PS2 Emotion Engine).
- **`Start PCSX2.bat`** – needs a BIOS dumped from your own PS2 in `tools/pcsx2/bios`.

## Next steps

1. Open the project in Ghidra and browse functions by class (Symbol Tree > Namespaces), e.g. `CSaveData`.
2. Load `TITLE.BIN` / `DUN.BIN` as overlay blocks at `0x01DABD00` when you need title-screen or dungeon code.
3. Use the PCSX2 debugger to set breakpoints on functions you're reading and confirm what they do.
