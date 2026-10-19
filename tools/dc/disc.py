"""Patch a COPY of the Dark Cloud ISO: replace files inside DATA.DAT without rebuilding the disc.

Disc layout (checked): ... DATA.HED, DATA.HD2, DATA.DAT (1.73 GB) and then a 63 MB filler file "DMMY." right after it.
DATA.DAT can therefore be grown in place by taking sectors from DMMY.
Index files (both hold the same offset/size per file; the game may use either, so both are kept in sync):
  DATA.HED : 80-byte entries   name[64], u32 byte offset (multiple of 2048), u32 size, u32 first sector, u32 sector count
  DATA.HD2 : 32-byte records   u32 name offset (into the name table that follows the records), 3 x u32 zero,
                               u32 offset, u32 size, u32 first sector, u32 sector count
New/larger data is appended at the end of DATA.DAT (old data stays, wasted); data that still fits its old slot is
overwritten in place. The ISO directory records of DATA.DAT and DMMY are updated (both-endian fields).

Usage as a library:   d = Disc("iso/x.patched.iso"); d.put("dun\\\\mainchara\\\\c01d.chr", data); d.commit()
"""
import shutil
import struct
from pathlib import Path

SECTOR = 2048


def both_endian(v):
    return struct.pack("<I", v) + struct.pack(">I", v)


class Disc:
    def __init__(self, path, readonly=False):
        self.path = Path(path)
        self.f = open(self.path, "rb" if readonly else "r+b")      # PCSX2 locks an ISO it has open for writing, but not for reading
        self._find_files()
        self.hed = bytearray(self._read_file("DATA.HED;1"))
        self.hd2 = bytearray(self._read_file("DATA.HD2;1"))
        self.n = len(self.hed) // 80
        self.names = [bytes(self.hed[i * 80:i * 80 + 64]).split(b"\0")[0].decode("latin1") for i in range(self.n)]
        self.index = {n.lower(): i for i, n in enumerate(self.names)}
        self.dat_size = self.files["DATA.DAT;1"]["size"]
        self.dirty = False

    # --- ISO9660 ------------------------------------------------------------------------------------
    def _find_files(self):
        self.f.seek(16 * SECTOR)
        pvd = self.f.read(SECTOR)
        if pvd[1:6] != b"CD001":
            raise ValueError("not an ISO9660 image")
        root_lba, root_len = struct.unpack_from("<I", pvd, 158)[0], struct.unpack_from("<I", pvd, 166)[0]
        self.f.seek(root_lba * SECTOR)
        d = self.f.read(root_len)
        self.root_lba = root_lba
        self.files, pos = {}, 0
        while pos < len(d):
            n = d[pos]
            if n == 0:
                pos = (pos // SECTOR + 1) * SECTOR
                continue
            name = d[pos + 33:pos + 33 + d[pos + 32]].decode("latin1")
            lba, size = struct.unpack_from("<I", d, pos + 2)[0], struct.unpack_from("<I", d, pos + 10)[0]
            self.files[name] = dict(lba=lba, size=size, rec=root_lba * SECTOR + pos, dir=bool(d[pos + 25] & 2))
            pos += n
        for need in ("DATA.HED;1", "DATA.HD2;1", "DATA.DAT;1", "DMMY.;1"):
            if need not in self.files:
                raise ValueError(f"{need} not found in the ISO root")

    def _read_file(self, name):
        e = self.files[name]
        self.f.seek(e["lba"] * SECTOR)
        return self.f.read(e["size"])

    def _write_record(self, name, lba, size):
        e = self.files[name]
        self.f.seek(e["rec"] + 2)
        self.f.write(both_endian(lba))
        self.f.write(both_endian(size))
        e["lba"], e["size"] = lba, size

    # --- reading --------------------------------------------------------------------------------------
    def entry(self, name):
        i = self.index[name.lower()]
        off, size, sec, cnt = struct.unpack_from("<4I", self.hed, i * 80 + 64)
        return i, off, size, sec, cnt

    def read(self, name):
        _, off, size, _, _ = self.entry(name)
        self.f.seek(self.files["DATA.DAT;1"]["lba"] * SECTOR + off)
        return self.f.read(size)

    # --- writing --------------------------------------------------------------------------------------
    def capacity_left(self):
        """Bytes of DMMY that can still be taken (keeping at least one sector so the file stays valid)."""
        dm = self.files["DMMY.;1"]
        end = dm["lba"] * SECTOR + ((dm["size"] + SECTOR - 1) // SECTOR) * SECTOR
        dat_end = self.files["DATA.DAT;1"]["lba"] * SECTOR + ((self.dat_size + SECTOR - 1) // SECTOR) * SECTOR
        return end - dat_end - SECTOR

    def put(self, name, data):
        i, off, size, sec, cnt = self.entry(name)
        dat_lba = self.files["DATA.DAT;1"]["lba"]
        need = (len(data) + SECTOR - 1) // SECTOR
        if need <= cnt:                       # fits the old slot: overwrite in place
            new_off, new_sec, new_cnt = off, sec, need
        else:                                 # append at the end of DATA.DAT
            new_off = ((self.dat_size + SECTOR - 1) // SECTOR) * SECTOR
            if new_off + need * SECTOR - self.dat_size > self.capacity_left() + (new_off - self.dat_size):
                raise RuntimeError("not enough filler space left after DATA.DAT")
            new_sec, new_cnt = new_off // SECTOR, need
            self.dat_size = new_off + len(data)
        self.f.seek(dat_lba * SECTOR + new_off)
        self.f.write(data + bytes(need * SECTOR - len(data)))
        struct.pack_into("<4I", self.hed, i * 80 + 64, new_off, len(data), new_sec, new_cnt)
        struct.pack_into("<4I", self.hd2, i * 32 + 16, new_off, len(data), new_sec, new_cnt)
        self.dirty = True
        return new_off

    def commit(self):
        """Write the index files back and fix the DATA.DAT / DMMY directory records."""
        for fname, buf in (("DATA.HED;1", self.hed), ("DATA.HD2;1", self.hd2)):
            e = self.files[fname]
            self.f.seek(e["lba"] * SECTOR)
            self.f.write(buf)
        dat, dm = self.files["DATA.DAT;1"], self.files["DMMY.;1"]
        dm_end = dm["lba"] * SECTOR + ((dm["size"] + SECTOR - 1) // SECTOR) * SECTOR
        new_dat_sectors = (self.dat_size + SECTOR - 1) // SECTOR
        new_dm_lba = dat["lba"] + new_dat_sectors
        if dat["size"] != self.dat_size:
            if new_dm_lba * SECTOR >= dm_end:
                raise RuntimeError("DATA.DAT would overrun the disc")
            self._write_record("DMMY.;1", new_dm_lba, dm_end - new_dm_lba * SECTOR)
            self._write_record("DATA.DAT;1", dat["lba"], self.dat_size)
        self.f.flush()
        self.dirty = False

    def verify(self):
        """Every index entry must lie inside DATA.DAT, HED and HD2 must agree, and the layout must be consistent."""
        problems = []
        dat = self.files["DATA.DAT;1"]
        for i in range(self.n):
            o1 = struct.unpack_from("<4I", self.hed, i * 80 + 64)
            o2 = struct.unpack_from("<4I", self.hd2, i * 32 + 16)
            if o1 != o2:
                problems.append(f"{self.names[i]}: HED {o1} != HD2 {o2}")
            off, size, sec, cnt = o1
            if off + size > dat["size"] or off != sec * SECTOR or cnt != (size + SECTOR - 1) // SECTOR:
                problems.append(f"{self.names[i]}: bad entry {o1} (DATA.DAT size {dat['size']})")
        dm = self.files["DMMY.;1"]
        if dm["lba"] < dat["lba"] + (dat["size"] + SECTOR - 1) // SECTOR:
            problems.append("DMMY overlaps DATA.DAT")
        return problems

    def close(self):
        self.f.close()


def make_copy(src, dst):
    shutil.copyfile(src, dst)
    return Disc(dst)
