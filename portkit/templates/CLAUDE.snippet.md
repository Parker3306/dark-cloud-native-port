
## portkit (regression checks and screenshot comparison)
Read `portkit/PORTKIT.md` before running tests or looking at screenshots in this repository. In short: run `python portkit/portkit.py check` instead of the individual test scripts
(it prints one line per check and an excerpt only for failures), and `python portkit/portkit.py shots` instead of opening screenshots (it reports only the pictures that changed and writes
one before | after | diff image for each). Do not read whole logs or view unchanged screenshots.
Also: `portkit status` at the start of a chat; `portkit find TERMS` before reading notes; `portkit decomp dossier FUNC` before reading a decompiler dump; `portkit logs FILE` for long logs;
`portkit emu|multi|replay|live|bisect|assets` for the emulator, multi-window runs, recorded-input regression tests, live comparisons, regression bisecting and disc/texture inspection (all in PORTKIT.md).
