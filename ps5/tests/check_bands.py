#!/usr/bin/env python3
"""check_bands.py dump.ppm PATTERN -- the Atari 2600 test game's 12 bands (tests/make_test_rom.py), top to bottom, as
the dump shows them down the middle of the picture: R red (off), G green (on), . either. Exit 1 on a mismatch.

  0 left fire   1 left up   2 left down   3 left left   4 left right   5 right fire   6 right left
  7 Game Reset  8 Game Select  9 TV type B&W  10 left difficulty A  11 right difficulty A

The picture is Stella's 160x228 (Fit to screen, 1080 lines): the game's 192 lines start 67 lines down, a band is 16
of them (75.5 dump lines). A 720p dump is scaled."""
import sys

path, want = sys.argv[1], sys.argv[2]
with open(path, "rb") as f:
    data = f.read()
parts = data.split(b"\n", 3)
w, h = map(int, parts[1].split())
pix = parts[3]
s = h / 1080
got = ""
for i in range(12):
    x, y = int(960 * s), int((104 + 75.5 * i) * s)
    o = (y * w + x) * 3
    r, g, b = pix[o], pix[o + 1], pix[o + 2]
    got += "G" if g > r + 40 else "R" if r > g + 40 else "?"
ok = len(want) == 12 and all(c == "." or c == d for c, d in zip(want, got))
print(f"{path}: bands {got}, expected {want}: {'ok' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
