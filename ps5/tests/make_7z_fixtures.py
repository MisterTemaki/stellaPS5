#!/usr/bin/env python3
"""Makes the .7z archives the 7z tests use (tests/7z/, committed: the tests need no network and no py7zr), from the
port's own test games (make_test_rom.py). Needs py7zr (pip install py7zr).

  make_7z_fixtures.py <out-dir>

  lzma.7z        Card (USA).a26, LZMA                  lzma2.7z    the same, LZMA2
  ppmd.7z        the same, PPMd                        bcj.7z      the same, x86 BCJ + LZMA2
  delta.7z       the same, Delta + LZMA2
  solid.7z       readme.txt, Alpha (USA).a26, Beta (USA).a78 in one LZMA2 block: the first game is Alpha
  a78.7z         notes/readme.txt, then Seven (USA).a78: an Atari 7800 game after a text file
  header.7z      tet.a78: the 7800 test game with the CRC of its data past the 128-byte header made Asteroids (USA)'s
                 (forge_crc.py): named by the CRC past its header (No-Intro's), once unpacked
  named.7z       splat.a26: the 2600 test game with its CRC made Adventure (USA)'s (forge_crc.py)
  empty.7z       no files at all                       nogame.7z   readme.txt only
  other.7z       Game (USA).sfc + readme.txt: nothing the app lists
  bzip2.7z       Card (USA).a26, BZip2: a method the decoder doesn't have
  aes.7z         Card (USA).a26, AES-256 (password "atari"), names readable
  aeshdr.7z      the same with the names encrypted too
  big.7z         Big (USA).a26 of 600 KiB: over the 512 KiB a game in an archive may be
  bigblock.7z    Card (USA).a26 then 65 MiB of padding in one solid block: over the 64 MiB block limit
  truncated.7z   lzma2.7z cut in half                  corrupt.7z  lzma2.7z with its packed data changed (CRC error)
  aesbig.7z      Big (USA).a26 of 600 KiB, AES: refused as encrypted (not as too big)
  ppmdbig.7z     Card (USA).a26, PPMd with a 256 MiB model: over the decoder's 136 MiB budget
  many.7z        70,000 empty files, then Card (USA).a26: more files than an archive may list (65,536)
"""
import os
import subprocess
import sys

import py7zr

HERE = os.path.dirname(os.path.abspath(__file__))

# py7zr writes PPMd's properties as 7 bytes (order, memory, two zero bytes); 7-Zip writes 5, and the LZMA SDK's C decoder
# takes only 5. The stream itself is the same PPMd (variant H, 7z's range coder): write the properties as 7-Zip does.
_ppmd_props = py7zr.compressor.PpmdCompressor.encode_filter_properties.__func__


def _ppmd_props_7zip(cls, f):
    return _ppmd_props(cls, f)[:5]


py7zr.compressor.PpmdCompressor.encode_filter_properties = classmethod(_ppmd_props_7zip)
_ppmd_decode = py7zr.compressor.PpmdCompressor._decode_property


def _ppmd_decode_7zip(self, properties):
    return _ppmd_decode(self, properties + b"\0\0" if len(properties) == 5 else properties)


py7zr.compressor.PpmdCompressor._decode_property = _ppmd_decode_7zip


def card(path, kind=None):
    subprocess.run([sys.executable, os.path.join(HERE, "make_test_rom.py"), path] + ([kind] if kind else []), check=True,
                   stdout=subprocess.DEVNULL)
    with open(path, "rb") as f:
        return f.read()


def forged(path, crc):
    subprocess.run([sys.executable, os.path.join(HERE, "forge_crc.py"), path, crc], check=True, stdout=subprocess.DEVNULL)
    with open(path, "rb") as f:
        data = f.read()
    os.remove(path)
    return data


def nointro(system, title):
    with open(os.path.join(HERE, "..", "frontend", "data", "nointro.tsv"), encoding="utf-8") as f:
        return next(l.split("\t")[1] for l in f if l.rstrip("\n").endswith("\t" + title) and l.startswith(system + "\t"))


def make(out, files, filters=None, password=None, header_encryption=False):
    if os.path.exists(out):
        os.remove(out)
    with py7zr.SevenZipFile(out, "w", filters=filters, password=password, header_encryption=header_encryption) as z:
        for name, data in files:
            z.writestr(data, name)


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    rom = card(os.path.join(out, "tmp.a26"))
    seven = card(os.path.join(out, "tmp.a78"))
    splat = forged(os.path.join(out, "tmp.a26"), nointro("a26", "Adventure (USA)"))
    tet = forged(os.path.join(out, "tmp.a78"), nointro("a78", "Asteroids (USA)"))
    readme = b"A test archive for Stella PS5.\n" * 4
    big = rom + b"\xff" * (600 * 1024 - len(rom))
    lz2 = [{"id": py7zr.FILTER_LZMA2, "preset": 9}]
    one = [("Card (USA).a26", rom)]
    aes = [{"id": py7zr.FILTER_LZMA2}, {"id": py7zr.FILTER_CRYPTO_AES256_SHA256}]
    make(os.path.join(out, "lzma.7z"), one, [{"id": py7zr.FILTER_LZMA}])
    make(os.path.join(out, "lzma2.7z"), one, lz2)
    make(os.path.join(out, "ppmd.7z"), one, [{"id": py7zr.FILTER_PPMD, "order": 6, "mem": 24}])
    make(os.path.join(out, "bcj.7z"), one, [{"id": py7zr.FILTER_X86}, {"id": py7zr.FILTER_LZMA2}])
    make(os.path.join(out, "delta.7z"), one, [{"id": py7zr.FILTER_DELTA}, {"id": py7zr.FILTER_LZMA2}])
    make(os.path.join(out, "solid.7z"), [("readme.txt", readme), ("Alpha (USA).a26", rom), ("Beta (USA).a78", seven)], lz2)
    make(os.path.join(out, "a78.7z"), [("notes/readme.txt", readme), ("Seven (USA).a78", seven)], lz2)
    make(os.path.join(out, "header.7z"), [("tet.a78", tet)], lz2)
    make(os.path.join(out, "named.7z"), [("splat.a26", splat)], lz2)
    make(os.path.join(out, "empty.7z"), [], lz2)
    make(os.path.join(out, "nogame.7z"), [("readme.txt", readme)], lz2)
    make(os.path.join(out, "other.7z"), [("Game (USA).sfc", rom), ("readme.txt", readme)], lz2)
    make(os.path.join(out, "bzip2.7z"), one, [{"id": py7zr.FILTER_BZIP2}])
    make(os.path.join(out, "aes.7z"), one, aes, password="atari")
    make(os.path.join(out, "aeshdr.7z"), one, aes, password="atari", header_encryption=True)
    make(os.path.join(out, "big.7z"), [("Big (USA).a26", big)], lz2)
    make(os.path.join(out, "aesbig.7z"), [("Big (USA).a26", big)], aes, password="atari")
    make(os.path.join(out, "ppmdbig.7z"), one, [{"id": py7zr.FILTER_PPMD, "order": 6, "mem": 28}])
    make(os.path.join(out, "many.7z"), [("%05d.txt" % i, b"") for i in range(70000)] + one, lz2)
    make(os.path.join(out, "bigblock.7z"), [("Card (USA).a26", rom), ("padding.bin", b"\0" * (65 * 1024 * 1024))], lz2)
    with open(os.path.join(out, "lzma2.7z"), "rb") as f:
        good = f.read()
    with open(os.path.join(out, "truncated.7z"), "wb") as f:
        f.write(good[: len(good) // 2])
    bad = bytearray(good)
    # the packed data starts right after the 32-byte signature header: change a byte in the middle of it
    bad[32 + 40] ^= 0x55
    with open(os.path.join(out, "corrupt.7z"), "wb") as f:
        f.write(bad)
    for n in sorted(os.listdir(out)):
        print("%-14s %7d bytes" % (n, os.path.getsize(os.path.join(out, n))))


if __name__ == "__main__":
    main()
