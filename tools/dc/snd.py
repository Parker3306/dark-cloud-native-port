#!/usr/bin/env python
"""Dark Cloud (PS2, SCUS-97111) sound data: container parsers, PS-ADPCM decoder and a reference software synthesizer.

Everything here was derived from the game's own code (CSound / Snd* functions in work/decomp/all_dun.c) and from the IOP
modules EZMIDI.IRX / MODMIDI.IRX / MODHSYN.IRX (Sony libmidi + libhsyn); see notes/sound.md for the format description.

Layers
  1. `.snd` files = the game's generic pack (0x50-byte entry headers) holding Sony `.hd` (bank header), `.bd` (bank body,
     raw PS-ADPCM), `.sq` (sequence), `.txt` (load list) and `.cfg` (reverb / table numbers) files.
  2. `Bank`   = parsed `.hd` ("SCEIVers/Head/Prog/Sset/Smpl/Vagi" chunks) + `.bd`; programs -> splits (key ranges) ->
     sample sets (velocity ranges) -> samples (ADSR, base key, pan, volume) -> VAG info (offset, sample rate).
  3. `Song`   = parsed `.sq` ("SCEIVers/Sequ/Midi" chunks): a Format-0 MIDI stream with Sony's compact event coding.
  4. `Synth`  = numpy software mixer (sample playback, pitch bend, SPU2-style ADSR, volume, pan, loops, optional reverb).

Command line (python tools/dc/snd.py -h):  list | info | wav | programs | seq | render-bgm | render-se | render-amb | render-special | verify
"""
import argparse
import hashlib
import math
import os
import re
import struct
import sys
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

ROOT = Path(__file__).resolve().parent.parent.parent
SOUND = ROOT / "data" / "sound"
OUT_RATE = 48000            # SPU2 native rate; a VAG "sample rate" of R plays at R/48000 source samples per output sample


# ----------------------------------------------------------------------------------------------------------------
# 1. containers
# ----------------------------------------------------------------------------------------------------------------
def parse_pack(buf):
    """Game pack: entries of 0x50-byte header {name[0x40], u32 data_off(0x50), u32 size, u32 stride, u32 extra} + data.
    The list ends at an entry whose first name byte is 0 (or at the end of the buffer).  Returns (entries, tail) where an
    entry is (name, data, extra).  Raises ValueError on anything inconsistent."""
    p, out = 0, []
    while p + 0x50 <= len(buf) and buf[p] != 0:
        name = bytes(buf[p:p + 0x40]).split(b"\0")[0].decode("latin1")
        off, size, stride, extra = struct.unpack_from("<4I", buf, p + 0x40)
        if off < 0x50 or stride < off + size or p + off + size > len(buf):
            raise ValueError("bad pack entry at 0x%x" % p)
        out.append((name, bytes(buf[p + off:p + off + size]), extra))
        if stride == 0:
            break
        p += stride
    return out, bytes(buf[p:])


class SndFile:
    """One `.snd` pack.  `.files` = ordered {name: bytes} (the first entry of a given name wins in `get`, like the game's
    GetPackFile; duplicates are kept in `.entries`).  `.txt` = lines of the load list, `.cfg` = {'REVERBE': (mode, depth),
    'TABLE': (basic, cap)}."""

    def __init__(self, path):
        self.path = Path(path)
        raw = self.path.read_bytes()
        self.size = len(raw)
        self.entries, self.tail = parse_pack(raw)
        self.names = [e[0] for e in self.entries]
        self.files = {}
        for n, d, _ in self.entries:
            self.files.setdefault(n, d)
        stem = self.path.stem
        txt = self.files.get(stem + ".txt", b"").decode("latin1")
        self.txt = [l.strip() for l in txt.replace("\r", "\n").split("\n") if l.strip()]
        cfg = self.files.get(stem + ".cfg", b"").decode("latin1")
        self.cfg = {}
        for l in cfg.replace("\r", "\n").split("\n"):
            m = re.match(r"\s*(REVERBE|TABLE)\s+(-?\d+)\s*,\s*(-?\d+)", l)
            if m:
                self.cfg[m.group(1)] = (int(m.group(2)), int(m.group(3)))

    def get(self, name):
        return self.files.get(name)

    def banks(self):
        """{letter: Bank} of the banks the load list asks for (the game loads exactly the `.hd` names of the .txt)."""
        out = {}
        for n in self.txt:
            if n.lower().endswith(".hd") and len(n) >= 9:
                hd, bd = self.files.get(n), self.files.get(n[:-3] + ".bd")
                if hd:
                    out[n[5]] = Bank(hd, bd or b"", name=n)
        return out

    def songs(self):
        """[(letter, name, Song)] in load-list order (the game numbers sequences per type letter a/e/i in this order)."""
        out = []
        for n in self.txt:
            if n.lower().endswith(".sq") and len(n) >= 9 and n in self.files and self.files[n]:
                out.append((n[5], n, Song(self.files[n], name=n)))
        return out


def tbl_se_info():
    """setbl.txt -> {(program, key): vol}, ordered list of (program, key, vol) (index = the game's SE number 'seNo')."""
    raw = (SOUND / "tbl" / "setbl.txt").read_bytes().decode("latin1")
    rows = []
    for l in raw.replace("\r", "\n").split("\n")[1:]:
        t = l.split()
        if len(t) >= 3 and all(re.fullmatch(r"-?\d+", x) for x in t[:3]):
            rows.append(tuple(int(x) for x in t[:3]))
    return rows


def tbl_sq_vol():
    """sqtbl.txt -> {'e01a_a.sq': 102, ...}"""
    raw = (SOUND / "tbl" / "sqtbl.txt").read_bytes().decode("latin1")
    out = {}
    for l in raw.replace("\r", "\n").split("\n")[1:]:
        t = l.split()
        if len(t) >= 2 and re.fullmatch(r"-?\d+", t[1]):
            out[t[0]] = int(t[1])
    return out


def tbl_snd_list():
    """snd.txt -> names of all .hd and .sq files the sound tools know (CR separated, no trailing newline per name)."""
    raw = (SOUND / "tbl" / "snd.txt").read_bytes().decode("latin1")
    return [x for x in raw.replace("\n", "\r").split("\r") if x]


def snd_path(kind, n):
    """kind: bgm | set | voice | special | sound_set  ->  path of the .snd file for number n (the game's sprintf formats)."""
    sub, stem = {"bgm": ("bgm", "bgm%d"), "set": ("set", "snd%d"), "sound_set": ("sound_set", "snd%d"),
                 "voice": ("voice", "voice%d"), "special": ("special", "sp%d")}[kind]
    return SOUND / sub / ((stem % n) + ".snd")


# ----------------------------------------------------------------------------------------------------------------
# 2. PS-ADPCM
# ----------------------------------------------------------------------------------------------------------------
ADPCM_F = ((0, 0), (60, 0), (115, -52), (98, -55), (122, -60))


def adpcm_decode(data, start=0, max_blocks=None):
    """Decode PS-ADPCM from `start` until the first block carrying the END flag (flags bit0).
    Returns (samples int16 ndarray, info) with info = dict(blocks, loop_start (sample index or None), loop_end_sample,
    looping (bool: final block has the repeat bit), end_flags, truncated (no END flag before the data ended))."""
    n = (len(data) - start) // 16
    if max_blocks is not None:
        n = min(n, max_blocks)
    out = []
    s1 = s2 = 0
    loop_start = None
    looping = False
    truncated = True
    flags = 0
    blocks = 0
    for b in range(n):
        o = start + b * 16
        hdr = data[o]
        flags = data[o + 1]
        shift = hdr & 15
        pred = min(hdr >> 4, 4)
        if shift > 12:
            shift = 9
        f0, f1 = ADPCM_F[pred]
        if flags & 4 and loop_start is None:
            loop_start = b * 28
        for i in range(14):
            v = data[o + 2 + i]
            for nib in (v & 15, v >> 4):
                x = ((nib ^ 8) - 8) << 12 >> shift      # signed 4 bit -> 16 bit (<<12) then >> shift
                x += (s1 * f0 + s2 * f1) >> 6
                if x > 32767:
                    x = 32767
                elif x < -32768:
                    x = -32768
                out.append(x)
                s2 = s1
                s1 = x
        blocks += 1
        if flags & 1:
            truncated = False
            looping = bool(flags & 2)
            break
    arr = np.array(out, dtype=np.int16)
    return arr, dict(blocks=blocks, loop_start=loop_start, looping=looping, end_flags=flags, truncated=truncated)


def write_wav(path, samples, rate, channels=1):
    """samples: int16 ndarray (n,) or (n, channels)."""
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    a = np.asarray(samples)
    if a.dtype != np.int16:
        a = np.clip(np.rint(a * 32767.0), -32768, 32767).astype(np.int16)
    with wave.open(str(path), "wb") as w:
        w.setnchannels(channels)
        w.setsampwidth(2)
        w.setframerate(int(rate))
        w.writeframes(a.tobytes())


# ----------------------------------------------------------------------------------------------------------------
# 3. banks (.hd + .bd)
# ----------------------------------------------------------------------------------------------------------------
def _chunk(d, off, magic):
    """Sony chunk: magic[8] (stored as two byte-reversed words, e.g. 'SCEIVagi' -> b'IECSigaV'), u32 size, u32 maxIndex,
    u32 offsets[maxIndex+1] (relative to the chunk, 0xFFFFFFFF = unused slot).  maxIndex == 0xFFFFFFFF = no entries."""
    if d[off:off + 8] != magic:
        raise ValueError("chunk %r expected at 0x%x, found %r" % (magic, off, d[off:off + 8]))
    size, mx = struct.unpack_from("<II", d, off + 8)
    if off + size > len(d) + 0x40 or size < 16:
        raise ValueError("bad chunk size")
    if mx == 0xFFFFFFFF:
        return size, []
    offs = struct.unpack_from("<%dI" % (mx + 1), d, off + 16)
    return size, [None if o == 0xFFFFFFFF else o for o in offs]


@dataclass
class VagInfo:
    index: int
    offset: int            # byte offset into the .bd
    rate: int              # sample rate in Hz (the u16 at +4)
    flags: int             # byte +6 (bit0 = sample loops)
    extra: int             # byte +7 (always 0xFF)


@dataclass
class Smpl:                # 42 bytes
    index: int
    vag: int               # +0 u16: index into the VAG info table
    vel_lo: int            # +2
    vel_xfade: int         # +3
    vel_hi: int            # +4
    base_key: int          # +11  MIDI key at which the sample plays at its native rate (60 for nearly all)
    fine: int              # +12  signed, 1/128 semitone
    pan: int               # +13  0..127, 64 centre
    group: int             # +14  exclusive group (0 = none)
    priority: int          # +15
    volume: int            # +16  128 = unity
    adsr1: int             # +18 u16 (SPU2 ADSR1: bit15 attack exp, 14..8 attack rate, 7..4 decay rate, 3..0 sustain level)
    adsr2: int             # +20 u16 (SPU2 ADSR2: bit15 sustain exp, 14 sustain dir(1=decrease), 12..6 sustain rate,
    #                                 bit5 release exp, 4..0 release rate)
    spu_flags: int         # +41  bit 4/5 = SPU2 core (0x10 core0, 0x20 core1); low bits = reverb sends
    raw: bytes = b""


@dataclass
class Sset:
    index: int
    vel_curve: int
    vel_lo: int
    vel_hi: int
    smpl: list


@dataclass
class Split:               # 20 bytes
    sset: int
    lo: int
    hi: int
    xfade: int
    number: int
    bend_lo: int           # u16 (units of 1/256 semitone at full scale; 0x600 = 6 semitones)
    bend_hi: int
    vol: int               # 128 = unity
    pan: int               # 64 centre
    transpose: int         # signed semitones
    detune: int            # signed 1/128 semitone
    raw: bytes = b""


@dataclass
class Program:
    index: int
    vol: int               # header +6, 128 = unity
    pan: int               # header +7, 64 centre
    transpose: int         # header +8
    detune: int            # header +9
    pan_key_amount: int    # header +10
    pan_key_center: int    # header +11
    splits: list = field(default_factory=list)
    header: bytes = b""


class Bank:
    """Parsed `.hd` + raw `.bd`."""

    def __init__(self, hd, bd=b"", name=""):
        self.name = name
        self.hd = hd
        self.bd = bd
        self._pcm = {}
        if len(hd) == 0:
            self.empty = True
            self.vagi, self.smpl, self.sset, self.prog = [], {}, {}, {}
            return
        self.empty = False
        if hd[:8] != b"IECSsreV":
            raise ValueError("%s: no SCEIVers" % name)
        self.version = (hd[0xE], hd[0xF])
        if hd[0x10:0x18] != b"IECSdaeH":
            raise ValueError("%s: no SCEIHead" % name)
        hsize, fsize, bsize, o_prog, o_sset, o_smpl, o_vagi = struct.unpack_from("<7I", hd, 0x18)
        self.head_size, self.file_size, self.body_size = hsize, fsize, bsize
        if fsize != len(hd):
            raise ValueError("%s: HD size field %d != %d" % (name, fsize, len(hd)))
        if bd and bsize != len(bd):
            raise ValueError("%s: BD size field %d != %d" % (name, bsize, len(bd)))
        # VAG info
        _, offs = _chunk(hd, o_vagi, b"IECSigaV")
        self.vagi = []
        for i, o in enumerate(offs):
            if o is None:
                self.vagi.append(None)
                continue
            a = o_vagi + o
            off, rate, fl, ex = struct.unpack_from("<IHBB", hd, a)
            self.vagi.append(VagInfo(i, off, rate, fl, ex))
        # samples
        _, offs = _chunk(hd, o_smpl, b"IECSlpmS")
        self.smpl = {}
        for i, o in enumerate(offs):
            if o is None:
                continue
            a = o_smpl + o
            r = hd[a:a + 42]
            if len(r) < 42:
                raise ValueError("%s: short sample record" % name)
            self.smpl[i] = Smpl(i, struct.unpack_from("<H", r, 0)[0], r[2], r[3], r[4], r[11],
                                struct.unpack_from("<b", r, 12)[0], r[13], r[14], r[15], r[16],
                                struct.unpack_from("<H", r, 18)[0], struct.unpack_from("<H", r, 20)[0], r[41], r)
        # sample sets
        _, offs = _chunk(hd, o_sset, b"IECStesS")
        self.sset = {}
        for i, o in enumerate(offs):
            if o is None:
                continue
            a = o_sset + o
            n = hd[a + 3]
            self.sset[i] = Sset(i, hd[a], hd[a + 1], hd[a + 2], list(struct.unpack_from("<%dH" % n, hd, a + 4)))
        # programs
        _, offs = _chunk(hd, o_prog, b"IECSgorP")
        self.prog = {}
        for i, o in enumerate(offs):
            if o is None:
                continue
            a = o_prog + o
            hsz, = struct.unpack_from("<I", hd, a)
            nsplit, bsz = hd[a + 4], hd[a + 5]
            if bsz != 20:
                raise ValueError("%s: split block size %d" % (name, bsz))
            p = Program(i, hd[a + 6], hd[a + 7], struct.unpack_from("<b", hd, a + 8)[0],
                        struct.unpack_from("<b", hd, a + 9)[0], struct.unpack_from("<b", hd, a + 10)[0], hd[a + 11],
                        [], hd[a:a + hsz])
            for k in range(nsplit):
                s = hd[a + hsz + k * 20:a + hsz + (k + 1) * 20]
                if len(s) < 20:
                    raise ValueError("%s: short split" % name)
                sset, lo, xf, hi, num, bl, bh = struct.unpack_from("<HBBBBHH", s, 0)
                p.splits.append(Split(sset, lo & 0x7F, hi & 0x7F, xf, num, bl, bh, s[16], s[17],
                                      struct.unpack_from("<b", s, 18)[0], struct.unpack_from("<b", s, 19)[0], s))
            self.prog[i] = p

    # -- sample access ------------------------------------------------------------------------------------------
    def pcm(self, vag_index):
        """(int16 samples, info) for a VAG info index (cached).  info as in adpcm_decode plus `rate`."""
        c = self._pcm.get(vag_index)
        if c is None:
            v = self.vagi[vag_index]
            arr, info = adpcm_decode(self.bd, v.offset)
            info["rate"] = v.rate
            c = self._pcm[vag_index] = (arr, info)
        return c

    def vag_sizes(self):
        """Block-count based size (bytes) of each VAG: up to and including the END block."""
        res = {}
        for v in self.vagi:
            if v is None:
                continue
            o, n = v.offset, 0
            while o + 16 <= len(self.bd):
                n += 1
                if self.bd[o + 1] & 1:
                    break
                o += 16
            res[v.index] = n * 16
        return res

    def resolve(self, prog, key, vel):
        """All sample records a note-on (program, key, velocity) starts, as the IOP's note-on routine does:
        list of (Split, Sset, Smpl, VagInfo)."""
        p = self.prog.get(prog)
        out = []
        if not p:
            return out
        for sp in p.splits:
            if not (sp.lo <= key <= sp.hi):
                continue
            ss = self.sset.get(sp.sset)
            if ss is None or not (ss.vel_lo <= vel <= ss.vel_hi):
                continue
            for si in ss.smpl:
                sm = self.smpl.get(si)
                if sm is None or not ((sm.vel_lo & 0x7F) <= vel <= (sm.vel_hi & 0x7F)):
                    continue
                vg = self.vagi[sm.vag] if sm.vag < len(self.vagi) else None
                if vg is None or vg.offset == 0xFFFFFFFF:
                    continue
                out.append((sp, ss, sm, vg))
        return out

    def describe(self):
        lines = ["bank %s: HD %d bytes, BD %d bytes, %d VAGs, %d samples, %d sample sets, %d programs"
                 % (self.name, len(self.hd), len(self.bd), sum(v is not None for v in self.vagi), len(self.smpl),
                    len(self.sset), len(self.prog))]
        sizes = self.vag_sizes() if self.bd else {}
        for v in self.vagi:
            if v:
                lines.append("  vag %3d off %7d size %7s rate %5d flags %02x" % (v.index, v.offset, sizes.get(v.index, "?"),
                                                                               v.rate, v.flags))
        for i, p in sorted(self.prog.items()):
            lines.append("  program %3d vol %3d pan %3d transpose %d detune %d pan-key %d@%d, %d splits"
                         % (i, p.vol, p.pan, p.transpose, p.detune, p.pan_key_amount, p.pan_key_center, len(p.splits)))
            for k, sp in enumerate(p.splits):
                ss = self.sset.get(sp.sset)
                lines.append("    split %d keys %3d-%3d vol %3d pan %3d transp %d detune %d bend %d/%d -> sset %d vel %s"
                             % (k, sp.lo, sp.hi, sp.vol, sp.pan, sp.transpose, sp.detune, sp.bend_lo, sp.bend_hi, sp.sset,
                                "%d-%d curve %d" % (ss.vel_lo, ss.vel_hi, ss.vel_curve) if ss else "MISSING"))
                for si in (ss.smpl if ss else []):
                    sm = self.smpl.get(si)
                    if not sm:
                        lines.append("      smpl %d MISSING" % si)
                        continue
                    vg = self.vagi[sm.vag] if sm.vag < len(self.vagi) else None
                    lines.append("      smpl %d vag %d (%s Hz) base %d fine %d pan %d vol %d group %d prio %d  ADSR %04x/%04x [%s]"
                                 % (si, sm.vag, vg.rate if vg else "?", sm.base_key, sm.fine, sm.pan, sm.volume, sm.group,
                                    sm.priority, sm.adsr1, sm.adsr2, adsr_text(sm.adsr1, sm.adsr2)))
        return "\n".join(lines)


def adsr_text(a1, a2):
    return ("A%s%d D%d S%d | S%s%s%d R%s%d" % ("e" if a1 & 0x8000 else "l", (a1 >> 8) & 0x7F, (a1 >> 4) & 15, a1 & 15,
                                              "e" if a2 & 0x8000 else "l", "-" if a2 & 0x4000 else "+", (a2 >> 6) & 0x7F,
                                              "e" if a2 & 0x20 else "l", a2 & 31))


# ----------------------------------------------------------------------------------------------------------------
# 4. sequences (.sq)
# ----------------------------------------------------------------------------------------------------------------
@dataclass
class Ev:
    tick: int
    kind: str      # 'on', 'off', 'cc', 'pc', 'bend', 'press', 'tempo', 'end', 'sysex', 'meta', 'poly'
    ch: int = 0
    a: int = 0
    b: int = 0
    raw_pos: int = 0


class Song:
    """One `.sq`.  Layout: SCEIVers(0x10) | SCEISequ(0x20: size, filesize, song-chunk off or -1, midi-chunk off, -1, -1) |
    SCEIMidi(size, count-1 at +0xc, u32 song offsets at +0x10): each song = {u32 data_off (=6), u16 division, events...}.
    Events use Sony's compact coding (see parse_events)."""

    def __init__(self, data, name="", index=0):
        self.name = name
        self.data = data
        if data[:8] != b"IECSsreV" or data[0x10:0x18] != b"IECSuqeS":
            raise ValueError("%s: not an SQ" % name)
        sz, fsize, o_song, o_midi = struct.unpack_from("<4I", data, 0x18)
        self.file_size = fsize
        if fsize != len(data):
            raise ValueError("%s: SQ size field %d != %d" % (name, fsize, len(data)))
        if o_midi == 0xFFFFFFFF or data[o_midi:o_midi + 8] != b"IECSidiM":
            raise ValueError("%s: no SCEIMidi" % name)
        self.has_song_chunk = o_song != 0xFFFFFFFF
        csize, count_m1 = struct.unpack_from("<II", data, o_midi + 8)[0], struct.unpack_from("<I", data, o_midi + 0xC)[0]
        self.midi_chunk = (o_midi, csize)
        offs = struct.unpack_from("<%dI" % (count_m1 + 1), data, o_midi + 0x10)
        self.n_songs = count_m1 + 1
        self.song_offsets = [o_midi + o for o in offs]
        self.select(index)

    def select(self, index=0):
        a = self.song_offsets[index]
        self.index = index
        hdr, self.division = struct.unpack_from("<IH", self.data, a)
        self.ev_start = a + hdr
        end = self.midi_chunk[0] + self.midi_chunk[1]
        if index + 1 < self.n_songs:
            end = self.song_offsets[index + 1]
        self.ev_end = min(end, len(self.data))
        self.events, self.end_pos = parse_events(self.data, self.ev_start, self.ev_end)
        return self

    @property
    def tempo0(self):
        for e in self.events:
            if e.kind == "tempo":
                return e.a
        return 500000


def _vlq(d, p):
    v = 0
    while True:
        b = d[p]
        p += 1
        v = (v << 7) | (b & 0x7F)
        if not b & 0x80:
            return v, p


def parse_events(d, p, limit):
    """Decode the compact MIDI stream exactly like the IOP sequencer (MODMIDI FUN_00000f78 / FUN_000009b8).
    Differences from a Standard MIDI File:
      * a delta time precedes the first event; after each event a VLQ delta follows UNLESS the event's last data byte has
        its MSB set (then the next event is simultaneous: delta 0, and the byte's low 7 bits are the value);
      * note-off (8n) has ONE data byte (key); note-on/CC/bend/poly pressure have two; program change and channel
        pressure have one; running status is supported; meta 0xFF type len data (0x51 tempo, 0x2F end); 0xF0/0xF7 sysex
        with a VLQ length.
    Returns (events, position after the end-of-track meta)."""
    ev = []
    t = 0
    status = 0
    nodelta = False
    dt, p = _vlq(d, p)
    t += dt
    while p < limit:
        pos = p
        b = d[p]
        p += 1
        if b & 0x80:
            status = b if b < 0xF0 else status
            st = b
            d1 = d[p]
            p += 1
        else:
            if status == 0:
                raise ValueError("running status without status at 0x%x" % pos)
            st = status
            d1 = b
        if st < 0xF0:
            hi, ch = st & 0xF0, st & 0xF
            status = st
            if hi in (0x90, 0xB0, 0xE0, 0xA0):
                d2 = d[p]
                p += 1
                nodelta = bool((d1 | d2) & 0x80)
                d1 &= 0x7F
                d2v = d2 & 0x7F
                if hi == 0x90:
                    ev.append(Ev(t, "on" if d2v else "off", ch, d1, d2v, pos))
                elif hi == 0xB0:
                    ev.append(Ev(t, "cc", ch, d1, d2v, pos))
                elif hi == 0xE0:
                    ev.append(Ev(t, "bend", ch, d1, d2v, pos))
                else:
                    ev.append(Ev(t, "poly", ch, d1, d2v, pos))
            elif hi in (0x80, 0xC0, 0xD0):
                nodelta = bool(d1 & 0x80)
                d1 &= 0x7F
                ev.append(Ev(t, {0x80: "off", 0xC0: "pc", 0xD0: "press"}[hi], ch, d1, 0, pos))
            else:
                raise ValueError("bad status %02x at 0x%x" % (st, pos))
        else:
            nodelta = False
            if st == 0xFF:
                typ = d1
                ln, p = _vlq(d, p)
                body = d[p:p + ln]
                p += ln
                if typ == 0x51 and ln == 3:
                    ev.append(Ev(t, "tempo", 0, (body[0] << 16) | (body[1] << 8) | body[2], 0, pos))
                elif typ == 0x2F:
                    ev.append(Ev(t, "end", 0, 0, 0, pos))
                    return ev, p
                else:
                    ev.append(Ev(t, "meta", 0, typ, ln, pos))
            elif st == 0xF0 or st == 0xF7:
                # F0: status byte then VLQ length; our 'd1' was already consumed as the first length byte
                p -= 1
                ln, p = _vlq(d, p)
                p += ln
                ev.append(Ev(t, "sysex", 0, ln, 0, pos))
            else:
                raise ValueError("bad status %02x at 0x%x" % (st, pos))
        if nodelta:
            nodelta = False
        else:
            dt, p = _vlq(d, p)
            t += dt
    raise ValueError("sequence ends without FF 2F")


def tick_to_sec(song, tick):
    """Seconds at `tick` of one linear pass (tempo map of the song, no loop jumps)."""
    t, last, tempo, sec = 0, 0, 500000, 0.0
    for e in song.events:
        if e.tick > tick:
            break
        sec += (e.tick - last) * tempo / 1e6 / song.division
        last = e.tick
        if e.kind == "tempo":
            tempo = e.a
    return sec + (tick - last) * tempo / 1e6 / song.division


def flatten(song, max_seconds=None, extra_loops=1, max_loop_iter=64):
    """Resolve the Sony loop controllers (CC99=0/CC6=id: loop start id; CC99=1/CC6=id/CC38=n: loop end, n=0 -> endless;
    body repeats n more times) into a linear event list with absolute time in seconds.
    Endless loops are followed `extra_loops` additional times (default: play once, then the body once more).
    Returns (events [(t_sec, Ev)], total_seconds, info) where info has loop markers found."""
    evs = song.events
    div = song.division
    out = []
    tempo = 500000
    # state of the controller machine
    nrpn = None
    dentry = None
    loops = {}                       # id -> [event index after the CC6 that started it, tick, remaining or None]
    sec = 0.0
    last_tick = 0
    i = 0
    info = dict(loops=[], endless=False)
    endless_done = {}
    guard = 0
    while i < len(evs):
        e = evs[i]
        guard += 1
        if guard > 2_000_000:
            break
        sec += (e.tick - last_tick) * tempo / 1e6 / div
        last_tick = e.tick
        if max_seconds is not None and sec > max_seconds:
            break
        if e.kind == "tempo":
            tempo = e.a
        elif e.kind == "end":
            out.append((sec, e))
            break
        elif e.kind == "cc" and e.a == 99:
            nrpn, dentry = e.b, None
            out.append((sec, e))
        elif e.kind == "cc" and e.a == 98:
            out.append((sec, e))
        elif e.kind == "cc" and e.a == 6:
            dentry = e.b
            if nrpn == 0:
                loops[dentry] = [i, e.tick, None]
                info["loops"].append(("start", dentry, e.tick))
            out.append((sec, e))
        elif e.kind == "cc" and e.a == 38 and nrpn == 1 and dentry is not None:
            st = loops.get(dentry)
            cnt = e.b
            out.append((sec, e))
            if ("end", dentry, e.tick, cnt) not in info["loops"]:
                info["loops"].append(("end", dentry, e.tick, cnt))
            if st is not None:
                if st[2] is None:
                    st[2] = cnt
                if cnt == 0 or st[2] != 0:
                    if cnt == 0:
                        endless_done[dentry] = endless_done.get(dentry, 0) + 1
                        info["endless"] = True
                        if endless_done[dentry] > extra_loops:
                            nrpn = None
                            i += 1
                            continue
                    else:
                        st[2] -= 1
                    # jump back: next event to process is the one after the loop-start CC6
                    i = st[0] + 1
                    last_tick = st[1]
                    # the original sequencer rewinds its tick counter to the start tick; the delta of the CC6 event
                    # itself belongs to the body, so continue from its successor with the stored tick
                    continue
                else:
                    st[2] = None
        else:
            out.append((sec, e))
        i += 1
    return out, sec, info


# ----------------------------------------------------------------------------------------------------------------
# 5. synthesizer
# ----------------------------------------------------------------------------------------------------------------
def _seg_lin(start, step, period, n):
    """level after k = 0..n-1 samples when `step` is added every `period` samples (first update at sample `period`)."""
    k = np.arange(n, dtype=np.float64) // period
    return start + step * k


def _rate_params(rate7, increase, exp=False):
    """SPU2 envelope rate -> (step, period) in the nocash model: shift = rate>>2, n = rate&3."""
    shift, n = rate7 >> 2, rate7 & 3
    step = (7 - n) if increase else (-8 + n)
    period = 1 << max(0, shift - 11)
    step = step << max(0, 11 - shift)
    return step, period


def adsr_envelope(adsr1, adsr2, n_hold, n_max):
    """Amplitude envelope (float32 0..1) of a voice that is key-on for `n_hold` samples and then released.
    Returns an array that ends where the envelope has reached 0 (or n_max).  Model: PS1/SPU2 hardware ADSR (nocash)."""
    amode = bool(adsr1 & 0x8000)
    ar = (adsr1 >> 8) & 0x7F
    dr = (adsr1 >> 4) & 15
    sl = ((adsr1 & 15) + 1) * 0x800
    smode = bool(adsr2 & 0x8000)
    sdir_dec = bool(adsr2 & 0x4000)
    sr = (adsr2 >> 6) & 0x7F
    rmode = bool(adsr2 & 0x20)
    rr = adsr2 & 31
    segs = []
    level = 0.0
    total = 0
    cap = min(n_max, n_hold if n_hold is not None else n_max)

    def push(arr):
        nonlocal level, total
        if len(arr):
            segs.append(arr)
            total += len(arr)
            level = float(arr[-1])

    # --- attack
    step, period = _rate_params(ar, True)
    if not amode:
        # linear: level += step every `period` samples until 0x7fff
        n_up = int(math.ceil(0x7FFF / step)) * period + 1
        n = min(n_up, cap)
        push(np.minimum(_seg_lin(0, step, period, n), 0x7FFF))
    else:
        # exponential: normal speed while the level (before the update) is <= 0x6000, then 4x slower
        u1 = 0x6000 // step + 1                       # updates until the level first exceeds 0x6000
        n1 = u1 * period
        push(_seg_lin(0, step, period, min(n1, cap)))
        if total < cap:
            lv = step * u1
            n2 = int(math.ceil((0x7FFF - lv) / step)) * period * 4 + 1 if lv < 0x7FFF else 1
            n = min(n2, cap - total)
            push(np.minimum(_seg_lin(lv, step, period * 4, n), 0x7FFF))
    # --- decay (exponential decrease towards the sustain level)
    if total < cap and level > sl:
        step, period = _rate_params(dr * 4, False)
        r = 1.0 + step / 32768.0
        # level_k = level * r^k  until <= sl
        kmax = int(math.ceil(math.log(max(sl, 1) / level) / math.log(r))) if r < 1 else 0
        n = min(kmax * period + 1, cap - total)
        k = np.arange(n, dtype=np.float64) // period
        push(np.maximum(level * np.power(r, k), sl))
    # --- sustain
    if total < cap:
        n = cap - total
        if sdir_dec:
            step, period = _rate_params(sr, False)
            if smode:
                r = 1.0 + step / 32768.0
                k = np.arange(n, dtype=np.float64) // period
                arr = level * np.power(r, k)
            else:
                arr = np.maximum(_seg_lin(level, step, period, n), 0)
        else:
            step, period = _rate_params(sr, True)
            arr = np.minimum(_seg_lin(level, step, period, n), 0x7FFF)
            if smode:        # exponential increase: slower above 0x6000 - approximated by the linear rule
                pass
        push(arr)
    held = np.concatenate(segs) if segs else np.zeros(0)
    # release starts from the level at n_hold
    if n_hold is not None and n_hold < len(held) + 1 or n_hold is not None and len(held) >= n_hold:
        pass
    if n_hold is None:
        env = held
    else:
        env = held[:n_hold] if len(held) >= n_hold else np.concatenate([held, np.full(n_hold - len(held), level)])
        lv = float(env[-1]) if len(env) else 0.0
        step, period = _rate_params(rr * 4, False)
        if lv > 0:
            if rmode:
                r = 1.0 + step / 32768.0
                kmax = int(math.ceil(math.log(1.0 / lv) / math.log(r))) + 1 if lv > 1 else 1
                n = min(kmax * period, n_max - len(env))
                k = np.arange(n, dtype=np.float64) // period
                rel = lv * np.power(r, k)
                rel = rel[rel >= 1.0]
            else:
                n = min(int(math.ceil(lv / -step)) * period, n_max - len(env))
                rel = np.maximum(_seg_lin(lv, step, period, n), 0)
            env = np.concatenate([env, rel])
    return (env / 32767.0).astype(np.float32)


def _pan_gains(pan):
    """HSyn pan law (MODHSYN FUN_00004534): pan 0..127 (64 centre): the far channel is attenuated linearly, the near one is
    left at unity.  Returns (gain_left, gain_right) arrays for a pan array."""
    pan = np.asarray(pan, dtype=np.float64)
    d = pan - 64.0
    right = np.where(d > 0, np.clip((63.0 - d) / 63.0, 0, 1), 1.0)       # pan right  -> left channel attenuated
    left = np.where(d > 0, 1.0, np.clip((63.0 + d) / 63.0, 0, 1))
    return right, left      # (gain_left, gain_right)


class ChannelState:
    __slots__ = ("prog", "port", "curves", "pedal_t")

    def __init__(self, port=0):
        self.prog = 0
        self.port = port
        # controller automation: name -> list of (time_sec, value)
        self.curves = {"vol": [(0.0, 127)], "expr": [(0.0, 127)], "pan": [(0.0, 64)], "bend": [(0.0, 0x2000)]}
        self.pedal_t = []        # [(time, down)]


def _curve_eval(curve, t_arr):
    ts = np.array([c[0] for c in curve])
    vs = np.array([c[1] for c in curve], dtype=np.float64)
    idx = np.searchsorted(ts, t_arr, side="right") - 1
    return vs[np.clip(idx, 0, len(vs) - 1)]


class Synth:
    """Offline mixer.  `banks` maps hsyn port number -> Bank (0 = A, 1 = E, 2 = I, 10 = G, 11 = S, 12 = Q, 13 = M, 14 = I,
    15 = C).  Output is float32 stereo at OUT_RATE."""

    def __init__(self, banks, rate=OUT_RATE):
        self.banks = banks
        self.rate = rate
        self.buf = None
        self.n = 0
        self.notes_rendered = 0
        self.missing = 0

    def _ensure(self, n):
        if self.buf is None:
            self.buf = np.zeros((max(n, 1), 2), dtype=np.float32)
        elif n > len(self.buf):
            nb = np.zeros((n, 2), dtype=np.float32)
            nb[:len(self.buf)] = self.buf
            self.buf = nb

    def note(self, t_on, t_off, port, prog, key, vel, ch_curves=None, gain=1.0, pan_offset=0, max_len=30.0,
             one_shot_ok=True):
        """Mix one note.  ch_curves: dict of automation curves (vol, expr, pan, bend) or None for defaults.  t_off=None =
        hold for the natural length of the sample."""
        bank = self.banks.get(port)
        if bank is None or bank.empty:
            self.missing += 1
            return
        layers = bank.resolve(prog, key, vel)
        if not layers:
            self.missing += 1
            return
        prog_o = bank.prog[prog]
        sr = self.rate
        for sp, ss, sm, vg in layers:
            pcm, info = bank.pcm(sm.vag)
            if len(pcm) == 0:
                continue
            f = pcm.astype(np.float32) / 32768.0
            looping = info["looping"]
            ls = info["loop_start"] if info["loop_start"] is not None else 0
            n_src = len(f)
            # --- pitch
            note_f = key + prog_o.transpose + sp.transpose + (prog_o.detune + sm.fine + sp.detune) / 128.0
            base_ratio = (vg.rate / sr) * 2.0 ** ((note_f - sm.base_key) / 12.0)
            # --- amplitude (nominal unity = everything at 128 / velocity 127 / CC 127)
            curve = ss.vel_curve & 15       # MODHSYN table PTR_FUN_0000a7f8: 0 = identity, 2 = v*v/127 (1,3..5 unused here)
            v_eff = vel if curve != 2 else max(1, vel * vel // 127)
            amp = (prog_o.vol / 128.0) * (sp.vol / 128.0) * (sm.volume / 128.0) * (v_eff / 127.0) * gain
            # --- key-follow pan
            pan0 = (prog_o.pan - 64) + (sp.pan - 64) + (sm.pan - 64) + 64 + pan_offset
            if prog_o.pan_key_amount:
                pan0 += (key - prog_o.pan_key_center) * prog_o.pan_key_amount / 12.0
            sp_amt = struct.unpack_from("<b", sp.raw, 14)[0]          # split key-follow pan (always 0 in the shipped data)
            if sp_amt:
                pan0 += (key - sp.raw[15]) * sp_amt / 12.0
            # --- how long is the voice?
            if t_off is None:
                hold_s = None
            else:
                hold_s = max(t_off - t_on, 0.0)
            if looping:
                hold_s = hold_s if hold_s is not None else 1.0
                hold_s = min(hold_s, max_len)
                n_hold = int(hold_s * sr)
                n_max = min(n_hold + int(10 * sr), int(max_len * sr * 2))
                env = adsr_envelope(sm.adsr1, sm.adsr2, n_hold, n_max)
                n_out = len(env)
            else:
                # one-shot: key-off cannot stop the SPU from playing the sample to its end, but the ADSR release can
                natural = int(n_src / max(base_ratio, 1e-6))
                if hold_s is None:
                    hold_s = natural / sr
                n_hold = min(int(hold_s * sr), natural)
                n_max = min(natural, int(max_len * sr))
                env = adsr_envelope(sm.adsr1, sm.adsr2, n_hold if hold_s * sr < natural else None, n_max)
                n_out = min(len(env), natural, n_max)
                env = env[:n_out]
            if n_out <= 0:
                continue
            # --- time axis and automation
            t_axis = t_on + np.arange(n_out, dtype=np.float64) / sr
            ratio = np.full(n_out, base_ratio)
            if ch_curves is not None:
                bend = _curve_eval(ch_curves["bend"], t_axis) - 0x2000
                rng = (sp.bend_hi if True else sp.bend_lo) / 256.0
                rng_lo = sp.bend_lo / 256.0
                semis = np.where(bend >= 0, bend / 8192.0 * rng, bend / 8192.0 * rng_lo)
                ratio = ratio * 2.0 ** (semis / 12.0)
                chvol = _curve_eval(ch_curves["vol"], t_axis) / 127.0 * _curve_eval(ch_curves["expr"], t_axis) / 127.0
                pan = _curve_eval(ch_curves["pan"], t_axis) - 64.0 + pan0
            else:
                chvol = 1.0
                pan = np.full(n_out, pan0)
            ratio = np.minimum(ratio, 0x3FFF / 0x1000)          # SPU2 pitch register limit 0x3FFF (4x of 48 kHz)
            pos = np.cumsum(ratio) - ratio
            if looping:
                end_pos = n_src
                loopn = max(end_pos - ls, 1)
                over = pos >= end_pos
                pos = np.where(over, ls + np.mod(pos - ls, loopn), pos)
            else:
                alive = pos < n_src - 1
                if not alive.all():
                    n_out = int(alive.sum())
                    pos, env, t_axis = pos[:n_out], env[:n_out], t_axis[:n_out]
                    pan = pan[:n_out] if isinstance(pan, np.ndarray) else pan
                    chvol = chvol[:n_out] if isinstance(chvol, np.ndarray) else chvol
            if n_out <= 0:
                continue
            i0 = np.floor(pos).astype(np.int64)
            fr = (pos - i0).astype(np.float32)
            i1 = i0 + 1                                     # next sample; past the end it is the loop start (looping) or the last sample
            if looping:
                i1 = np.where(i1 >= n_src, ls, i1)
            else:
                i1 = np.minimum(i1, n_src - 1)
            i0 = np.minimum(i0, n_src - 1)
            s = f[i0] * (1.0 - fr) + f[i1] * fr
            sig = s * env[:n_out] * (amp * chvol)
            gl, gr = _pan_gains(pan)
            start = int(round(t_on * sr))
            self._ensure(start + n_out + 1)
            self.buf[start:start + n_out, 0] += (sig * gl).astype(np.float32)
            self.buf[start:start + n_out, 1] += (sig * gr).astype(np.float32)
        self.notes_rendered += 1

    def finish(self, reverb_depth=0, peak_limit=0.98):
        out = self.buf if self.buf is not None else np.zeros((1, 2), np.float32)
        if reverb_depth > 0:
            out = out + reverb(out, self.rate, reverb_depth / 128.0)
        peak = float(np.max(np.abs(out))) if len(out) else 0.0
        scale = 1.0
        if peak > peak_limit:
            scale = peak_limit / peak
        return out * scale, peak, scale


def reverb(x, rate, wet):
    """Small Schroeder reverb (4 combs + 2 allpasses per side), used for the cfg 'REVERBE mode,depth' send (depth/128)."""
    n = len(x)
    out = np.zeros_like(x)
    combs = [(1557, 0.78), (1617, 0.77), (1491, 0.76), (1422, 0.75)]
    sc = rate / 44100.0
    for c in range(2):
        mono = x[:, c].astype(np.float64)
        acc = np.zeros(n)
        for (d, g), off in zip(combs, (0, 23, 41, 59)):
            d = int((d + off * c) * sc)
            y = np.zeros(n)
            # y[i] = mono[i] + g*y[i-d]  (block-wise recursion)
            for s in range(0, n, d):
                e = min(s + d, n)
                y[s:e] = mono[s:e] + (g * y[s - d:e - d] if s >= d else 0)
            acc += y
        out[:, c] = (acc * 0.25 * wet).astype(np.float32)
    return out


# ----------------------------------------------------------------------------------------------------------------
# 6. sequence rendering
# ----------------------------------------------------------------------------------------------------------------
def render_song(song, banks, port=0, port_volume=102, max_seconds=None, extra_loops=1, reverb_depth=0, route_cc32=True):
    """Render a Song to (float32 stereo array, stats dict).  `banks` = {hsyn port: Bank}; the player's own port is `port`
    (0 for BGM = bank A, 1 for E = ambient, 2 for I); CC32 on a channel re-routes it to hsyn port (value & 15) like the IOP."""
    evs, total, info = flatten(song, max_seconds=max_seconds, extra_loops=extra_loops)
    syn = Synth(banks)
    chans = [ChannelState(port) for _ in range(16)]
    active = [dict() for _ in range(16)]          # key -> list of (t_on, vel, port, prog)
    pedal = [False] * 16
    pending_off = [[] for _ in range(16)]
    gain = port_volume / 256.0
    ntotal = 0

    # 1st pass: controller curves per channel (needed by notes that started earlier)
    for t, e in evs:
        c = chans[e.ch] if e.kind in ("cc", "pc", "bend") else None
        if c is None:
            continue
        if e.kind == "bend":
            c.curves["bend"].append((t, (e.b << 7) | e.a))
        elif e.kind == "cc":
            if e.a == 7:
                c.curves["vol"].append((t, e.b))
            elif e.a == 11:
                c.curves["expr"].append((t, e.b))
            elif e.a == 10:
                c.curves["pan"].append((t, e.b))
    notes = []                       # (t_on, t_off, ch, port, prog, key, vel)
    open_notes = [dict() for _ in range(16)]
    for t, e in evs:
        if e.kind in ("on", "off", "cc", "pc"):
            c = chans[e.ch]
        if e.kind == "pc":
            c.prog = e.a
        elif e.kind == "cc":
            if e.a == 32 and route_cc32:
                c.port = e.b & 15
            elif e.a == 64:
                down = e.b >= 64
                if pedal[e.ch] and not down:
                    for n in pending_off[e.ch]:
                        n[1] = t
                    pending_off[e.ch] = []
                pedal[e.ch] = down
        elif e.kind == "on":
            n = [t, None, e.ch, c.port, c.prog, e.a, e.b]
            open_notes[e.ch].setdefault(e.a, []).append(n)
            notes.append(n)
        elif e.kind == "off":
            lst = open_notes[e.ch].get(e.a)
            if lst:
                n = lst.pop(0)
                if pedal[e.ch]:
                    pending_off[e.ch].append(n)
                else:
                    n[1] = t
    end_t = total
    for n in notes:
        if n[1] is None:
            n[1] = end_t
    for n in notes:
        t_on, t_off, ch, prt, prog, key, vel = n
        syn.note(t_on, t_off, prt, prog, key, vel, ch_curves=chans[ch].curves, gain=gain)
        ntotal += 1
    out, peak, scale = syn.finish(reverb_depth)
    # trim trailing silence
    ls = [x[2] for x in info["loops"] if x[0] == "start"]
    le = [x[2] for x in info["loops"] if x[0] == "end"]
    stats = dict(seconds=len(out) / OUT_RATE, notes=ntotal, missing=syn.missing, peak_before_scale=peak, scale=scale,
                 sequence_seconds=total, loops=info["loops"], endless=info["endless"],
                 loop_start_s=tick_to_sec(song, ls[0]) if ls else None, loop_end_s=tick_to_sec(song, le[0]) if le else None,
                 song_end_s=tick_to_sec(song, song.events[-1].tick))
    return out, stats


# ----------------------------------------------------------------------------------------------------------------
# 7. SE / bank-letter mapping, SE tables from the main executable
# ----------------------------------------------------------------------------------------------------------------
# hsyn port of each bank letter (LoadHdBd_x: A->0, E->1, I->2 and 14, G->10, S->11, Q->12, M->13, C->15)
LETTER_PORT = {"a": 0, "e": 1, "i": 2, "g": 10, "s": 11, "q": 12, "m": 13, "c": 15}
SE_PORTS = {10: "g", 11: "s", 12: "q", 13: "m", 14: "i", 15: "c"}
SE_PORT_NAMES = {10: "G (ground/foot)", 11: "S (voice)", 12: "Q (special SE)", 13: "M (monster/menu)", 14: "I (item/effect)",
                 15: "C (common/character)"}


def exe_tables():
    """Read the SE tables out of the main executable (uses tools/dc/elfdata.py -> iso/Dark Cloud (USA).iso, read only).
    Returns dict(se_info=[(prog, key, flag, port)]*2800, cap=[list or None]*101, voice=[...]*11, basic=[2], special=[64])."""
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    import elfdata
    e = elfdata.Elf()

    def ent(va, n):
        raw = e.read(va, 6 * n)
        return [struct.unpack_from("<4b", raw, 6 * i) for i in range(n)]

    def table(va, maxn):
        out = []
        for i in range(maxn):
            t = struct.unpack("<4b", e.read(va + 6 * i, 4))
            if t[0] == -128:
                break
            out.append(t)
        return out

    res = dict(se_info=ent(0x25DFB0, 2800))
    cp = struct.unpack("<101I", e.read(0x265BA0, 404))
    res["cap"] = [table(p, 100) if p else None for p in cp]
    vp = struct.unpack("<11I", e.read(0x266580, 44))
    res["voice"] = [table(p, 100) if p else None for p in vp]
    bp = struct.unpack("<2I", e.read(0x2A1ED8, 8))
    res["basic"] = [table(p, 200) for p in bp]
    res["special"] = ent(0x2665B0, 65)[:64]
    return res


def se_lookup(tabs, se_id, basic_no=-1, cap_no=-1, voice_set=-1):
    """GetSeInfo(id): returns (prog, key, flag, port) or None.  100..299 -> basic table, 300..399 -> cap table of the sound
    set, 400..499 -> voice table of the loaded voice set, everything else -> the global table."""
    if se_id < 0 or se_id > 0xAEF:
        return None
    if basic_no >= 0 and 100 <= se_id < 300:
        t = tabs["basic"][basic_no]
        i = se_id - 100
        return t[i] if i < len(t) else None
    if 300 <= se_id < 400 and cap_no >= 0 and tabs["cap"][cap_no]:
        t = tabs["cap"][cap_no]
        i = se_id - 300
        return t[i] if i < len(t) else None
    if 400 <= se_id < 500 and voice_set >= 0 and tabs["voice"][voice_set]:
        t = tabs["voice"][voice_set]
        i = se_id - 400
        return t[i] if i < len(t) else None
    return tabs["se_info"][se_id]


def render_se(banks, prog, key, vel=127, hold=1.0, port=14, vol=64, max_len=6.0):
    """Render one SE note (hs-message note-on of `key` with velocity `vel`) from bank port `port`."""
    syn = Synth(banks)
    syn.note(0.0, hold, port, prog, key, vel, ch_curves=None, gain=vol / 127.0, max_len=max_len)
    out, peak, scale = syn.finish(0)
    return out, dict(seconds=len(out) / OUT_RATE, peak_before_scale=peak, scale=scale, missing=syn.missing)


# ----------------------------------------------------------------------------------------------------------------
# 7b. executable specification of the game's Snd* API (what the C++ audio layer has to do)
# ----------------------------------------------------------------------------------------------------------------
class SoundModel:
    """Python model of the main executable's sound layer.  Method names follow the C++ API proposed in notes/sound.md;
    the comments name the Snd* function each one reproduces.  It owns the banks that are 'loaded on the IOP' (one per hsyn
    port) and can render a BGM sequence or a sound effect exactly the way the game's tables select them."""

    def __init__(self):
        self.tabs = exe_tables()
        self.setbl = tbl_se_info()
        self.setbl_vol = {(r[0], r[1]): r[2] for r in self.setbl}
        self.sq_vol = tbl_sq_vol()
        self.banks = {}                  # hsyn port -> Bank
        self.seqs = {"a": [], "e": [], "i": []}
        self.basic_table = -1
        self.cap_table = -1
        self.voice_set = -1
        self.sound_set = -1
        self.bgm_no = -1
        self.bgm_reverb = (0, 0)
        self.set_reverb = (0, 0)

    # SndSoundLoad(n): LoadSoundFileFromPack(set/snd<n>.snd) + TABLE + REVERBE of the pack's .cfg
    def load_sound_set(self, n):
        f = SndFile(snd_path("set", n))
        for letter in "ceigm":
            for p in [p for p, b in self.banks.items() if p in {LETTER_PORT[letter]} | ({14} if letter == "i" else set())]:
                del self.banks[p]
        for letter, b in f.banks().items():
            self.banks[LETTER_PORT[letter]] = b
            if letter == "i":
                self.banks[14] = b
        self.seqs["e"] = [x for x in f.songs() if x[0] == "e"]
        self.seqs["i"] = [x for x in f.songs() if x[0] == "i"]
        self.basic_table, self.cap_table = f.cfg.get("TABLE", (-1, -1))
        self.set_reverb = f.cfg.get("REVERBE", (0, 0))
        self.sound_set = n

    # SndVoiceLoad(n): voice<n>.snd -> S bank on port 11; SE ids 400..499 use voice table n
    def load_voice(self, n):
        f = SndFile(snd_path("voice", n))
        self.banks[11] = list(f.banks().values())[0]
        self.voice_set = n

    # SndSPSeLoad(n): special/sp<n>.snd -> Q bank on port 12
    def load_special(self, n):
        f = SndFile(snd_path("special", n))
        self.banks[12] = list(f.banks().values())[0]

    # SndBgmLoad(n): bgm/bgm<n>.snd -> A bank on port 0, sequences of type a; reverb for core 0
    def load_bgm(self, n):
        f = SndFile(snd_path("bgm", n))
        self.banks[0] = f.banks().get("a") or next(iter(f.banks().values()))
        self.seqs["a"] = [x for x in f.songs() if x[0] == "a"] or f.songs()
        self.bgm_reverb = f.cfg.get("REVERBE", (0, 0))
        self.bgm_no = n

    # GetSeInfo + GetPortNo: (program, key, port) of an SE id, port defaults to 14 (0x0e) when the table says -1
    def se_info(self, se_id):
        t = se_lookup(self.tabs, se_id, self.basic_table, self.cap_table, self.voice_set)
        if t is None or t[0] < 0:
            return None
        return t[0], t[1], (t[3] if t[3] >= 0 else 14)

    # SndGetVolf(id, volf): setbl volume (0x40 when the (program,key) pair is not in setbl.txt) * volf, clamped 0..127
    def se_volume(self, se_id, volf=None):
        i = self.se_info(se_id)
        if i is None:
            return 0
        v = self.setbl_vol.get((i[0], i[1]), 0x40)
        if volf is None:
            return v
        return max(0, min(127, int(v * volf)))

    # SndSePlay(id[, volf, panf]) -> one note on the SE port: program change, volume, pan, note-on key velocity 127
    def render_se(self, se_id, volf=None, panf=0.0, hold=None, max_len=8.0):
        i = self.se_info(se_id)
        if i is None:
            raise KeyError("SE %d has no table entry" % se_id)
        prog, key, port = i
        pan = int(max(-1.0, min(1.0, panf)) * 63.0) + 0x40            # SndGetPanf
        syn = Synth(self.banks)
        syn.note(0.0, hold, port, prog, key, 127, ch_curves=None, gain=self.se_volume(se_id, volf) / 127.0,
                 pan_offset=pan - 64, max_len=max_len)
        out, peak, scale = syn.finish(0)
        return out, dict(seconds=len(out) / OUT_RATE, peak_before_scale=peak, scale=scale, missing=syn.missing,
                         program=prog, key=key, port=port, volume=self.se_volume(se_id, volf))

    # SndSPSePlay(i): special_se_info[i] on port 12, volume = setbl volume of (prog,key) (0x40 if absent)
    def render_special(self, i, hold=None, max_len=8.0):
        prog, key, _, port = self.tabs["special"][i]
        if prog < 0:
            raise KeyError("special SE %d has no table entry" % i)
        vol = self.setbl_vol.get((prog, key), 0x40)
        syn = Synth(self.banks)
        syn.note(0.0, hold, 12, prog, key, 127, ch_curves=None, gain=vol / 127.0, max_len=max_len)
        out, peak, scale = syn.finish(0)
        return out, dict(seconds=len(out) / OUT_RATE, peak_before_scale=peak, scale=scale, missing=syn.missing,
                         program=prog, key=key, port=12, volume=vol)

    # SndBgmPlay(idx): SQ_Play(0, idx): sequence #idx of the loaded A list, port volume = sqtbl.txt value
    def render_bgm(self, idx=0, kind="a", **kw):
        letter, name, sg = self.seqs[kind][idx]
        vol = self.sq_vol.get(name, 100)
        rev = self.bgm_reverb[1] if kind == "a" else self.set_reverb[1]
        out, st = render_song(sg, self.banks, port=LETTER_PORT[kind], port_volume=vol, reverb_depth=rev, **kw)
        st["sequence"] = name
        st["port_volume"] = vol
        return out, st


# ----------------------------------------------------------------------------------------------------------------
# 8. command line
# ----------------------------------------------------------------------------------------------------------------
def _all_snd():
    for sub in ("bgm", "set", "sound_set", "special", "voice"):
        for p in sorted((SOUND / sub).glob("*.snd"), key=lambda q: int(re.sub(r"\D", "", q.stem))):
            yield sub, p


def cmd_list(a):
    for sub, p in _all_snd():
        s = SndFile(p)
        print("%s/%s: %d bytes; %s; txt=%s cfg=%s" % (sub, p.name, s.size, ", ".join("%s(%d)" % (n, len(d)) for n, d, _ in s.entries
                                                                              if not n.endswith((".txt", ".cfg"))),
                                                   ",".join(s.txt), s.cfg))


def cmd_info(a):
    s = SndFile(a.file)
    print("pack %s size %d, %d entries, tail %d" % (a.file, s.size, len(s.entries), len(s.tail)))
    for n, d, x in s.entries:
        print("  %-14s %8d  extra=%d" % (n, len(d), x))
    print("load list:", s.txt, "cfg:", s.cfg)
    for k, b in s.banks().items():
        print(b.describe() if a.verbose else "bank %s: %d programs, %d samples" % (b.name, len(b.prog), len(b.smpl)))
    for letter, n, sg in s.songs():
        print("song %s: division %d tempo0 %d us (%.1f bpm) events %d" % (n, sg.division, sg.tempo0, 6e7 / sg.tempo0, len(sg.events)))


def cmd_wav(a):
    s = SndFile(a.file)
    out = Path(a.out)
    n = 0
    for name, b in s.banks().items():
        for v in b.vagi:
            if v is None or not b.bd:
                continue
            pcm, info = b.pcm(v.index)
            if len(pcm) == 0:
                continue
            fn = out / ("%s_%03d.wav" % (b.name.rsplit(".", 1)[0], v.index))
            write_wav(fn, pcm, v.rate)
            n += 1
    print("wrote %d wav files to %s" % (n, out))


def cmd_programs(a):
    s = SndFile(a.file)
    for k, b in s.banks().items():
        print(b.describe())


def cmd_seq(a):
    s = SndFile(a.file)
    songs = s.songs()
    for letter, n, sg in songs:
        for i in range(sg.n_songs):
            sg.select(i)
            evs, total, info = flatten(sg, extra_loops=0)
            progs = {}
            for e in sg.events:
                if e.kind == "pc":
                    progs.setdefault(e.ch, set()).add(e.a)
            chans = sorted({e.ch for e in sg.events if e.kind in ("on", "off")})
            print("%s[%d]: division %d, tempo %.1f bpm, %d events, %d notes, channels %s, programs %s, length %.1fs (one pass), loops %s"
                  % (n, i, sg.division, 6e7 / sg.tempo0, len(sg.events), sum(e.kind == "on" for e in sg.events), chans,
                     {k: sorted(v) for k, v in progs.items()}, total, info["loops"]))
            if a.dump:
                for e in sg.events[:a.dump]:
                    print("   ", e)


def banks_by_port(s):
    """{hsyn port: Bank} for a SndFile: the load list's banks on the ports the game's LoadHdBd_x puts them on (the I bank is
    loaded twice: port 2 for the I sequencer, port 14 for sound effects)."""
    banks = {}
    for letter, b in s.banks().items():
        banks[LETTER_PORT[letter]] = b
        if letter == "i":
            banks[14] = b
    return banks


_banks_for_bgm = banks_by_port


def cmd_render_bgm(a):
    s = SndFile(snd_path("bgm", a.n))
    banks = _banks_for_bgm(s)
    songs = [x for x in s.songs() if x[0] == "a"]
    if not songs:
        songs = s.songs()
    letter, name, sg = songs[a.seq]
    vols = tbl_sq_vol()
    vol = vols.get(name, 100)
    rev = s.cfg.get("REVERBE", (0, 0))[1] if not a.noreverb else 0
    print("rendering bgm%d (%s) volume %d reverb depth %d" % (a.n, name, vol, rev), flush=True)
    out, st = render_song(sg, banks, port=0, port_volume=vol, max_seconds=a.max_seconds, extra_loops=a.loops, reverb_depth=rev)
    write_wav(a.out, out, OUT_RATE, 2)
    print(a.out, st)


def cmd_render_se(a):
    m = SoundModel()
    m.load_sound_set(a.set)
    if a.voice is not None:
        m.load_voice(a.voice)
    if a.special is not None:
        m.load_special(a.special)
    print("SE %d -> %s (set %d: basic table %d, cap table %d)" % (a.id, m.se_info(a.id), a.set, m.basic_table, m.cap_table))
    out, st = m.render_se(a.id, hold=a.hold)
    write_wav(a.out, out, OUT_RATE, 2)
    print(a.out, st)


def cmd_render_amb(a):
    m = SoundModel()
    m.load_sound_set(a.set)
    out, st = m.render_bgm(a.idx, kind="e", extra_loops=a.loops, max_seconds=a.max_seconds)
    write_wav(a.out, out, OUT_RATE, 2)
    print(a.out, st)


def cmd_render_special(a):
    m = SoundModel()
    m.load_special(a.n)
    out, st = m.render_special(a.n, hold=a.hold)
    write_wav(a.out, out, OUT_RATE, 2)
    print(a.out, st)


def cmd_verify(a):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from verify_snd import run
    run(a)


def main():
    try:
        sys.stdout.reconfigure(encoding="utf-8", errors="backslashreplace")
    except Exception:
        pass
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    p = sub.add_parser("list")
    p.set_defaults(f=cmd_list)
    p = sub.add_parser("info")
    p.add_argument("file")
    p.add_argument("-v", "--verbose", action="store_true")
    p.set_defaults(f=cmd_info)
    p = sub.add_parser("wav")
    p.add_argument("file")
    p.add_argument("out")
    p.set_defaults(f=cmd_wav)
    p = sub.add_parser("programs")
    p.add_argument("file")
    p.set_defaults(f=cmd_programs)
    p = sub.add_parser("seq")
    p.add_argument("file")
    p.add_argument("--dump", type=int, default=0)
    p.set_defaults(f=cmd_seq)
    p = sub.add_parser("render-bgm")
    p.add_argument("n", type=int)
    p.add_argument("out")
    p.add_argument("--seq", type=int, default=0)
    p.add_argument("--loops", type=int, default=1)
    p.add_argument("--max-seconds", type=float, default=None)
    p.add_argument("--noreverb", action="store_true")
    p.set_defaults(f=cmd_render_bgm)
    p = sub.add_parser("render-se")
    p.add_argument("set", type=int)
    p.add_argument("id", type=int)
    p.add_argument("out")
    p.add_argument("--hold", type=float, default=None, help="seconds until SE_Stop (default: one-shots play to their end, loops 1 s)")
    p.add_argument("--voice", type=int, default=None, help="also load voice<n>.snd (SE ids 400..499)")
    p.add_argument("--special", type=int, default=None, help="also load special/sp<n>.snd")
    p.set_defaults(f=cmd_render_se)
    p = sub.add_parser("render-amb", help="render ambient/battle-layer sequence #idx (type E) of a sound set")
    p.add_argument("set", type=int)
    p.add_argument("idx", type=int)
    p.add_argument("out")
    p.add_argument("--loops", type=int, default=1)
    p.add_argument("--max-seconds", type=float, default=None)
    p.set_defaults(f=cmd_render_amb)
    p = sub.add_parser("render-special", help="render special SE n (special/sp<n>.snd)")
    p.add_argument("n", type=int)
    p.add_argument("out")
    p.add_argument("--hold", type=float, default=None)
    p.set_defaults(f=cmd_render_special)
    p = sub.add_parser("verify")
    p.add_argument("--decode", action="store_true", help="also ADPCM-decode every sample (slow)")
    p.set_defaults(f=cmd_verify)
    a = ap.parse_args()
    a.f(a)


if __name__ == "__main__":
    main()
