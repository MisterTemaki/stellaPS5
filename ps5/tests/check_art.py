#!/usr/bin/env python3
# The home-screen background: pic0.dds and pic1.dds are the same file, a 3840x2160 BC7_UNORM DX10 DDS with one mip
# level (the header the home screen takes), and decode to the key art, background-source.png (PSNR over 40 dB).
#   check_art.py <sce_sys dir>
# SPDX-License-Identifier: MIT
import os
import struct
import sys

import numpy as np
from PIL import Image

d = sys.argv[1]
dds = open(os.path.join(d, 'pic0.dds'), 'rb').read()
problems = []
if dds != open(os.path.join(d, 'pic1.dds'), 'rb').read():
    problems.append('pic1.dds differs from pic0.dds')
h, w = struct.unpack_from('<II', dds, 12)
mips = struct.unpack_from('<I', dds, 28)[0]
if dds[:4] != b'DDS ' or dds[84:88] != b'DX10' or struct.unpack_from('<I', dds, 128)[0] != 98:
    problems.append('not a BC7_UNORM DX10 DDS')
if (w, h, mips) != (3840, 2160, 1) or len(dds) != 148 + 3840 * 2160:
    problems.append('size %dx%d, %d mip(s), %d bytes' % (w, h, mips, len(dds)))
if not problems:
    a = np.asarray(Image.open(os.path.join(d, 'pic0.dds')).convert('RGB'), np.float32)
    b = np.asarray(Image.open(os.path.join(d, 'background-source.png')).convert('RGB').resize((3840, 2160)), np.float32)
    mse = float(((a - b) ** 2).mean())
    psnr = 99.0 if mse == 0 else 10 * np.log10(255 ** 2 / mse)
    print('pic0.dds against background-source.png: PSNR %.1f dB' % psnr)
    if psnr < 40:
        problems.append('pic0.dds is not background-source.png (PSNR %.1f dB): run make art' % psnr)
for p in problems:
    print(p)
sys.exit(1 if problems else 0)
