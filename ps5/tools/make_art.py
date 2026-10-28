#!/usr/bin/env python3
# Stella PS5: the home-screen background, pic0.dds / pic1.dds, from the project's key art (background-source.png,
# chosen by the author; this script only reads it): 3840x2160, BC7 DX10 DDS without mipmaps (bc7enc_rdo), with the
# header the home screen takes (as ps5-native-app-boilerplate's tools/prepare-assets.sh writes it). The icon
# (icon0.png) is the project's art too, used as it is.
#   make_art.py <sce_sys dir> <bc7enc>
# SPDX-License-Identifier: MIT
import os
import shutil
import struct
import subprocess
import sys
import tempfile

from PIL import Image

out, bc7enc = sys.argv[1], sys.argv[2]
bg = Image.open(os.path.join(out, 'background-source.png')).convert('RGB').resize((3840, 2160), Image.LANCZOS)
tmp = tempfile.mkdtemp()
src = os.path.join(tmp, 'bg.png')
bg.save(src)
subprocess.run([bc7enc, '-q', '-g', src, os.path.join(tmp, 'bg.dds')], check=True, cwd=tmp)
# the header the console's home screen takes: mip count 1 (with its flag), depth 1, DX10 alpha mode "straight"
dds = bytearray(open(os.path.join(tmp, 'bg.dds'), 'rb').read())
assert dds[:4] == b'DDS ' and dds[84:88] == b'DX10'
struct.pack_into('<I', dds, 8, struct.unpack_from('<I', dds, 8)[0] | 0x20000)
struct.pack_into('<I', dds, 24, 1)
struct.pack_into('<I', dds, 28, 1)
struct.pack_into('<I', dds, 144, 1)
for name in ('pic0.dds', 'pic1.dds'):
    open(os.path.join(out, name), 'wb').write(dds)
shutil.rmtree(tmp)
print('art written to', out)
