#!/usr/bin/env python3
"""Makes the licence files the app folder carries (ps5/app/licenses/, committed; the app zip and the payload install
them in <app>/licenses/):
  COPYING.txt   the GNU GPL version 3, the licence of the app as a whole (the repository's COPYING)
  LGPL-2.1.txt  the GNU LGPL 2.1 (libsmb2, libnfs; Blargg's NTSC filter)
  NOTICES.txt   what is in the app, under which terms, and the notices the permissive licences ask to be kept
                with it (taken from the sources themselves)

  make_notices.py <repository root> <out dir>
SPDX-License-Identifier: MIT
"""
import os
import re
import sys

root, out = sys.argv[1], sys.argv[2]
ps5 = os.path.join(root, "ps5")


def read(*p):
    with open(os.path.join(*p), encoding="utf-8", errors="replace") as f:
        return f.read()


def comment_block(text, start, end):
    """The lines between the first line containing `start` and the next containing `end` (both included), with the
    comment markers taken off."""
    lines = text.splitlines()
    i = next(k for k, l in enumerate(lines) if start in l)
    j = next(k for k in range(i, len(lines)) if end in lines[k])
    return "\n".join(re.sub(r"^\s*(/?\*+/?|//)\s?", "", l).rstrip() for l in lines[i:j + 1]).strip("\n")


os.makedirs(out, exist_ok=True)
with open(os.path.join(out, "COPYING.txt"), "w", encoding="utf-8") as f:
    f.write(read(root, "COPYING"))
with open(os.path.join(out, "LGPL-2.1.txt"), "w", encoding="utf-8") as f:
    f.write(read(ps5, "third_party", "libsmb2", "LICENCE-LGPL-2.1.txt"))

cores = os.path.join(ps5, "third_party", "cores")
sections = [
    ("Stella PS5", """Stella PS5 is free software: you can redistribute it and/or modify it under the terms of the GNU General
Public License as published by the Free Software Foundation, either version 3 of the License, or (at your option)
any later version (COPYING.txt). Its complete source: https://github.com/MisterTemaki/stellaPS5 (each release has
its source zip). It combines the programs below; Gearlynx is GPL-3.0-or-later, the others' licences allow that
combination, so the whole is distributed under the GPL version 3 or later.

- Stella 7.0 (the Stella team: Bradford W. Mott, Stephen Anthony, Thomas Jentzsch, Christian Speckner and
  contributors; https://stella-emu.github.io/): GPL-2.0-or-later. Includes Blargg's NTSC filter (LGPL-2.1-or-later,
  LGPL-2.1.txt) and nlohmann/json (MIT, below).
- a5200 (the Atari800 development team's emulation; its 5200 port and libretro core by their authors):
  GPL-2.0-or-later. Altirra's 5200 OS ROM replacement (Avery Lee): all-permissive, below.
- ProSystem (Greg Stanton; the libretro contributors): GPL-2.0-or-later. Ron Fries' TIA and POKEY sound: LGPL-2.0
  (used under the GPL, its section 3), below. Osman Celimli's BupChip: zlib, below.
- Gearlynx (Ignacio Sanchez): GPL-3.0-or-later. miniz: MIT (with public-domain parts), below.
- The PS5 frontend (MisterTemaki and the ports it comes from): MIT.
- ps5-native-app-boilerplate's runtime and tools (BlackBearReloaded): GPL-3.0-or-later.
- libsmb2, libnfs (Ronnie Sahlberg and contributors): LGPL-2.1-or-later (LGPL-2.1.txt); their full source and what
  is needed to relink the app with a modified version are in the source zip.
- Mbed TLS 3.6 (the Mbed TLS contributors): Apache-2.0, below. Mozilla's CA certificate list (as packaged by
  certifi): MPL-2.0 (https://mozilla.org/MPL/2.0/; the file is data/cacert.pem in the source).
- zlib (Jean-loup Gailly, Mark Adler) and minizip (Gilles Vollant and others): zlib, below.
- LZMA SDK (Igor Pavlov): public domain. stb_image, stb_image_resize2, stb_truetype (Sean Barrett): MIT or public
  domain.
- CRT shaders rewritten from libretro's slang-shaders: see THIRD_PARTY_SHADERS.md in the source (public domain, CC0,
  Unlicense and MIT notices).
- Fonts: Roboto (Google, Apache-2.0), PromptFont (Yukari Hafner, SIL OFL 1.1), Font Awesome Brands (Fonticons; font
  SIL OFL 1.1, icons CC BY 4.0), below."""),
    ("miniz (Gearlynx)", comment_block(read(cores, "gearlynx", "platforms", "shared", "dependencies", "miniz",
                                             "miniz.c"), "Copyright 2013-2014 RAD", "THE SOFTWARE.")),
    ("BupChip (ProSystem)", read(cores, "prosystem", "bupboop", "License.txt").strip()),
    ("TIA and POKEY sound (ProSystem)", comment_block(read(cores, "prosystem", "core", "Tia.c"),
                                                     "TiaSound is Copyright", "Free Software Foundation, Inc., 675")),
    ("Altirra 5200 OS ROM replacement (a5200)", comment_block(read(cores, "a5200", "emu", "altirra_5200_os.c"),
                                                             "Altirra - Atari", "without any warranty.")),
    ("zlib", comment_block(read(ps5, "third_party", "zlib", "zlib.h"), "Copyright (C) 1995", "misrepresented as being")
     + "\n  3. This notice may not be removed or altered from any source distribution."),
    ("nlohmann/json (Stella)", read(root, "src", "lib", "json", "LICENSE.MIT").strip()),
    ("Mbed TLS", read(ps5, "frontend", "third_party", "mbedtls", "LICENSE").strip()),
    ("Roboto", read(ps5, "frontend", "assets", "fonts", "Roboto-Regular-copyright").strip()),
    ("PromptFont", read(ps5, "frontend", "assets", "fonts", "promptfont-license").strip()),
    ("Font Awesome Brands", read(ps5, "frontend", "assets", "fonts", "FontAwesome-LICENSE.txt").strip()),
]
with open(os.path.join(out, "NOTICES.txt"), "w", encoding="utf-8") as f:
    for title, body in sections:
        f.write("=" * 78 + "\n" + title + "\n" + "=" * 78 + "\n\n" + body.rstrip() + "\n\n")
print("written", sorted(os.listdir(out)))
