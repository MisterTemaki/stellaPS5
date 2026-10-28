#!/usr/bin/env python3
"""Builds tiny test ROMs for the host tests (no game data needed: the programs are written here, tests/asm6502.py).

  make_test_rom.py out.a26 [ntsc|pal|paddles|sound]   Atari 2600, 4 KiB
      The screen is 12 bands, top to bottom, red, each turning green while what it watches is on:
        0 left fire   1 left up   2 left down   3 left left   4 left right   5 right fire   6 right left
        7 Game Reset  8 Game Select  9 TV type B&W  10 left difficulty A  11 right difficulty A
      and channel 0 plays a pure tone (AUDC 4, AUDF 15: 31440 / 32 = 982.5 Hz NTSC).
      pal: 312 lines a frame (Stella detects a 50 Hz PAL game), PAL colours; paddles: the same game also reads the
      first paddle (LDA INPT0 / BPL), which Stella's detection takes for a paddle game; sound: the tone only, the
      bands black (a quiet picture for the sound test).
  make_test_rom.py out.a52 [5200]       Atari 5200: background red / green (Fire 1) / blue (analog stick right)
  make_test_rom.py out.a78 [7800|7800bin]  Atari 7800 (.a78 header, or none): red / green (button 1) / blue (right)
  make_test_rom.py out.o [lynx]          Atari Lynx BS93 homebrew: red / green (A) / blue (B)
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from asm6502 import Asm  # noqa: E402

# 2600 registers
VSYNC, VBLANK, WSYNC, COLUBK = 0x00, 0x01, 0x02, 0x09
AUDC0, AUDF0, AUDV0 = 0x15, 0x17, 0x19
INPT0, INPT4, INPT5 = 0x08, 0x0C, 0x0D
SWCHA, SWCHB = 0x280, 0x282

# (register, bit mask, "on" when the bit is 0 (pressed / B&W) or 1 (difficulty A))
BANDS = [
    (INPT4, 0x80, 0), (SWCHA, 0x10, 0), (SWCHA, 0x20, 0), (SWCHA, 0x40, 0), (SWCHA, 0x80, 0),
    (INPT5, 0x80, 0), (SWCHA, 0x04, 0),
    (SWCHB, 0x01, 0), (SWCHB, 0x02, 0), (SWCHB, 0x08, 0), (SWCHB, 0x40, 1), (SWCHB, 0x80, 1),
]


def rom2600(kind):
    pal = kind == "pal"
    red, green = (0x64, 0x54) if pal else (0x44, 0xC6)
    if kind == "sound":
        red = green = 0x00
    # 312 lines a frame for PAL (an even count: Stella's detection takes an odd one for half NTSC), 261 for NTSC
    vblank_lines, band_lines, overscan = (45, 19, 37) if pal else (37, 16, 30)
    a = Asm(0xF000)
    a.label("reset")
    a.op("sei"); a.op("cld"); a.op("ldx", "#", 0xFF); a.op("txs")
    a.op("lda", "#", 0); a.op("ldx", "#", 0)
    a.label("clear"); a.op("sta", "zpx", 0); a.op("inx"); a.op("bne", "rel", "clear")
    a.op("lda", "#", 4); a.op("sta", "zp", AUDC0)
    a.op("lda", "#", 15); a.op("sta", "zp", AUDF0)
    a.op("lda", "#", 8); a.op("sta", "zp", AUDV0)
    a.label("frame")
    a.op("lda", "#", 2); a.op("sta", "zp", VBLANK); a.op("sta", "zp", VSYNC)
    for _ in range(3):
        a.op("sta", "zp", WSYNC)
    a.op("lda", "#", 0); a.op("sta", "zp", VSYNC)
    # the bands' colours, in RAM $80.. (a few lines of the vertical blank)
    for i, (reg, mask, on_when) in enumerate(BANDS):
        a.op("ldx", "#", red)
        if reg < 0x100:
            a.op("lda", "zp", reg)
        else:
            a.op("lda", "abs", reg)
        a.op("and", "#", mask)
        a.op("bne" if on_when == 0 else "beq", "rel", f"off{i}")
        a.op("ldx", "#", green)
        a.label(f"off{i}")
        a.op("stx", "zp", 0x80 + i)
    if kind == "paddles":
        a.op("lda", "zp", INPT0); a.op("bpl", "rel", "pad0"); a.label("pad0")
    a.op("ldy", "#", vblank_lines - 4)
    a.label("vb"); a.op("sta", "zp", WSYNC); a.op("dey"); a.op("bne", "rel", "vb")
    a.op("sta", "zp", WSYNC)
    a.op("lda", "#", 0); a.op("sta", "zp", VBLANK)
    for i in range(len(BANDS)):
        a.op("lda", "zp", 0x80 + i); a.op("sta", "zp", WSYNC); a.op("sta", "zp", COLUBK)
        a.op("ldy", "#", band_lines - 1)
        a.label(f"band{i}"); a.op("sta", "zp", WSYNC); a.op("dey"); a.op("bne", "rel", f"band{i}")
    a.op("lda", "#", 2); a.op("sta", "zp", VBLANK); a.op("lda", "#", 0); a.op("sta", "zp", COLUBK)
    a.op("ldy", "#", overscan)
    a.label("os"); a.op("sta", "zp", WSYNC); a.op("dey"); a.op("bne", "rel", "os")
    a.op("jmp", "abs", "frame")
    code = a.assemble()
    rom = bytearray(4096)
    rom[:len(code)] = code
    # $FF00..$FF03 stay 0: tests/forge_crc.py may change them (a ROM with a given CRC)
    start = a.labels["reset"]
    rom[0xFFC] = start & 0xFF
    rom[0xFFD] = start >> 8
    rom[0xFFE] = start & 0xFF
    rom[0xFFF] = start >> 8
    return bytes(rom)



# ---- Atari 5200: a 32 KiB cartridge ($4000-$BFFF). The background (COLBK) is red; green while Fire 1 is held, blue
# while the analog joystick is pushed right (POT0 over 170: proportional, as a5200 reads the left stick), and channel 1
# plays a pure tone (AUDF1 40: 63921 / 82 = 780 Hz). NMIs are off: no OS vertical blank writes the colours over ours.
def rom5200(kind):
    COLBK, TRIG0, CONSOL = 0xC01A, 0xC010, 0xC01F
    DMACTL, NMIEN, VCOUNT = 0xD400, 0xD40E, 0xD40B
    POT0, AUDF1, AUDC1, AUDCTL, POTGO, SKCTL = 0xE800, 0xE800, 0xE801, 0xE808, 0xE80B, 0xE80F
    red, green, blue = 0x34, 0xC6, 0x86
    a = Asm(0x4000)
    a.label("start")
    a.op("sei"); a.op("cld"); a.op("ldx", "#", 0xFF); a.op("txs")
    a.op("lda", "#", 0); a.op("sta", "abs", NMIEN); a.op("sta", "abs", DMACTL); a.op("sta", "abs", AUDCTL)
    a.op("lda", "#", 0x04); a.op("sta", "abs", CONSOL)
    a.op("lda", "#", 0x03); a.op("sta", "abs", SKCTL)
    a.op("lda", "#", 40); a.op("sta", "abs", AUDF1)
    a.op("lda", "#", 0xA8); a.op("sta", "abs", AUDC1)
    a.op("sta", "abs", POTGO)
    a.label("frame")
    a.label("wait1"); a.op("lda", "abs", VCOUNT); a.op("cmp", "#", 124); a.op("bne", "rel", "wait1")
    a.op("ldx", "#", red)
    a.op("lda", "abs", POT0); a.op("cmp", "#", 170); a.op("bcc", "rel", "notright"); a.op("ldx", "#", blue)
    a.label("notright")
    a.op("lda", "abs", TRIG0); a.op("and", "#", 1); a.op("bne", "rel", "nofire"); a.op("ldx", "#", green)
    a.label("nofire")
    a.op("stx", "abs", COLBK)
    a.op("sta", "abs", POTGO)
    a.label("wait2"); a.op("lda", "abs", VCOUNT); a.op("cmp", "#", 124); a.op("beq", "rel", "wait2")
    a.op("jmp", "abs", "frame")
    code = a.assemble()
    rom = bytearray(32768)
    rom[:len(code)] = code
    title = b"STELLA PS5 TEST 5200"
    rom[0x7FE8:0x7FE8 + len(title)] = title
    rom[0x7FFD] = 0xFF  # no title screen
    start = a.labels["start"]
    rom[0x7FFE] = start & 0xFF
    rom[0x7FFF] = start >> 8
    return bytes(rom)


# ---- Atari 7800: a 32 KiB cartridge ($8000-$FFFF) with its .a78 header (128 bytes; .bin: without). MARIA shows an
# empty display list on every line, so the screen is the background colour: red, green while button 1 (INPT4) is
# held, blue while the joystick is pushed right; the TIA plays a tone (AUDC0 4, AUDF0 15: about 980 Hz).
def rom7800(kind, header=True):
    BACKGRND, DPPH, DPPL, CTRL, MSTAT, WSYNC = 0x20, 0x2C, 0x30, 0x3C, 0x28, 0x24
    INPT4, AUDC0, AUDF0, AUDV0, SWCHA, CTLSWB, SWCHB = 0x0C, 0x15, 0x17, 0x19, 0x280, 0x283, 0x282
    red, green, blue = 0x34, 0xC6, 0x86
    dll, dl = 0x1800, 0x1880  # in RAM: 16 DLL entries of 16 lines each, all to one empty display list
    a = Asm(0x8000)
    a.label("start")
    a.op("sei"); a.op("cld")
    a.op("lda", "#", 0x07); a.op("sta", "zp", 0x01)  # INPTCTRL: lock the 7800 mode
    a.op("ldx", "#", 0xFF); a.op("txs")
    a.op("lda", "#", 0x7F); a.op("sta", "zp", CTRL)  # DMA off while the lists are made
    a.op("lda", "#", 0)
    a.op("sta", "abs", dl); a.op("sta", "abs", dl + 1)
    a.op("ldx", "#", 0)
    a.label("mkdll")
    a.op("lda", "#", 15); a.op("sta", "absx", dll)
    a.op("lda", "#", dl >> 8); a.op("sta", "absx", dll + 1)
    a.op("lda", "#", dl & 0xFF); a.op("sta", "absx", dll + 2)
    a.op("inx"); a.op("inx"); a.op("inx"); a.op("cpx", "#", 48); a.op("bne", "rel", "mkdll")
    a.op("lda", "#", dll >> 8); a.op("sta", "zp", DPPH)
    a.op("lda", "#", dll & 0xFF); a.op("sta", "zp", DPPL)
    a.op("lda", "#", 0x40); a.op("sta", "zp", CTRL)  # DMA on
    a.op("lda", "#", 0); a.op("sta", "abs", CTLSWB)
    a.op("lda", "#", 4); a.op("sta", "zp", AUDC0)
    a.op("lda", "#", 15); a.op("sta", "zp", AUDF0)
    a.op("lda", "#", 8); a.op("sta", "zp", AUDV0)
    a.label("frame")
    a.label("wvb"); a.op("bit", "zp", MSTAT); a.op("bpl", "rel", "wvb")
    a.op("ldx", "#", red)
    a.op("lda", "abs", SWCHA); a.op("and", "#", 0x80); a.op("bne", "rel", "notright"); a.op("ldx", "#", blue)
    a.label("notright")
    a.op("lda", "zp", INPT4); a.op("bmi", "rel", "nofire"); a.op("ldx", "#", green)
    a.label("nofire")
    a.op("stx", "zp", BACKGRND)
    a.label("wvis"); a.op("bit", "zp", MSTAT); a.op("bmi", "rel", "wvis")
    a.op("jmp", "abs", "frame")
    a.label("nmi"); a.op("rti")
    code = a.assemble()
    rom = bytearray(32768)
    rom[:len(code)] = code
    start, nmi = a.labels["start"], a.labels["nmi"]
    rom[0x7FF8] = 0xFF  # the 7800 BIOS's signature check region flag (no BIOS here)
    rom[0x7FF9] = 0x87  # $8000 up is the cartridge, a 7800 game
    rom[0x7FFA:0x7FFC] = bytes([nmi & 0xFF, nmi >> 8])
    rom[0x7FFC:0x7FFE] = bytes([start & 0xFF, start >> 8])
    rom[0x7FFE:0x8000] = bytes([nmi & 0xFF, nmi >> 8])
    if not header:
        return bytes(rom)
    h = bytearray(128)
    h[0] = 1
    h[1:10] = b"ATARI7800"
    name = b"Stella PS5 test 7800"
    h[17:17 + len(name)] = name
    h[49:53] = len(rom).to_bytes(4, "big")
    h[53:55] = b"\0\0"  # a plain 32 KiB cartridge
    h[55] = 1  # left port: a joystick
    h[56] = 1
    h[57] = 0  # NTSC
    h[100:128] = b"ACTUAL CART DATA STARTS HERE"
    return bytes(h + rom)


# ---- Atari Lynx: a BS93 homebrew (.o, loaded at $0200 and run there; no cartridge, no boot ROM code). Mikey shows a
# frame buffer of colour 0, whose palette entry the program sets each frame: red, green while A is held, blue while B is
# (Suzy's JOYSTICK register).
def romlynx(kind):
    DISPADRL, DISPADRH, DISPCTL, PBKUP = 0xFD94, 0xFD95, 0xFD92, 0xFD93
    TIM0BKUP, TIM0CTLA, TIM2BKUP, TIM2CTLA = 0xFD00, 0xFD01, 0xFD08, 0xFD09
    GREEN0, BLUERED0, JOYSTICK, MAPCTL, VCOUNT_CUR = 0xFDA0, 0xFDB0, 0xFCB0, 0xFFF9, 0xFD0A
    buf = 0xC000
    a = Asm(0x0200)
    a.label("start")
    a.op("sei"); a.op("cld"); a.op("ldx", "#", 0xFF); a.op("txs")
    a.op("lda", "#", 0x00); a.op("sta", "abs", MAPCTL)
    # clear the frame buffer ($C000-$DFFF) to colour 0
    a.op("lda", "#", buf >> 8); a.op("sta", "zp", 0x81); a.op("lda", "#", 0); a.op("sta", "zp", 0x80)
    a.op("ldy", "#", 0)
    a.label("clr")
    a.op("lda", "#", 0)
    a.byte(0x91, 0x80)  # sta ($80),y
    a.op("iny"); a.op("bne", "rel", "clr")
    a.op("inc", "zp", 0x81); a.op("lda", "zp", 0x81); a.op("cmp", "#", 0xE0); a.op("bne", "rel", "clr")
    # the display: timers 0 (lines) and 2 (frames) as the boot ROM sets them, DMA on, 4 bits a pixel
    a.op("lda", "#", 0x9E); a.op("sta", "abs", TIM0BKUP); a.op("lda", "#", 0x18); a.op("sta", "abs", TIM0CTLA)
    a.op("lda", "#", 0x68); a.op("sta", "abs", TIM2BKUP); a.op("lda", "#", 0x1F); a.op("sta", "abs", TIM2CTLA)
    a.op("lda", "#", 0x29); a.op("sta", "abs", PBKUP)
    a.op("lda", "#", buf & 0xFF); a.op("sta", "abs", DISPADRL); a.op("lda", "#", buf >> 8); a.op("sta", "abs", DISPADRH)
    a.op("lda", "#", 0x0D); a.op("sta", "abs", DISPCTL)
    a.label("loop")
    a.op("ldx", "#", 0x0F); a.op("ldy", "#", 0x00)  # red: BLUERED0 low nibble = red, GREEN0 = 0
    a.op("lda", "abs", JOYSTICK); a.op("and", "#", 0x01); a.op("beq", "rel", "noa")
    a.op("ldx", "#", 0x00); a.op("ldy", "#", 0x0F)  # green
    a.label("noa")
    a.op("lda", "abs", JOYSTICK); a.op("and", "#", 0x02); a.op("beq", "rel", "nob")
    a.op("ldx", "#", 0xF0); a.op("ldy", "#", 0x00)  # blue
    a.label("nob")
    a.op("stx", "abs", BLUERED0); a.op("sty", "abs", GREEN0)
    a.op("jmp", "abs", "loop")
    code = a.assemble()
    size = len(code) + 10
    return bytes([0x80, 0x08, 0x02, 0x00, size >> 8, size & 0xFF]) + b"BS93" + code


def main():
    out = sys.argv[1]
    kind = sys.argv[2] if len(sys.argv) > 2 else "ntsc"
    ext = os.path.splitext(out)[1].lower()
    if kind == "5200" or ext == ".a52":
        data = rom5200(kind)
    elif kind in ("7800", "7800bin") or ext == ".a78":
        data = rom7800(kind, header=kind != "7800bin")
    elif kind == "lynx" or ext == ".o":
        data = romlynx(kind)
    elif kind in ("ntsc", "pal", "paddles", "sound"):
        data = rom2600(kind)
    else:
        sys.exit(f"make_test_rom.py: unknown kind {kind} for {out}")
    with open(out, "wb") as f:
        f.write(data)
    print(f"{out}: {len(data)} bytes")


if __name__ == "__main__":
    main()
