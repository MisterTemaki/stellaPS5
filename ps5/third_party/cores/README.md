# The cores of the other Atari systems

Stella PS5 plays the Atari 2600 with Stella 7.0 (the repository's own `src/`, compiled from its libretro port) and
three more Atari systems with the cores below, all four in one `eboot.bin` (`ps5/cores.mk` says how they are kept
apart). Each folder is the core's repository at the commit named, cut down to what its libretro build compiles plus
its licence and README; **nothing in them is changed**. They are compiled with their own `Makefile.common`'s source
list and their libretro build's flags.

| System | Core | Repository | Commit | Licence |
|---|---|---|---|---|
| Atari 5200 | **a5200** | [github.com/libretro/a5200](https://github.com/libretro/a5200) | `40c6f2f1ad4a3145b328d5baaf010fae6c7e752b` (2026-08-01) | GPL-2.0-or-later (`a5200/License.txt`); `emu/altirra_5200_os.c`, Altirra's 5200 BIOS replacement (Avery Lee): all-permissive |
| Atari 7800 | **ProSystem** | [github.com/libretro/prosystem-libretro](https://github.com/libretro/prosystem-libretro) | `8a88014287c7a01cd568067e5a557d0a2b2a051f` (2026-08-22) | GPL-2.0-or-later (`prosystem/License.txt`); Ron Fries' TIA and POKEY sound (`core/Tia.c`, `core/Pokey.c`): LGPL-2.0; Osman Celimli's BupChip (`bupboop/`): zlib |
| Atari Lynx | **Gearlynx** | [github.com/drhelius/Gearlynx](https://github.com/drhelius/Gearlynx) | `77901f3d3b318a12fdf71dd3ebb64048f0070ada` (2026-10-25) | GPL-3.0-or-later (`gearlynx/LICENSE`); miniz (`platforms/shared/dependencies/miniz`; Rich Geldreich, RAD Game Tools, Valve Software): MIT, with parts in the public domain |

Gearlynx is GPL-3.0-or-later, which GPL-2.0-or-later code (Stella, a5200, ProSystem) can be combined with: the app
as a whole is therefore distributed under the **GNU GPL, version 3 or later** (the LGPL-2.0 sound code may be used
under the GPL, its section 3). No core here is GPL-2.0-only.

**Where "or later" comes from for a5200 and ProSystem.** Their `License.txt` is the bare text of the GPL version 2,
which says nothing about later versions, and their libretro glue (`a5200/libretro/`, `prosystem/core/libretro*.c`,
`libretro_core_options*.h`) and many headers carry no notice at all. The "or later" rests on the emulator files' own
headers -- Atari800's and Greg Stanton's sources say "either version 2 of the License, or (at your option) any later
version" -- and, for the files that name no version, on section 9 of the GPL 2 ("If the Program does not specify a
version number of this License, you may choose any version ever published by the Free Software Foundation"). No file
in either core says "version 2 only". The combination is therefore distributed as **GPL-3.0-or-later**; the app's
`licenses/` folder carries the GPL 3 (`COPYING.txt`) and `NOTICES.txt` with the notices of the permissive parts
(miniz's MIT, BupChip's zlib, Ron Fries' LGPL notice, Altirra's).

## Why these

- **Atari 5200: a5200**, not libretro-atari800. Both are the Atari800 project's emulation (ANTIC, GTIA, POKEY,
  the 6502), so their accuracy is the same for 5200 games. a5200 is that emulation cut down to the 5200 alone, as a
  core of its own: no 400/800/XL/XE machine, disk drives or SIO around it (a fraction of the code), the 5200's analog
  joystick read **proportionally** from the left stick (with deadzone and sensitivity options), the keypad (and an
  on-screen keypad of its own), the high-fidelity POKEY, frame blending, and **Altirra's free 5200 BIOS built in**, so
  most games start without the console's `5200.rom` (it is used when it is in `/data/stella/bios`). It is maintained
  in libretro (2026). libretro-atari800 needs the 5200 BIOS, starts in the computer modes unless told otherwise and
  carries the whole 8-bit computer.
- **Atari 7800: ProSystem** (prosystem-libretro) is the libretro core for the 7800: Greg Stanton's ProSystem with
  the libretro team's work (a cartridge database, BupChip sound, a dual-stick option). Its BIOS is
  optional (it starts games directly), and it is small C, maintained in libretro (2026). The app gives it an empty
  system folder of its own (`/data/stella/cores/prosystem`), so it never loads a 7800 BIOS: with a BIOS there, the BIOS
  used for one game stayed switched on for the next (ProSystem keeps that in a static), and a PAL game started after
  an NTSC one ran wrong.
- **Atari Lynx: Gearlynx**, not Beetle Lynx. Beetle Lynx (Mednafen's Lynx, i.e. Handy) is older and less accurate;
  Gearlynx is a new emulator (2025-) written for accuracy, that passes the Lynx test suites, is actively developed
  (the commit above is from October 2026), runs on one thread, rotates the picture of the games made for a sideways
  Lynx from its own database, loads `.lnx`, `.lyx` and BS93 `.o` homebrews, and is C++11 with no other dependency.
  It needs the Lynx boot ROM (`lynxboot.img`, 512 bytes; Gearlynx shows a "no BIOS" screen without it, so the app
  checks it first and says what to copy where). Its GPL-3.0 makes the whole app GPL-3.0.

## How they live together

Every core defines `retro_init`, `retro_run`... Each core's sources are compiled with `frontend/fe_retro_prefix.h`
forced in and its prefix (`a52_`, `a78_`, `lnx_`; Stella's are `st_`), which renames its libretro entry points; the
frontend calls them through a table (`frontend/fe_cores.h`). Each core's objects are then linked into one
(`ld -r`) and every other strong global name in it is made local (`tools/localize_core.sh`), so two cores can have a
function of the same name (each has its own copy of libretro-common). C++'s weak symbols can't be hidden that way:
Gearlynx's classes are renamed instead (`frontend/fe_gearlynx_names.h`), and `tools/check_core_symbols.py` stops the
build if two parts of the eboot define one name.
