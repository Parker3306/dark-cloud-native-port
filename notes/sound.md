# Dark Cloud sound: formats, tables, runtime model, native mapping (2026-10-19)

Tools: `tools/dc/snd.py` (parsers, PS-ADPCM decoder, reference synth, `SoundModel` = executable spec of the game's `Snd*` layer),
`tools/dc/verify_snd.py` (exhaustive self-check, also `snd.py verify [--decode]`), C++ port: section 13. Test renders: `work/snd_test/*.wav`.

Sources: the game code (`CSound::*`, `Snd*`, `LoadSoundFileFromPack`, `SetSoundFile`, ... in `work/decomp/all_dun.c`), the IOP modules
(`EZMIDI.IRX` = Dark Cloud's RPC front end; `MODMIDI.IRX` = Sony libmidi sequencer; `MODHSYN.IRX` = Sony libhsyn hardware synth; `MSIFRPC.IRX`
= RPC front end of the MIDI stream input used for effects; only skimmed), decompiled with Ghidra headless (project `work/ghidra_irx`, C dumps `work/snd_scratch/{ezmidi,modmidi,modhsyn,msifrpc}.c`),
the executable's data tables (read from the ISO through `tools/dc/elfdata.py`), and the data itself.
Status labels: **[verified]** = checked numerically/structurally on all files or against the IOP code, **[guess]** = plausible, not proven.

## 1. What is on the disc

```
data/sound/bgm/bgm<N>.snd      57 files  music (bank A + sequences), N = BGM_NO of the maps' mapinfo.cfg
data/sound/set/snd<N>.snd      62 files  sound sets: banks C,E,I,G,M + ambient sequences + SE table numbers   (SOUND_SET = N)
data/sound/voice/voice<N>.snd   7 files  character voices (bank S), N = character id (0 Toan,1 Xiao,2 Goro,3 Ruby,4 Ungaga,5 Osmond,6 "turi")
data/sound/special/sp<N>.snd   62 files  "special SE" (bank Q), N = special SE number; 12 of them are empty stubs (352 bytes: 4,5,7,9,10,11,22,23,24,35,42,49)
data/sound/tbl/setbl.txt, sqtbl.txt      read by the game (LoadSndTxt)         ; snd.txt = authoring manifest, never read by the game
data/sound/sound_set/snd<N>.snd 29 files  OLDER/alternate copy of the sound sets: NOT referenced by the executable
data/sound/battle/{d01,d02,public}/*   loose .hd/.bd/.sq: prototype battle/jingle data, NOT referenced by the executable
```
The executable only builds the paths `sound/bgm/bgm%d.snd`, `sound/set/snd%d.snd`, `sound/voice/voice%d.snd`, `sound/special/sp%d.snd`,
`sound/tbl/setbl.txt`, `sound/tbl/sqtbl.txt` (string table at 0x299f50..0x29a000, 0x299360). There is no string for `sound_set`, `battle` or `snd.txt`.
`sound_set` has the same file names as `set` but different (older) banks: no G bank, no `TABLE` line in the cfg, bigger I banks (e.g. e01a_i
480,960 vs 317,472 bytes), different sysa_m (52,416 vs 115,280), other reverb depths; 33 sets exist only in `set`. The loose `battle/*` files use exactly
the same formats (dan01bgm.bd is byte-identical in size to bgm100's d01a_a.bd; dan0Na/b/c.sq are the calm/medium/full variants of the dungeon
BGM; evwin/item/use/ivbt/stepdw are short jingles on the dan0Nsc/bg banks); nothing in the executable or the data refers to them.

## 2. `.snd` = the game's generic pack  [verified, 217/217 files]

Same container as `.pak/.chr` (`tools/dc/pack.py`, `GetPackFile @0x13F720`): entries of `0x50`-byte header + data, list ends at an entry whose first
name byte is 0 (that 0x50-byte terminator is the file tail; its remaining bytes are stack garbage).

| offset | size | field |
|---|---|---|
| 0x00 | 0x40 | file name, NUL terminated (rest of the field is uninitialised junk, e.g. `nd\bgm\bgm1.cfg`) |
| 0x40 | 4 | data offset from entry start (always 0x50) |
| 0x44 | 4 | data size |
| 0x48 | 4 | stride to the next entry (0x50 + size rounded up to 16) |
| 0x4C | 4 | junk (constant per file) |

Entries (order is arbitrary; the game looks files up by name): `X.hd`, `X.bd`, `X.sq`, `<pack>.txt`, `<pack>.cfg`, plus a dummy entry named SJIS 0x819B + `.sq` (size 0; listed in the .txt of most BGM packs).
* **`<pack>.txt` = load list** (CRLF text, one file name per line). `LoadSoundFileFromPack(0x1440d0)` walks it with `strtok`; for every name the 6th character
  (index 5, the letter before the extension: `e01a_a.hd`) selects the slot: `.hd` => bank letter (the matching `.bd` is fetched implicitly), `.sq` => sequence
  list of that letter. Sequences are numbered per letter in load-list order (that number is the argument of `SndBgmPlay` / `SndAmbientPlay`).
  The `.bd` names are never listed. Two debug packs name sequences that are not in the pack: `bgm120.txt` lists `d03a_a.sq`, `bgm130.txt` lists `d04a_a.sq`
  (stale; the game never loads 110/120/130 except from a debug key handler: `SndBgmLoad(DAT_01d56484 * 10 + 100)`).
* **`<pack>.cfg`** is parsed by `LoadSoundInfo (0x15bab0)` with the script tag table at 0x266740: `REVERBE mode,depth` and (sets only) `TABLE basic,cap`.
  BGM packs: `SetReverb(core 0, mode, depth)`; sound sets: `SetReverb(core 1, mode, depth)`, `basic_se_table_no = basic`, `se_table_no = cap`.
  mode = libsd reverb preset (2 STUDIO_A, 3 STUDIO_B, 4 STUDIO_C), depth = effect return volume = `depth << 8` (of 0x7fff) => depth/128.
  (mapinfo.cfg has its own `REVERBE "STUDIO_C",30,"STUDIO_A",5` line that overrides these per map; not decoded further.)
* The pack files' reverb/TABLE numbers and bank names for every BGM / sound set are listed in section 9.

## 3. Bank = `.hd` + `.bd`  (Sony libhsyn "HD/BD")  [verified, 561 .hd entries / 227 distinct banks / 2,871 VAGs]

All Sony chunks start with an 8-byte magic that is two byte-swapped words (`SCEIVers` is stored `IECSsreV`), `u32 chunkSize`; all multi-byte values little endian.

```
HD file:  SCEIVers (0x10)  SCEIHead (0x40)  SCEIVagi  SCEISmpl  SCEISset  SCEIProg   (order in the file; the offsets are in the Head chunk; magics are stored byte-swapped, e.g. `IECStesS`)
Vers  : +8 u32 0x10, +0xC u8[4] = 00 00 01 01   (byte 0xE = 1 = format major; the loader reads an extra chunk (Head+0x24) only if byte 0xE > 1: never)
Head  : +0x08 u32 0x40 | +0x0C u32 HD file size (== len(.hd), checked) | +0x10 u32 BD size (== len(.bd), checked)
        +0x14 u32 Prog offset | +0x18 Sset | +0x1C Smpl | +0x20 Vagi | +0x24.. 0xFFFFFFFF
Every table chunk: +8 u32 chunkSize, +0xC u32 maxIndex (0xFFFFFFFF = empty), +0x10 u32 offset[maxIndex+1] (from the chunk start; 0xFFFFFFFF = unused slot), then the records.
```
The `.bd` is raw PS-ADPCM with no header; sample `i` starts at `Vagi[i].offset` and ends with the block whose flag byte has bit0 (END).

**Vagi record (8 bytes)**: `u32 offset` into the BD | `u16 rate` (Hz; 18,430..44,100; 44,100 and 22,050 dominate, music multisamples are 43,000..44,100 because they were resampled to their pitch) | `u8 flags` (bit0 = the sample loops; equals the
ADPCM repeat bit of its last block in all 2,871 samples) | `u8 0xFF`.  VAGs are stored 16-byte aligned, back to back (gap after END in 1,851 cases = padding).

**Smpl record (42 bytes)** = "tone" (what the SPU voice plays):
| off | field | meaning |
|---|---|---|
| 0 | u16 | VAG index |
| 2 | u8 | velocity low (1 normally) |
| 3 | u8 | velocity cross-fade (64; unused by the note-on code) |
| 4 | u8 | velocity high (127) |
| 5..10 | 00 01 00 00 01 00 | velocity-follow of the two LFO depths (modhsyn `FUN_000042a4`); inert in all data |
| 11 | u8 | **base key**: MIDI key at which the VAG plays at its own rate (60 for 5,756 of 7,101; music multisamples use 28,32,..,72,76,80) |
| 12 | s8 | fine tune, 1/128 semitone (0 everywhere) |
| 13 | u8 | pan (0..127, 64 centre) |
| 14 | u8 | exclusive group (0 none; 1 for 55 samples of sysa_m) [guess: choke group, `FUN_00006900`] |
| 15 | u8 | priority (10 normally; 3,4,1,11,15 for a few) |
| 16 | u8 | sample volume (128 = unity, 127 for some SE) |
| 18 | u16 | **ADSR1** (SPU2 format: bit15 attack exponential, 14..8 attack rate, 7..4 decay rate, 3..0 sustain level) e.g. 0x80FF |
| 20 | u16 | **ADSR2** (bit15 sustain exponential, 14 sustain decreasing, 12..6 sustain rate, 5 release exponential, 4..0 release rate) e.g. 0xDFE8 |
| 22..31 | 5 x (s8 amount, u8 centre key=60) | key-follow of the ADSR fields (`FUN_00003594/38`, order: 22 attack rate, 24 decay rate, 26 sustain rate, 28 release rate, 30 sustain level); (0,60) everywhere except 22 samples with (-1,48) in the sustain-rate slot |
| 32..40 | 0 | LFO/pitch-EG parameters (`FUN_00008b64`), all zero in the data |
| 41 | u8 | bits 4..5 = SPU2 core (0x10 core 0 = music, 0x20 core 1 = effects), bits 0..3 = mixer routing (0xF = dry stereo + reverb send, 0x3 = dry only) |

**Sset record (4+2n bytes, n = 1 in all data)**: `u8 velocity curve` (0 = identity, 2 = `v*v/127`; the table is `PTR_FUN_0000a7f8` in MODHSYN), `u8 vel low`, `u8 vel high`,
`u8 n`, `u16 Smpl index[n]`.

**Prog record**: 36-byte header + `nSplit` x 20-byte split blocks (header[0] = u32 0x24 = offset of the first split, [4] = nSplit, [5] = 0x14 = split size):
| header off | meaning |
|---|---|
| 6 | program volume (128 = unity) |
| 7 | program pan (64) |
| 8, 9 | transpose (semitones), detune (1/128 semitone); 0 |
| 10, 11 | key-follow pan: amount (s8, in 1/12 pan units per key), centre key — program 0 of e01a_a has 12 @ 35, i.e. notes pan to the right as they go up |
| 12..35 | LFO / bend defaults (`00 ff 00 00 .. 64 00 64 00 80 00 80 00 80 00 80 00 ..`): constant in 763 of 766 programs |

Split block (20 bytes): `u16 Sset index` (0xFFFF = unused slot, 2,124 of them) | `u8 key low` | `u8 cross-fade (60, unused)` | `u8 key high` | `u8 split number` |
`u16 bend range down` | `u16 bend range up` (0x0600 = 6 semitones at full bend, i.e. value/256 semitones) | `u8,u8 key-follow pitch (amount,centre)` | `u8,u8 key-follow pan` | `u8,u8 key-follow volume` |
`u8 volume (128)` | `u8 pan (64)` | `s8 transpose` | `s8 detune`.

**Note-on resolution (modhsyn `FUN_00005f90`)** [verified by code reading + coverage test]: for a note (program, key, velocity) every split with `lo <= key <= hi`
whose Sset has `vel_lo <= vel <= vel_hi` starts one voice per Smpl of the Sset whose own velocity range matches (all of them, i.e. layers are summed).
Pitch ratio = `vag.rate/48000 * 2^((key + prog.transpose + split.transpose + (prog.detune + smpl.fine + split.detune)/128 - smpl.base_key)/12)` (+ pitch bend, see 5).
The pitch formula is `sceSdNote2Pitch(base_key, 0, note, fine) * rate / 48000` (modhsyn `FUN_00003ef8/9760`, clamped to 0x3FFF = 4x), i.e. the code is authoritative.
Independent check on the data: in bgm1's bank the loop length of 5 of 8 bass samples is an integer multiple (4.00, 4.00, 2.00, 3.00, 3.00 periods) of the period predicted from (base key, rate), the autocorrelation of the three high samples
gives keys 72.05 / 75.97 / 80.02 for base keys 72 / 76 / 80; over all 919 distinct looping samples 35% have an integer number of periods at the declared base key (chance ~12%), 25% only one octave higher (the sound designers
sometimes declared the base key an octave below the sampled pitch, so the loop test cannot prove the base key by itself).
Strongest confirmation: SE/voice banks map one key to one one-shot sample with `split.transpose = base_key - key` (voice0: key 20, transpose +40, base 60), and `render-se` then produces exactly the sample's own duration (Toan voice 400: 0.2248 s = 9,912 samples at 44.1 kHz).
Of 98,145 note-ons in all 107 distinct sequences exactly 15 (program 6 of e02a_a.sq) find no sample: bank/program/split decoding is consistent with the data.

## 4. PS-ADPCM  [verified: 2,871/2,871 samples decode with END flag; vagi loop bit == ADPCM repeat bit]

Standard SPU ADPCM, 16-byte blocks = 28 samples: byte0 low nibble shift, high nibble predictor (filters (0,0) (60,0) (115,-52) (98,-55) (122,-60), >>6);
byte1 flags (bit0 END, bit1 repeat -> jump to loop start, bit2 loop start). A sample that ends with flag 1 (no repeat) simply stops; with flag 3 it jumps back to the
last block carrying flag 4 (or to the first block). `snd.adpcm_decode` returns the loop start; `snd.py wav pack outdir` writes every VAG as mono 16-bit WAV at its Vagi rate
(e.g. `work/snd_test/samples_bgm1/*.wav`).

## 5. Sequence = `.sq` (Sony libmidi "SCEIMidi")  [verified: 107 distinct sequences, 217 files, all end exactly with FF 2F]

```
SCEIVers (0x10)  SCEISequ (0x20: +8 size, +0xC total file size, +0x10 song-chunk offset or -1, +0x14 Midi-chunk offset (0x30), +0x18,+0x1C -1)  SCEIMidi
SCEIMidi: +8 u32 chunk size, +0xC u32 (number of songs - 1), +0x10 u32 song offset[n] (from the SCEIMidi chunk start)
song:     u32 6 (offset of the event data from the song start) | u16 division (ticks per quarter; 480 in every file) | events...
```
The event stream is Format-0 MIDI with a compact coding (decoded exactly like `MODMIDI FUN_00000f78/9b8`; `snd.parse_events`):
* A VLQ delta precedes the first event. After an event a VLQ delta follows **unless the MSB of the event's last data byte is set**; then the next event is simultaneous
  (delta 0) and the byte's low 7 bits are the value. (Mixed: for 2-data-byte events the flag is `(d1|d2)&0x80`.)
* Status byte, running status allowed. Data bytes: `8n` note-off has ONE (key); `9n` note-on key,vel (vel 0 = off); `An` poly pressure 2; `Bn` controller,value; `Cn` program 1; `Dn` pressure 1; `En` bend lsb,msb.
* `FF type len data`: `51` tempo (3 bytes, microseconds per quarter), `2F` end; others skipped. `F0/F7 len data` sysex skipped.
* Controllers used (30,353 CC events): 7 volume, 10 pan, 11 expression, plus the Sony loop controllers: **CC99=0 + CC6=id** loop start,
  **CC99=1 + CC6=id + CC38=n** loop end (n = 0: endless; otherwise the body is repeated n more times). `CC32` would re-route a channel to another hsyn port (value & 15) — unused in all data.
  CC1, 64 handled by the hardware synth but only one CC1 exists. The loop machine (`FUN_00000654`) saves the position right after the CC6 of a loop start and jumps back at the CC38.
* Tempo: `us per tick = tempo / division`. 132 tempo events; the first one is at tick 0.
* In all 90 looping songs the loop end is followed by a "tail" (rest of the arrangement up to `FF 2F`) that is never played while looping (e.g. bgm1: loop 7.79 s .. 50.99 s of 55.2 s).
* Channels: music uses MIDI channels 1..n (channel 0 only carries the loop CCs); each channel has its own program (`C n`) into the bank of the player's port.

## 6. Runtime model: ports, banks, players (IOP side)  [verified from EZMIDI/MODMIDI/MODHSYN code and the game's `LoadHdBd_*`]

The IOP has 3 MIDI **players** (sequencers, 0..2) and 16 hsyn **ports** (each holds one bank = one HD + BD in SPU2 RAM; each port has 16 MIDI channels).
Player n feeds hsyn port n by default (per-channel destination mask `1 << port`, changeable with CC32). Ports 10..15 are fed by 6 "MSIn" live streams (sound effects).

| bank letter | file kind | hsyn port | loaded by | purpose |
|---|---|---|---|---|
| A | `bgm*.snd`: `x_a.hd` | 0 (player 0) | SndBgmLoad | music bank (SPU address 0x5010, up to 0x78000 bytes; biggest BD = 490,944) |
| E | set / bgm200 | 1 (player 1) | SndSoundLoad | ambient / battle-layer sequences (`SndAmbientPlay`), also effect samples |
| I | set | 2 (player 2) **and** 14 | SndSoundLoad | effects / item / character sounds (loaded twice: once per use) |
| G | set | 10 | SndSoundLoad | per-area ground/environment effects (ids 600..2599); absent in 11 sets: 54-57, 300-306 |
| S | voice | 11 | SndVoiceLoad | character voices |
| Q | special | 12 | SndSPSeLoad | one "special" sound per file |
| M | set (`sysa_m`) | 13 | SndSoundLoad | system/menu sounds (ids 0..24, prog 122) |
| C | set | 15 | SndSoundLoad | common/character effects (ids 50..76, prog 16) |

Core assignment (Smpl byte 41): A = core 0 (music reverb `SetReverb(0,...)` from the bgm cfg); everything else core 1 (`SetReverb(1,...)` from the set cfg).
Port volumes (`ezMidi 0xB0+port`, hsyn `SetVolume`, 0x100 = unity; voice level = `... * port_vol >> 8`): sets/voices/specials are reset to **0x100** on load;
the BGM player gets the `sqtbl.txt` value of the sequence (e.g. e01a_a.sq 102, m01a_e.sq 140) when `SQ_Play` runs, so music plays at about vol/256.
Output level model (modhsyn `FUN_000036b4/40f0`): `amp = prog.vol/128 * split.vol/128 * smpl.vol/128 * velcurve(vel)/127 * CC7/127 * CC11/127 * port_vol/256`, per-voice
SPU volume 0x4000 = 1.0; pan law = linear balance (`FUN_00004534`: for pan p>64 the left gain is `(127-p)/63`, for p<64 the right gain is `(p-1)/63`, the other side stays 1).
Voice priority = `smpl.priority + port priority` (port attr `0xA0+port`: 0x3020 for A, 0x3070 C, 0x3060 E, 0x3050 I(2), 0x3040 I(14)/G, 0x3080 M, 0x3090 Q/S: low byte priority,
high byte 0x30 = voice limit [guess]); when no voice is free the lowest priority voice is stolen (`FUN_00005e0c`). 48 SPU2 voices (24 per core).
Envelope: SPU2 hardware ADSR (nocash model); my closed-form version in `snd.adsr_envelope` equals a brute-force per-sample simulation to 2.4e-4 for all 61 distinct ADSR pairs of 40 packs.

**Sound-effect messages** (`CSound::SE_Play*`, `sceMSIn_PutMsg/PutHsMsg`; EE buffers are flushed to the IOP once per frame in `CSound::Step`):
`SE_Play(port, prog, key, pan, vel, vol, id)` sends on stream `port-10`: MIDI `C0 prog` (channel 0), then hs-messages: `F9 00 vol` (volume of the *next* note, 0..127),
`F9 01 pan` (pan of the next note, 64 centre), `FD 10 0 key id vel` (note-on key with voice id `id`, velocity 0x7F). `SE_Stop` = `FD 10 0 key id 0` (note-off of that key/id).
`SE_SetVol/SE_SetPan` = `FD 00/01 ... key id value` (retarget the running voice). There is no note-off for one-shot effects; looping effects run until `SndSeStop`.

**IOP RPC `EZMIDI` (bind 0x12346, `ezMidi(cmd, arg)`)** [verified from `midiFunc @0x20c`]: `0x00+n` start player n; `0x20+n` stop; `0x30+n` seek (SQ_Play sends 0 first);
`0x40+n` set sequence (IOP address; `MidiSetSq` runs `sceMidi_Load/SelectMidi/SetLocation`); `0xA0+p` port attr; `0xB0+p` port volume; `0xC0` output mode (stereo/mono,
`SetStereoMode(config==0)`); `0x8010` init; `0x80E0+p` get port volume; `0x8090+n` player status; `0x80D0` voices in use; `0x80F0` quit; `0x9050+p` set HD for port p
(struct {hd iop addr, bd iop addr, bd size, SPU address}); `0x9060` BD packet transfer; `0x9070` BD transfer; `0x9080` file length.
SPU2 reverb: `CSound::SetReverb(core, mode, depth)` via `sceSdRemote` (effect area = top 0x20000 bytes of RAM minus 0x20000 per core, EVOL = depth<<8, MVOL 0x3fff).

## 7. Tables  [verified]

`sqtbl.txt` (94 rows `name.sq<TAB>vol`): per sequence port volume (see 6); **every** `.sq` in every pack has a row. `LoadSqInf` stores {name[12], vol}.
`setbl.txt` (2,976 rows `PRG.NO KEY VOL` after a one-line header): `seNo` = row index; `GetSeNo(prog,key)` (0x143b70) returns the row of the (prog,key) pair; `vol` is the base volume (0..127)
of that effect. (prog,key) not in the table -> volume 0x40. 19 of the 2,074 (prog,key) pairs used by the executable are not in the file.
`snd.txt`: 186 `.hd` + 94 `.sq` names, the authoring manifest (not read by the game).

**SE id -> (program, key, port)** (`GetSeInfo @0x15a0b0`; entries are 6 bytes `{s8 prog, s8 key, s8 flag, s8 port, u16 seNo (filled at start-up by GetSeNo)}`; prog -1 = no sound; port -1 means 14).
Tables live in the executable's data segment (`snd.exe_tables()` reads them): `se_info` 0x25DFB0 (2,800 entries), `basic_se_info` 0x2A1ED8 (2 tables of 198), `cap_se_info` 0x265BA0 (101 slots,
47 used), `voice_info` 0x266580 (7 voices), `special_se_info` 0x2665B0 (64).
| SE id | table | port / bank |
|---|---|---|
| 0..76 (45 used) | `se_info` | 13 (M, prog 122, keys 24..) menu/system, 15 (C, prog 16, keys 20..50) character effects |
| 100..299 | `basic_se_info[TABLE.basic]` (if `TABLE.basic >= 0`) | mostly 14 (I) |
| 300..399 | `cap_se_info[TABLE.cap]` (if that slot exists) | 10 (G, area specific) |
| 400..499 | `voice_info[voice set]` | 11 (S): id 400+j = key 20+j... of program 23+character; see below |
| 500..561 | `se_info` | 14 (I, prog 21, keys 20..): footsteps `500 + 4*surface + (right foot)` (`SndPlayFootSound`) |
| 601..2596 | `se_info` | 10 (G): 1,996 area effects (programs 53..121 of whichever G bank is loaded; each G bank only contains ~19 of the programs, so most ids are silent in a given set) |
Special SE: `SndSPSePlay(n)` -> prog 124, key 19+n on port 12 (Q) with the bank of `special/sp<n>.snd` (exactly one id resolves in each of the 49 non-empty packs that has a table entry; sp44 is non-empty but has no table entry, ids 9 and 35 have empty packs).
Voice tables (id-400 -> key; program = 23 Toan, 24 Xiao, 25 Goro, 26 Ruby, 27 Ungaga, 28 Osmond, 29 "turi"): Toan {0-3: 20-23, 20-23: 40-43, 30-33: 50-53, 40: 60, 50-51: 70-71}; the other characters
use subsets of the same layout (Osmond adds 4,9,10,13,60,61); voice 6 has ids 0..4 -> keys 20..24. Meaning of the groups (attack / hurt / ...) is **[guess]** from usage only.

Validation of the id -> bank mapping: with the port -> bank table of section 6 and each set's `TABLE` numbers, every id that the set's banks can serve resolves to a sample (set 100: 18/18 M-port ids,
52/71 I-port ids, 64 G-port ids; the remaining ids name programs of other areas' banks, as expected for the shared tables). Over all 62 sets 1,143 of 1,852 `basic` and 263 of 507 `cap` lookups resolve;
voice tables: 12/15, 8/11, 9/12, 10/13, 10/13, 11/15, 5/5 entries resolve (3-4 per character do not, presumably unused ids); special: see above.

## 8. Game-side API (what the executable calls) and the native mapping

State kept by the game: `now_bgm_no/now_bgm_play/now_bgm_vol`, `now_sound_set`, `now_voice_set`, `now_sp_no`, `now_amb_no/vol`, `basic_se_table_no`, `se_table_no`, `se_seq[32]`.
Everything is loaded synchronously (`SndXxxLoad(n)`: read the .snd, `SetXxxFile`) or in the background (`SndXxxLoadBG` + `SndSyncBG` polled by `_LOAD_SND_SYNC`).

| game function | what it does | native API (proposal) |
|---|---|---|
| `SndInit` | `CSound::Init`, SPU reverb off, `SndInitialize` | `snd_init()` |
| `SndStep` (every frame, 60 Hz) | BGM fade, SE sequences (`se_seq`), `CSound::Step` (port fades, msin flush) | `snd_step()` |
| `SndBgmLoad(n)` / `SndBgmLoadBG` | `bgm/bgm<n>.snd`; loads bank A + sequences `a`; `SetReverb(core0, cfg)` | `load_bgm(n)` |
| `SndBgmPlay(i)` | `SQ_Play(0,i)`: sequence #i of the loaded list on port 0, port volume = sqtbl vol, `now_bgm_vol = default vol`. All shipped native callers pass i = 0 (verified in the machine code: `por a0,zero,zero` before every `jal SndBgmPlay`; for bgm100..106 that is d01c, d02c, d03a, d04c, d05c, d06b, d07b) except the script op `_PLAY_BGM` | `play_bgm(i = 0)` |
| `SndBgmStop` | `Stop(0)` | `stop_bgm()` |
| `SndBgmRePlay` | `ezMidi(0x00)` restart a paused sequence (state 2) | `resume_bgm()` |
| `SndBgmFadeOutStop` | 10 vsync steps down to 0 then stop | `stop_bgm(fade_frames = 10)` |
| `SndSetBgmVol(v)` (0..127) / `SndSetBgmVolf(f)` | port volume `ezMidi(0xB0, v)`; `f * default vol` | `set_bgm_volume(v)` / `set_bgm_volume_f(f)` |
| `SndBgmFadeIn(frames, target=-1->default, from=0)` / `SndBgmFadeOut(frames, target=0)` | linear per-frame ramp of the BGM volume (quirk: `from` is divided by the default vol in FadeIn) | `bgm_fade_in/out(frames, ...)` |
| `SndAmbientPlay(i)` / `Stop` / `SetVol[f]` | `SQ_Play(1,i)`: E sequence #i (village: time of day 0..3; dungeon: i = 0 = the battle layer `m01a_e.sq`, volume 140); the dungeon cross-fades BGM vs. ambient with `BtBattleMusic_Excg` when a monster is within 100..110 units | `play_ambient(i)`, `stop_ambient()`, `set_ambient_volume_f(f)` |
| `SndSoundLoad(n)` / `LoadBG` | `set/snd<n>.snd`: banks C E I G M, E sequences, `TABLE`, `SetReverb(core1)`, port volumes 0xF,0xE,0xA,0xD,0xC = 0x100, stops all SE | `load_sound_set(n)` |
| `SndVoiceLoad(c)` | `voice/voice<c>.snd`, S bank on port 11 (called with the character id when entering a dungeon, and by the menus) | `load_voice(c)` |
| `SndSePlay(id, vol=-1, sub=0)` | `GetSeInfo(id)`, program change, volume = setbl vol (vol < 0) | `play_se(id)` |
| `SndSePlay(id, volf, panf)` | volume = `clamp(setbl vol * volf, 0, 127)`, pan = `int(clamp(panf,-1,1) * 63) + 64` | `play_se(id, volf, panf)` |
| `SndSePlay(id, pos, near=20, far=500)` | `SndGetVolPan`: volf = `1 - (d-near)/(far-near)` clamped to [0,1]; pan = 0.7 * sign * `|dot(dir_to_source_flat, camera_right_flat)|^4`; volf *= `(0.7 + 0.3|pan|) * 1.4` | `play_se_at(id, pos, near, far)` |
| `SndSeStop(id)` | `SE_Stop` (note-off key/id on the port) | `stop_se(id)` |
| `SndSetSeVol[f]`, `SndSetSePanf` | retarget a running SE | `set_se_volume/pan` |
| `SndSeSeqPlayStop(id, frames, sub)` | adds {id, duration, counter} to a 32-slot list; `SndSeSeqStep` plays at counter 0 and stops after `frames` | `play_se_timed(id, frames)` |
| `SndStopAllSe`, `SndSeSeqAllStop` | `Stop(0xF,1,0xE,0xA,0xD,0xC,0xB)`, `StopVoice` | `stop_all_se()` |
| `SndSPSeLoad(n)`/`SndSPSePlay(i)`/`Stop`/`SetSPSeVolf/Panf` | `special/sp<n>.snd`, Q bank on port 12; SE = special_se_info[i] (prog 124, key 19+i) | `load_special(n)`, `play_special(i)`... |
| `SndPlayFootSound(surface, foot, pos)` | `SndSePlay(500 + 4*surface + (foot>0), pos, near 50, far 300)` | `play_se_at` |
| `SndSetCamera` | stores the camera position/direction used by the 3-D pan | `set_listener(pos, dir)` |
| script ops (`CRunScript`) | `_LOAD_BGM,_PLAY_BGM,_STOP_BGM,_DELETE_BGM,_SET_BGM_VOL[F],_BGM_FADE_IN/OUT,_CHECK_BGM_FADE,_PLAY_AMB_BGM,_STOP_AMB_BGM,_SET_AMB_VOLF,_LOAD_SOUND_SET,_LOAD_VOICE_SET,_PLAY_SE(id[,vol] | id,pos,near,far),_STOP_SE,_SET_SE_VOL,_LOAD_SPECIAL_SE,_PLAY_SPECIAL_SE,_STOP_SPECIAL_SE,_SET_SPECIAL_SE_VOL,_LOAD_SND_SYNC` | thin wrappers |

Voice request from game logic: `voice(character, n)` = `load_voice(character)` once, then `play_se(400 + n)` (the table of the loaded voice set). The sound set to load for a map is the map's `SOUND_SET`,
the music its `BGM_NO` (`data/gedit/*/mapinfo.cfg`, section 9); dungeon `selectMapNo` (0..6): `BGM 100+n`, `SOUND_SET 100+n`; SE ids 100..399 depend on the set's `TABLE`.
Units: BGM volume 0..127 (default = sqtbl vol, can exceed 127), ambient 0..127, SE volume 0..127, pan 0..127 (64 centre) in hsyn, ±1 in the game API.

## 9. What file holds what

Areas (mapinfo.cfg `BGM_NO` / `SOUND_SET`, comments are SJIS): e01 Norune: BGM 1 (ノルン村テーマ), set 0; e02 Matataki: 2/1; e03 Queens: 3/2; e04 Muska Lacka: 4/3; e05 Factory: 14/4; s04 Brownboo: 6/14;
s09/s25/s33 East Harbor: BGM 3 (Queens theme) or 102 (sunken ship), set 19; s13 Yellow Drop: 15/23; s26 temple of sun and moon: 103/3; s27 moon sea: 104/23; s28 and s30 Dark Heaven Castle: 30/56 and 30/40; s31 robots: -/41; s98 Dran fight: set 200; s99 divine-beast cave: set 100.
Bank-name prefixes (from the mapinfo comments, rest **[guess]**): e0N towns, o0N other places/ending, s?? story scenes, v?? event scenes (opening/cut scenes), d0N dungeons, b0N bosses, m monster/battle layer, t title, sysa system.

BGM files (sequence order = load order; volume = sqtbl; seconds = one pass incl. tail):

| BGM no | bank (.hd/.bd) | sequences in load order (index = SndBgmPlay arg) with sqtbl volume | seconds one pass | REVERBE (mode,depth) |
|---|---|---|---|---|
| 0 | o04c_a.hd | o04c_a.sq (101) | 113 | (4, 30) |
| 1 | e01a_a.hd | e01a_a.sq (102) | 55 | (4, 15) |
| 2 | e02a_a.hd | e02a_a.sq (56) | 55 | (4, 35) |
| 3 | e03a_a.hd | e03a_a.sq (66) | 75 | (4, 20) |
| 4 | e04a_a.hd | e04a_a.sq (60) | 87 | (4, 20) |
| 5 | o04a_a.hd | o04b_a.sq (108) | 50 | (4, 30) |
| 6 | s04a_a.hd | s04a_a.sq (50) | 76 | (4, 45) |
| 7 | v03a_a.hd | v03a_a.sq (66) | 52 | (4, 55) |
| 8 | o01a_a.hd | o01a_a.sq (85) | 183 | (4, 80) |
| 9 | o01b_a.hd | o01b_a.sq (107) | 110 | (4, 50) |
| 10 | o03a_a.hd | o03a_a.sq (85) | 141 | (3, 20) |
| 11 | o02a_a.hd | o02a_a.sq (58) | 58 | (4, 30) |
| 12 | o03b_a.hd | o03b_a.sq (79) | 55 | (4, 50) |
| 13 | o04a_a.hd | o04a_a.sq (111) | 50 | (4, 50) |
| 14 | e05a_a.hd | e05a_a.sq (116) | 146 | (4, 40) |
| 15 | s13a_a.hd | s13a_a.sq (118) | 101 | (4, 45) |
| 16 | v04a_a.hd | v04a_a.sq (63) | 61 | (4, 35) |
| 17 | v05a_a.hd | v05a_a.sq (89) | 51 | (3, 30) |
| 18 | v01a_a.hd | v01a_a.sq (88) | 79 | (4, 30) |
| 19 | v06a_a.hd | v06a_a.sq (82) | 90 | (4, 55) |
| 20 | v07a_a.hd | v07a_a.sq (217) | 31 | (4, 30) |
| 21 | v08a_a.hd | v08a_a.sq (85) | 64 | (4, 45) |
| 22 | v09a_a.hd | v09a_a.sq (140) | 69 | (4, 45) |
| 23 | v10a_a.hd | v10a_a.sq (106) | 102 | (4, 55) |
| 24 | v11a_a.hd | v11a_a.sq (112) | 53 | (4, 45) |
| 25 | v02a_a.hd | v02a_a.sq (120) | 90 | (4, 40) |
| 26 | v12a_a.hd | v12a_a.sq (80) | 46 | (4, 40) |
| 27 | v13a_a.hd | v13a_a.sq (66) | 133 | (4, 60) |
| 28 | v14a_a.hd | v14a_a.sq (81) | 118 | (4, 50) |
| 29 | v15a_a.hd | v15a_a.sq (124) | 147 | (4, 35) |
| 30 | s30a_a.hd | s30a_a.sq (109) | 68 | (4, 55) |
| 31 | v11a_a.hd | v11b_a.sq (112) | 26 | (4, 45) |
| 32 | v16a_a.hd | v16a_a.sq (78) | 46 | (4, 40) |
| 33 | v17a_a.hd | v17a_a.sq (81) | 89 | (4, 35) |
| 34 | v18a_a.hd | v18a_a.sq (136) | 100 | (4, 25) |
| 35 | v19a_a.hd | v19a_a.sq (186) | 102 | (4, 50) |
| 36 | s50a_a.hd | s50a_a.sq (6) | 19 | (4, 50) |
| 100 | d01a_a.hd | d01c_a.sq (80), d01a_a.sq (80), d01b_a.sq (80) | 63 | (4, 40) |
| 101 | d02a_a.hd | d02c_a.sq (93), d02a_a.sq (93), d02b_a.sq (93) | 113 | (4, 40) |
| 102 | d03a_a.hd | d03a_a.sq (88), d03b_a.sq (88), d03c_a.sq (88) | 61 | (4, 40) |
| 103 | d04a_a.hd | d04c_a.sq (100), d04a_a.sq (100), d04b_a.sq (100) | 110 | (4, 40) |
| 104 | d05a_a.hd | d05c_a.sq (76), d05a_a.sq (76), d05b_a.sq (76) | 69 | (4, 40) |
| 105 | d06a_a.hd | d06b_a.sq (110), d06a_a.sq (110), d06c_a.sq (110) | 93 | (4, 40) |
| 106 | d07a_a.hd | d07b_a.sq (95), d07a_a.sq (95), d07c_a.sq (95) | 74 | (4, 40) |
| 110 | d02a_a.hd | d02c_a.sq (93) | 113 | none |
| 120 | d03a_a.hd |  | - | none |
| 130 | d04a_a.hd |  | - | none |
| 150 | b01a_a.hd | b01a_a.sq (94) | 82 | (4, 50) |
| 151 | b02a_a.hd | b02a_a.sq (122) | 53 | (4, 30) |
| 152 | b03a_a.hd | b03a_a.sq (88) | 56 | (4, 50) |
| 153 | b04a_a.hd | b04a_a.sq (114) | 125 | (4, 50) |
| 154 | b05a_a.hd | b05a_a.sq (93) | 82 | (4, 40) |
| 155 | b06a_a.hd | b06a_a.sq (66) | 46 | (4, 50) |
| 156 | b06b_a.hd | b06b_a.sq (124) | 95 | (4, 50) |
| 157 | b07a_a.hd | b07a_a.sq (78) | 59 | (4, 55) |
| 200 | m01a_e.hd | m01a_e.sq (140) | 43 | (4, 0) |
| 201 | b01a_a.hd | b01a_a.sq (94) | 82 | (4, 60) |

Sound sets (`snd<N>.snd`; letters = bank names):

| SOUND_SET no | C (port 15) | E (port 1) | I (ports 2,14) | G (port 10) | M (port 13) | E sequences | TABLE (basic,cap) | REVERBE |
|---|---|---|---|---|---|---|---|---|
| 0 | e01a_c | e01a_e | e01a_i | e01a_g | sysa_m | e01a_e,e01b_e,e01c_e,e01d_e | (0, 0) | (2, 3) |
| 1 | e02a_c | e02a_e | e01a_i | e02a_g | sysa_m | e02a_e,e02b_e,e02c_e,e02d_e | (0, 1) | (2, 3) |
| 2 | e03a_c | e03a_e | e01a_i | e03a_g | sysa_m | e03a_e,e03b_e,e03c_e,e03d_e | (0, 2) | (2, 3) |
| 3 | e04a_c | e04a_e | e01a_i | e04a_g | sysa_m | e04a_e,e04b_e,e04c_e,e04d_e | (0, 3) | (2, 3) |
| 4 | e05a_c | - | e01a_i | e05a_g | sysa_m | - | (0, 4) | (4, 10) |
| 13 | - | - | e01a_i | e01a_g | sysa_m | - | (0, 10) | (4, 40) |
| 14 | - | s04a_e | e01a_i | s04a_g | sysa_m | s04a_e,s04a_e | (0, 10) | (4, 15) |
| 19 | s09a_c | e03a_e | e01a_i | e03a_g | sysa_m | s09a_e,s09b_e,s09c_e,s09d_e | (0, 11) | (2, 3) |
| 21 | e05a_c | - | e01a_i | s11a_g | sysa_m | - | (0, 12) | (4, 10) |
| 22 | e04a_c | e04a_e | s24a_i | s12a_g | sysa_m | e04a_e,e04b_e,e04c_e,e04d_e | (0, 13) | (2, 3) |
| 23 | e05a_c | - | e01a_i | e05a_g | sysa_m | - | (0, 14) | (4, 10) |
| 26 | s16a_c | - | s24a_i | s16a_g | sysa_m | - | (1, 50) | (4, 3) |
| 31 | s30a_c | s30a_e | e01a_i | s30a_g | sysa_m | s30a_e | (0, 13) | (4, 15) |
| 32 | e05a_c | - | e01a_i | s11a_g | sysa_m | - | (0, 14) | (4, 15) |
| 34 | - | - | s24a_i | s24a_g | sysa_m | - | (1, 15) | (4, 10) |
| 38 | d06a_c | - | d01a_i | s28a_g | sysa_m | - | (0, 51) | (4, 15) |
| 40 | s30a_c | s30a_e | s24a_i | s30a_g | sysa_m | s30a_e | (1, 52) | (4, 5) |
| 41 | e05a_c | - | - | s31a_g | sysa_m | - | (0, 17) | (4, 10) |
| 49 | s16a_c | - | s24a_i | s39a_g | sysa_m | - | (1, 53) | (4, 5) |
| 50 | s30a_c | s30a_e | s24a_i | s40a_g | sysa_m | s30a_e | (1, 54) | (4, 5) |
| 51 | s30a_c | s30a_e | s24a_i | s40b_g | sysa_m | s30a_e | (1, 55) | (4, 3) |
| 52 | s30a_c | s30a_e | s24a_i | s40c_g | sysa_m | s30a_e | (1, 56) | (4, 5) |
| 53 | s30a_c | s30a_e | s24a_i | s40d_g | sysa_m | s30a_e | (1, 57) | (4, 5) |
| 54 | e01a_c | e01a_e | s24a_i | - | sysa_m | e01d_e | (0, 0) | (2, 3) |
| 55 | s30a_c | s30a_e | e01a_i | - | sysa_m | s30a_e | (0, 0) | (4, 5) |
| 56 | d06a_c | - | e01a_i | - | sysa_m | - | (0, 0) | (4, 15) |
| 57 | s50a_c | s50a_e | e01a_i | - | sysa_m | s50a_e,s50a_e | (0, 0) | (4, 5) |
| 100 | d01a_c | m01a_e | d01a_i | d01a_g | sysa_m | m01a_e | (1, 20) | (4, 15) |
| 101 | d02a_c | m01a_e | d01a_i | d02a_g | sysa_m | m01a_e | (1, 25) | (4, 5) |
| 102 | d03a_c | m01a_e | d01a_i | d03a_g | sysa_m | m01a_e | (1, 30) | (4, 10) |
| 103 | d04a_c | m01a_e | d01a_i | d04a_g | sysa_m | m01a_e | (1, 35) | (4, 15) |
| 104 | d05a_c | m01a_e | d01a_i | d05a_g | sysa_m | m01a_e | (1, 40) | (4, 5) |
| 105 | d06a_c | m01a_e | d01a_i | d06a_g | sysa_m | m01a_e | (1, 45) | (4, 10) |
| 106 | d07a_c | m01a_e | d01a_i | d07a_g | sysa_m | m01a_e | (1, 59) | (4, 10) |
| 107 | d07a_c | m01a_e | d01a_i | d07b_g | sysa_m | m01a_e | (1, 60) | (4, 10) |
| 108 | d07a_c | m01a_e | d01a_i | d07c_g | sysa_m | m01a_e | (1, 61) | (4, 10) |
| 109 | d07a_c | m01a_e | d01a_i | d07d_g | sysa_m | m01a_e | (1, 62) | (4, 10) |
| 110 | d07a_c | m01a_e | d01a_i | d07e_g | sysa_m | m01a_e | (1, 63) | (4, 10) |
| 200 | d01a_c | m01a_e | d01a_i | d01b_g | sysa_m | m01a_e | (1, 21) | (4, 15) |
| 201 | - | m01a_e | d01a_i | d02b_g | sysa_m | m01a_e | (1, 26) | (4, 5) |
| 202 | - | m01a_e | d01a_i | d03b_g | sysa_m | m01a_e | (1, 31) | (4, 10) |
| 203 | d04a_c | m01a_e | d01a_i | d04b_g | sysa_m | m01a_e | (1, 36) | (4, 15) |
| 204 | d05a_c | m01a_e | d01a_i | d05b_g | sysa_m | m01a_e | (1, 41) | (4, 5) |
| 205 | - | m01a_e | d01a_i | d06b_g | sysa_m | m01a_e | (1, 46) | (4, 10) |
| 206 | - | m01a_e | d01a_i | d06c_g | sysa_m | m01a_e | (1, 64) | (4, 10) |
| 207 | - | m01a_e | d01a_i | d07f_g | sysa_m | m01a_e | (1, 65) | (4, 10) |
| 300 | t01a_c | t01a_e | t01a_i | - | sysa_m | t01a_e | (0, 0) | (2, 3) |
| 301 | o01a_c | - | o01a_i | - | sysa_m | - | (0, 0) | (4, 30) |
| 302 | o01a_c | - | o01a_i | - | sysa_m | - | (0, 0) | (4, 30) |
| 303 | e01a_c | e01a_e | o02a_i | - | sysa_m | e01d_e | (0, 0) | (2, 3) |
| 304 | e01a_c | e01a_e | o02a_i | - | sysa_m | e01d_e | (0, 0) | (2, 3) |
| 305 | o03b_c | o03b_e | o03b_i | - | sysa_m | o03b_e | (0, 0) | (2, 3) |
| 306 | e01a_c | o04a_e | o04a_i | - | sysa_m | o04a_e | (0, 0) | (2, 3) |
| 307 | e01a_c | e01a_e | e01a_i | e01a_g | sysa_m | e01a_e,e01b_e,e01c_e,e01d_e | (0, 0) | (2, 3) |
| 308 | e01a_c | e01a_e | e01a_i | e01a_g | sysa_m | e01a_e,e01b_e,e01c_e,e01d_e | (0, 0) | (2, 3) |
| 309 | s30a_c | s30a_e | o05a_i | o05a_g | sysa_m | s30a_e | (1, 58) | (4, 3) |
| 400 | d01a_c | m01a_e | d01a_i | d01c_g | sysa_m | m01a_e | (1, 22) | (4, 15) |
| 401 | d02a_c | m01a_e | d01a_i | d02c_g | sysa_m | m01a_e | (1, 27) | (4, 5) |
| 402 | d01a_c | m01a_e | d01a_i | d02c_g | sysa_m | m01a_e | (1, 32) | (4, 40) |
| 403 | d01a_c | m01a_e | d01a_i | d02c_g | sysa_m | m01a_e | (1, 37) | (4, 40) |
| 404 | d01a_c | m01a_e | d01a_i | d02c_g | sysa_m | m01a_e | (1, 42) | (4, 40) |
| 405 | d01a_c | m01a_e | d01a_i | d02c_g | sysa_m | m01a_e | (1, 47) | (4, 40) |

(* = empty placeholder bank, 0 bytes)

Special SE: `special/sp<N>.snd` (N = 1..62) holds one bank `spNN_q` (1 program, 124) -> SE special id N.
Voice: `voice/voice<c>.snd`: toan_s, syao_s, goro_s, ruby_s, unga_s, ozmd_s, turi_s.

## 10. Reference synthesizer (`tools/dc/snd.py`)

`python tools/dc/snd.py list|info|programs|seq|wav|render-bgm|render-se|render-amb|render-special|verify` (use `python -I` when reading untrusted data; the module has no dependency except numpy).
* `Bank`: `.prog/.sset/.smpl/.vagi`, `resolve(prog,key,vel)`, `pcm(vag)`, `describe()` (all programs, splits with key ranges, tones with ADSR, base key, pan, volume, rate).
* `Song`: `events` (note/cc/pc/bend/tempo), `flatten()` resolves the loop controllers (endless loops followed `extra_loops` times).
* `Synth`: numpy mixer (48 kHz stereo): linear-interpolated sample playback with loop, pitch ratio incl. bend (per-split range) and CC automation sampled at sample accuracy, SPU2 ADSR,
  volume chain of section 6, linear pan law incl. key-follow pan, sustain pedal, optional Schroeder reverb with send = cfg depth/128 (not the SPU2 reverb algorithm), output scaled down only if the peak > 0.98.
* `SoundModel`: `load_sound_set/load_voice/load_special/load_bgm`, `se_info(id)`, `se_volume(id, volf)`, `render_se(id, volf, panf)`, `render_bgm(i, kind)`.

Test renders (`work/snd_test/`, all 48 kHz stereo 16-bit; no NaN, no clipping, DC offset < 2.4e-3):
| file | seconds | peak | notes |
|---|---|---|---|
| `bgm1_norune.wav` (`snd.py render-bgm 1`) | 98.53 | 0.545 | Norune theme: intro 0..7.79 s, loop body 7.79..50.99 s played twice, tail to 55.2 s (source: 100 bpm, 733 notes, reverb STUDIO_C/15, port volume 102/256) |
| `bgm100_dungeon1.wav` | 118.30 | 0.735 | dungeon 1 (d01c_a.sq), loop 2.56..57.42 s of 62.56 s |
| `bgm150_boss1.wav` | 123.35 | 0.980 | boss 1 (4,157 notes); 1.23 before the automatic -2 dB scale (only when the peak exceeds 0.98); loop 32.05..74.79 s |
| `set100_ambient0_m01a_e.wav` (`render-amb 100 0`) | 70.78 | 0.707 | dungeon "battle layer" sequence, loop 8.90..38.90 s, port volume 140 |
| `set0_ambient0_e01a_e.wav` | 66.53 | 0.088 | Norune morning ambience (volume 24), loop 4.04..33.70 s |
| `se_set100_id{0,1,101,150,206,296,500,1133}.wav` (`render-se 100 ID`) | 0.05 / 0.08 / 0.74 / 0.18 / 0.53 / 2.07 / 0.30 / 1.89 | 0.77 / 0.52 / 0.66 / 0.40 / 0.36 / 0.61 / 0.35 / 0.54 | menu blips (ids 0,1; M bank), I-bank effects (101..500; 500 = footstep), G-bank area effect (1133); volumes from setbl.txt |
| `voice0_se400.wav` (`render-se 100 400 --voice 0`) | 0.22 | 0.535 | Toan voice id 400 (program 23 key 20) |
| `special_sp1.wav`, `special_sp47.wav` (`render-special N`) | 3.17 / 6.07 | 0.69 / 0.52 | special SE 1 and 47 (one-shot ADPCM played to its end) |
| `samples_bgm1/`, `samples_sp1/`, `samples_voice0/` (`snd.py wav`) | | | raw mono samples at the Vagi rates (e.g. 44,091 Hz) |
Sanity beyond levels: the first note of bgm1 (key 36, C2) renders with spectral peaks at 130.4 / 261.5 Hz = harmonics 2 and 4 of 65.4 Hz; loop points above are the CC99/CC6/CC38 markers converted with the tempo map.
The rendering is "reference quality": no SPU2 reverb emulation, no voice stealing, LFO/pitch-EG ignored (all zero in the data), velocity curve 1 and 3..10 ignored (never used).

## 11. Self-check summary (`python tools/dc/verify_snd.py --decode`, 19 s)

217 `.snd` packs (bgm 57, set 62, sound_set 29, special 62, voice 7) + 30 loose battle files: every pack parses and its entries account for every byte (0x50-byte tail);
227 distinct banks (561 entries; 334 byte-identical duplicates skipped) parse with consistent HD/BD size fields and valid split->sset->smpl->vag references; all 2,871 VAGs have 16-byte aligned
offsets inside the BD, reach their END block, and decode; vagi loop bit == ADPCM repeat bit for all; 107 distinct sequences (217 files) parse to FF 2F with only padding after it
(94,721 note-ons = 94,721 note-offs, 30,353 CC, 12,030 bend, 494 program changes, 132 tempo events, 90 endless loops); 98,145 note-ons checked against their banks (15 without sample).
Only known data defects reported as warnings: `bgm120/bgm130.txt` name sequences that are not in the pack; `bgm110/120/130` lack a REVERBE line; sound_set/snd106 is empty.

## 12. Open questions / guesses

1. **Velocity curves** 1 and 3..10 (`PTR_FUN_0000a7f8`: 0x86bc, 0x8710, 0x8738, 0x876c ...) and the custom velocity tables (Sset curve high nibble): unused in the data; only 0 and 2 decoded.
2. **LFO / pitch envelope / key-follow of ADSR / velocity-follow** (Prog header 12..35, Smpl 5..10, 22..40, `FUN_00008b64`, `FUN_000042a4`, `FUN_00003594/638`): all inert in the shipped data; not synthesized.
3. **Exclusive groups** (Smpl byte 14, `FUN_00006900`), voice limits and stealing (`FUN_00005e0c`, port attr `ezMidi(0xA0+n, 0x30xx)`): semantics inferred, not tested; matters for rapid SE retriggering (hi-hat style choke in sysa_m).
4. **Reverb**: `SetReverb(core, mode, depth)` only configures the SPU2 hardware reverb (STUDIO_A/C presets); the exact algorithm/levels are the SPU2's, my reverb is a stand-in. mapinfo.cfg's own `REVERBE` line overrides the cfg per map: code not read (`EdSetReverb`?).
5. **Voice id meaning** (400+j groups), special SE names, and what the BGM numbers 0, 5, 7..13, 16..29, 31..36, 150..157, 201 play in the story: only bank names are known; names must come from the scripts (`.stb`) calling `_LOAD_BGM/_PLAY_BGM`.
6. **Why the other two sequences of bgm10N exist** (index 1, 2 of `SndBgmPlay`): no native caller passes 1 or 2 (only a `_PLAY_BGM` script could; the .stb scripts were not searched).
7. `SndBgmFadeIn` quirk (`from` divided by default volume while `target` is absolute), `Fade__6CSoundFifi` (per-port linear fades, never called), `ezMidi(0xC0)` argument mapping (stereo = 1?), `ezMidi(0x30+n)` seek argument: from the call sites only.
8. The 3-D pan sign convention of `SndGetVolPan` (right vector = (dz, 0, -dx) of the camera direction, pan = -dot): copied as decompiled, not checked against a camera orientation.
9. Port volume of ambient/E vs BGM mixing during `BtBattleMusic_Excg` crossfade, and the SPU2 master volume (0x3fff): levels of the renders are consistent with the formula but not compared with a recording of the real game.
10. Sony `SCEIVers` byte 0xC..0xD (always 0), `Head` trailing words, Sset `n > 1` (layers within one sample set) and `Sess` chunk: never present in the data.

## 13. C++ port (native/dccore, 2026-10-19)

Pure C++20, no Godot/audio-API dependency, part of the `dccore` static library. Files:
* `include/dccore/sound.h` - everything public: `Pcm`/`adpcm_decode`, `Bank` (HD/BD parse, `resolve(prog,key,vel)`, all VAGs ADPCM-decoded at load), `Song` (compact `.sq` parser), `SndPack` (`.snd` = `Pack` + load list + `.cfg`),
  `SndTables` (SE tables read from the executable via `dccore/elf.h` at the addresses of section 7, `setbl.txt`/`sqtbl.txt` from the disc), `SoundSystem`.
* `src/sound_bank.cpp` - formats and tables (snd.py sections 1-4, 7). `src/sound.cpp` - engine: sequencer, voices, SPU2 ADSR state machine, reverb, command queue, `SoundSystem` (snd.py sections 5-7b).
* `tools/dcsound.cpp` (target `dcsound`), `tools/dc/verify_snd_cpp.py` (runs snd.py and dcsound side by side).

**API** (`SoundSystem(const Disc&)`; all commands are thread-safe, queued, applied at the start of the next `render`; loading is synchronous in the calling thread):
`render(float* stereo, int frames)` (48 kHz interleaved, overwrites; audio thread, no allocation, commands taken with `try_lock`);
music `load_bgm(n)`, `play_bgm(i=0)`, `stop_bgm(fade_frames=0)`, `pause_bgm/resume_bgm`, `set_bgm_volume(v)`, `set_bgm_volume_f(f)`, `bgm_fade_in(frames,target=-1,from=0)`, `bgm_fade_out(frames,target=0)` (the game's per-frame 60 Hz fade
with its quirks), `set_bgm_loop_limit(n)` (-1 = endless like the game); sets `load_sound_set(n)`, `play_ambient(i)`, `stop_ambient()`, `set_ambient_volume[_f]`;
SE `play_se(id, volf=-1, panf=0)`, `play_se_at(id, pos, near=20, far=500)` + `set_listener(pos, dir)` (SndGetVolPan maths of section 8), `play_se_timed(id, frames)`, `stop_se(id)`, `stop_se_handle(h)`, `stop_all_se()`, `set_se_volume/pan`,
queries `se_info(id)`, `se_volume(id)`, `se_is_looping(id)`; `load_voice(c)` (ids 400..499), `load_special(n)`/`play_special(i)`/`stop_special()`;
mixer `set_master_volume`, `set_stereo`, `set_reverb_enabled`, `set_voice_limit(core0, core1)` (default 24 per SPU2 core), `set_limiter`; state `active_voices()`, `peak_voices()`, `missing_notes()`, `bgm_playing()`, `bgm_position()`.

**Exact vs approximate.** Exact (same maths as snd.py): file parsing, PS-ADPCM, program/split/velocity resolution, pitch (base key, rate, transpose/detune, per-split bend range, 4x clamp), ADSR1/ADSR2 envelopes (the nocash SPU2 model as a per-sample state machine,
stage by stage identical to `adsr_envelope`; exponential sustain-increase is linear as in snd.py), volume chain, velocity curves 0/2, linear pan law incl. key-follow pan, CC7/10/11/64/32/99/6/38, sequence timing and loop controllers, one-shot vs looping samples, FIFO note-off pairing,
SE/special/voice id -> (program,key,port) and setbl volumes, fades. Approximate / my additions: **reverb** (4 feedback combs per side with the delays/gains of snd.py; default engine mode adds a mode 2/3/4 preset scaling, two allpasses and returns only the wet tail, per-voice sends from
Smpl byte 41 bits 2/3 into the core-0 (BGM cfg) / core-1 (set cfg) reverb with depth/128 return; none of it is the SPU2 reverb), **voice stealing** (24 voices per core, lowest `smpl.priority + port priority`, then released voices, then oldest; port priorities are the guessed low bytes of section 6),
soft limiter (above 0.9) instead of snd.py's offline peak scaling, linear-interpolated sample wrap uses the loop start (snd.py had a bug here, fixed together with the port: the `np.where` for the loop wrap was dead code and caused a click per loop pass), exclusive groups / LFO / key-follow ADSR not implemented (inert in the data).

**How to check.** `powershell -NoProfile -ExecutionPolicy Bypass -File native/build_core.ps1`, then
`dcsound <iso> bgm <n> <seconds|0> <out.wav> [--seq i] [--loops N]`, `dcsound <iso> amb <set> <idx> <seconds|0> <out.wav>`, `dcsound <iso> se <set> <id> <out.wav> [--voice c] [--special n] [--hold s] [--reverb]`, `dcsound <iso> special <n> <out.wav>`,
`dcsound compare ref.wav test.wav [seconds]` (RMS difference, correlation, worst seconds), `dcsound <iso> stress [seconds]` (audio thread + command thread smoke test). The default mode of dcsound is the snd.py-equivalent reference mode (all voices feed the stand-in reverb, no diffusion/presets, unlimited voices,
no limiter, snd.py's peak scaling when the peak exceeds 0.98); `--game` renders with the engine defaults. `python tools/dc/verify_snd_cpp.py [--quick]` renders both and prints the comparison (35 s).

Results (reference mode, whole files; ref = snd.py, int16 quantisation floor is about -82 dB for these levels): bgm1 -71.9 dB (0.026 % of the reference RMS, correlation 1.000000), bgm100 -83.0 dB, bgm150 -77.2 dB, ambient set100/0 -71.9 dB, set0/0 -86.8 dB; all 8 SEs, voice0 id 400 and specials 1/47 -108..-115 dB
(identical to quantisation; looping SE 53/61 too). The residual of bgm1 is one 66 ms window at 82 s that starts exactly at a pitch-bend event (most likely the one-sample difference between snd.py's float time axis and the sample-exact event time here). Rendering in chunks of 64, 777 or 1024 frames gives bit-identical output.
With the hardware voice limit (24/core) bgm1/bgm100 are unchanged (-72/-83 dB), bgm150 (38 voices wanted) differs by -28 dB; `--game` differs from snd.py only through the reverb stand-in. All 57 BGM packs render with no missing notes except bgm2 (the 5 known notes of program 6), 200-400x real time on one core.
