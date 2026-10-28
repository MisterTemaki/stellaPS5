#!/usr/bin/env python3
"""forge_crc.py file CRC32HEX [offset [skip]] -- changes 4 bytes of a test ROM so the CRC32 of the file past its first
`skip` bytes becomes CRC32HEX (for the name-by-CRC tests: a test ROM the No-Intro table knows).

The 4 bytes are at `offset`, which must hold zeros (bytes the test ROM doesn't use), or appended when the offset is
"end". Without an offset, the test ROMs' own spare bytes (tests/make_test_rom.py):
  .a26   $FF00 of the 4 KiB cartridge (offset 0xF00), the whole file's CRC
  .a78   $C000 of the cartridge (offset 128 + 0x4000), the CRC past the 128-byte header (No-Intro's)
  .a52   $8000 of the cartridge (offset 0x4000), the whole file's CRC
  else   appended
CRC32 is affine in those bits, so 32 single-bit probes and Gaussian elimination over GF(2) solve it."""
import os
import sys
import zlib

path, target = sys.argv[1], int(sys.argv[2], 16)
data = bytearray(open(path, 'rb').read())
ext = os.path.splitext(path)[1].lower()
defaults = {'.a26': (0xF00, 0), '.a78': (128 + 0x4000, 128), '.a52': (0x4000, 0)}
at, skip = defaults.get(ext, ('end', 0))
if len(sys.argv) > 3:
    at = sys.argv[3] if sys.argv[3] == 'end' else int(sys.argv[3], 0)
    skip = int(sys.argv[4], 0) if len(sys.argv) > 4 else 0
if at == 'end':
    at = len(data)
    data += b'\0\0\0\0'
assert data[at:at + 4] == b'\0\0\0\0', "the bytes to change must be unused (zero)"


def crc_with(x):
    d = bytearray(data)
    d[at:at + 4] = x.to_bytes(4, 'little')
    return zlib.crc32(bytes(d[skip:]))


base = crc_with(0)
cols = [crc_with(1 << bit) ^ base for bit in range(32)]
want = base ^ target
basis = {}
for i in range(32):
    v, mask = cols[i], 1 << i
    for b in range(31, -1, -1):
        if not (v >> b) & 1:
            continue
        if b in basis:
            v ^= basis[b][0]
            mask ^= basis[b][1]
        else:
            basis[b] = (v, mask)
            break
x, v = 0, want
for b in range(31, -1, -1):
    if (v >> b) & 1:
        v ^= basis[b][0]
        x ^= basis[b][1]
assert v == 0
data[at:at + 4] = x.to_bytes(4, 'little')
assert zlib.crc32(bytes(data[skip:])) == target
open(path, 'wb').write(data)
print(f"{path}: crc32 {target:08X}")
