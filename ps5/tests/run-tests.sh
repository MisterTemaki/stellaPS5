#!/bin/bash
# Stella PS5 host tests: the real port code (main-boot, shims, frontend, the four cores -- Stella, a5200, ProSystem,
# Gearlynx --; the installer and the helper) on Linux, with the PS5 calls faked by host/sce_host.cpp. A scripted pad
# drives the app; flips are de-tiled to PPM and checked. The games are tiny test ROMs written for these tests
# (tests/make_test_rom.py): the Atari 2600 one shows 12 bands, red, each turning green while what it watches is on
# (tests/check_bands.py); the 5200, 7800 and Lynx ones are red, green while the fire button is held, blue while the
# stick is right (Lynx: B).
set -u
cd "$(dirname "$0")/.."
BIN=$PWD/build/host/stella-ps5-host
INSTALLER=$PWD/build/host/stella-ps5-installer
HELPER=$PWD/build/host/stella-ps5-helper
# ports nobody listens on, so the app's ordinary runs find no helper and no ELF loader
export STELLAPS5_HELPER_PORT=$((20000 + RANDOM % 10000)) STELLAPS5_ELFLDR_PORT=$((30000 + RANDOM % 10000)) STELLAPS5_JB_NO_OTHERS=1
# debug logs are off by default; the tests read boot.log, so they turn the default on (group 25 checks it off)
export STELLAPS5_LOGS_DEFAULT=1
CHECK="python3 $PWD/tests/check_ppm.py"
RECT="python3 $PWD/tests/check_rect.py"
BANDS="python3 $PWD/tests/check_bands.py"
ROM="python3 $PWD/tests/make_test_rom.py"
WORK=${WORK:-$(mktemp -d)}
ONLY=${ONLY:-}
PASS=0
FAIL=0

# pad bits
CROSS=4000; CIRCLE=2000; TRIANGLE=1000; SQUARE=8000; UP=10; DOWN=40; RIGHT=20; LEFT=80; L1=400; R1=800; L2=100; R2=200
L3R3=6; R3=4; OPTIONS=8; TOUCHPAD=100000
L2UP=$(printf %x $((0x$L2 | 0x$UP))); L2DOWN=$(printf %x $((0x$L2 | 0x$DOWN))); L2R2=$(printf %x $((0x$L2 | 0x$R2)))
# the 2600 test game's bands (check_bands.py): fire, up, down, left, right, P2 fire, P2 left, Reset, Select, B&W, diff A, diff A
IDLE=RRRRRRRRRRRR
# pause menu: L3+R3, then Up from "Resume" wraps to "Quit Stella PS5"
QUITAT() { echo "$1:$L3R3;$(($1 + 2)):0;$(($1 + 10)):$UP;$(($1 + 12)):0;$(($1 + 20)):$CROSS;$(($1 + 22)):0"; }
# the shelf: Options, then Cross on "Quit"
SHELFQUIT_AT() { echo "$1:$OPTIONS;$(($1 + 2)):0;$(($1 + 10)):$CROSS;$(($1 + 12)):0"; }
# buttons pressed one after the other from frame $1, 8 frames apart
SEQ() { local f=$1 out="" b; shift; for b in "$@"; do out="$out;$f:$b;$((f + 2)):0"; f=$((f + 8)); done; echo "${out#;}"; }
# a button pressed $2 times from frame $1, 6 frames apart (the settings screen's rows)
REP() { local f=$1 n=$2 b=$3 out="" i; for i in $(seq 1 $n); do out="$out;$f:$b;$((f + 2)):0"; f=$((f + 6)); done; echo "${out#;}"; }

ok() { echo "  ok: $*"; PASS=$((PASS + 1)); }
bad() { echo "  FAIL: $*"; FAIL=$((FAIL + 1)); }
expect() { if eval "$1"; then ok "$2"; else bad "$2"; fi; }
want() { [ -z "$ONLY" ] || [[ " $ONLY " == *" $1 "* ]]; }

newroot() {
	local t=$WORK/$1
	rm -rf "$t" && mkdir -p "$t/root/roms" "$t/dump"
	echo "shader=0" >"$t/root/stella-ps5.ini" # the plain picture: the colour checks expect it (group 21 tests the shaders)
	echo "$t"
}

run() { # dir pad dumps [args...]
	local t=$1 pad=$2 dumps=$3
	shift 3
	STELLAPS5_HOST_OFFLINE=${OFFLINE-1} STELLAPS5_COVER_URL=${COVER_URL-} \
	STELLAPS5_ROOT=$t/root STELLAPS5_HOMEBREW=$t/homebrew STELLAPS5_HOST_DUMP_DIR=$t/dump STELLAPS5_HOST_DUMP=$dumps \
		STELLAPS5_HOST_MAX_FLIPS=${MAXFLIPS-5000} STELLAPS5_HOST_PAD=$pad ASAN_OPTIONS=detect_leaks=0 timeout 180 "$BIN" "$@" >"$t/out.txt" 2>&1
	echo $?
}
nosan() { expect "! grep -q 'runtime error\|AddressSanitizer' $1/out.txt" "no sanitizer reports"; }
waitfor() { for i in $(seq 1 80); do grep -q "$2" "$1" 2>/dev/null && return 0; sleep 0.1; done; return 1; }
stop_helpers() { pkill -f 'build/host/stella-ps5-(installer|helper)' 2>/dev/null; pkill -f 'received.elf' 2>/dev/null; sleep 0.3; }
# the No-Intro table's CRC of a title (frontend/data/nointro.tsv): system id, title
crc_of() { grep -P "^$1\t[0-9A-F]{8}\t\Q$2\E$" frontend/data/nointro.tsv | cut -f2 | head -1; }

if want 1; then
echo "== 1. an Atari 2600 game from the command line: Stella's picture, the joystick's fire, quick save (L2 + Up), quit from the pause menu"
T=$(newroot t1)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;200:$L2UP;205:0;$(QUITAT 220)" "90,130" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$RECT $T/dump/flip00090.ppm $T/root/logs/boot.log red sides >/dev/null" "red picture where the log says, black beside it"
expect "$CHECK $T/dump/flip00090.ppm 960 540 177 40 39 >/dev/null" "the backdrop is NTSC colour \$44 of Stella's standard palette"
expect "$BANDS $T/dump/flip00090.ppm $IDLE >/dev/null && $BANDS $T/dump/flip00130.ppm GRRRRRRRRRRR >/dev/null" "Cross -> the joystick's fire button (the first band turns green), nothing else"
expect "grep -q 'running \"test\" (Atari 2600), 60\.0' $T/root/logs/boot.log" "a 60 Hz NTSC game"
expect "grep -qE 'Stella: \"test\", md5 [0-9a-f]{32}, NTSC( \*)?, bankswitching 4K; controllers Joystick / Joystick; difficulty B / B, color' $T/root/logs/boot.log" "Stella's detection: NTSC, 4K, two joysticks, the switches as the console starts"
expect "grep -q 'picture 160x228 (228 lines, aspect 1.4035, Fit to screen)' $T/root/logs/boot.log" "Stella's TV picture (160x228, its double-width pixels), fit to the screen"
expect "[ -f $T/root/states/Atari2600/test.state1 ] && grep -q 'save state 1: ok' $T/root/logs/boot.log" "L2 + Up wrote states/Atari2600/test.state1"
expect "grep -q 'cores: Stella (Atari 2600), a5200 (Atari 5200), ProSystem (Atari 7800), Gearlynx (Atari Lynx)' $T/root/logs/boot.log" "the four cores are built in"
nosan "$T"
fi

if want 2; then
echo "== 2. the joystick, the console's switches and the second controller reach Stella"
T=$(newroot t2)
$ROM "$T/root/roms/test.a26" >/dev/null
# D-pad up, down, left, right; the left stick left; Square (fire, second button); Options (Game Reset), the touchpad
# (Game Select) held; L1, R1, Triangle pressed once (the switches stay put); the second pad's Cross and left
P="0:0;100:$UP;120:0;130:$DOWN;150:0;160:$LEFT;180:0;190:$RIGHT;210:0;220:0,0,128,128,128;240:0;250:$SQUARE;270:0"
P="$P;280:$OPTIONS;300:0;310:$TOUCHPAD;330:0;340:$L1;342:0;360:$R1;362:0;380:$TRIANGLE;382:0"
# then the pause menu: Down six times to "Left difficulty", Cross flips it back to B; Circle resumes
P="$P;$(SEQ 520 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS $CIRCLE)"
rc=$(STELLAPS5_HOST_PADS=2 STELLAPS5_HOST_PAD2="0:0;420:$CROSS;450:0;470:$LEFT;500:0" run "$T" "$P;$(QUITAT 700)" "110,140,170,200,230,260,290,320,400,440,490,560,680" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
D=$T/dump
expect "$BANDS $D/flip00110.ppm RGRRRRRRRRRR >/dev/null && $BANDS $D/flip00140.ppm RRGRRRRRRRRR >/dev/null && $BANDS $D/flip00170.ppm RRRGRRRRRRRR >/dev/null && $BANDS $D/flip00200.ppm RRRRGRRRRRRR >/dev/null" "the D-pad: up, down, left, right"
expect "$BANDS $D/flip00230.ppm RRRGRRRRRRRR >/dev/null" "the left stick: left"
expect "$BANDS $D/flip00260.ppm GRRRRRRRRRRR >/dev/null" "Square: fire too"
expect "$BANDS $D/flip00290.ppm RRRRRRRGRRRR >/dev/null && $BANDS $D/flip00320.ppm RRRRRRRRGRRR >/dev/null" "Options: Game Reset; the touchpad: Game Select (while held)"
expect "$BANDS $D/flip00400.ppm RRRRRRRRRGGG >/dev/null" "L1, R1, Triangle: the left and right difficulty switches on A, the TV type on B&W (they stay put)"
expect "grep -q 'left difficulty A' $T/root/logs/boot.log && grep -q 'right difficulty A' $T/root/logs/boot.log && grep -q 'TV type B&W' $T/root/logs/boot.log" "and the log says so"
expect "$BANDS $D/flip00440.ppm RRRRRGRRRGGG >/dev/null && $BANDS $D/flip00490.ppm RRRRRRGRRGGG >/dev/null" "the second signed-in controller is the right joystick (its fire, its left)"
expect "$BANDS $D/flip00680.ppm RRRRRRRRRGRG >/dev/null && grep -q 'left difficulty B' $T/root/logs/boot.log" "the pause menu's Left difficulty flips it back to B"
nosan "$T"
fi

if want 3; then
echo "== 3. shelf -> game -> back to the shelf -> the game again -> back -> quit"
T=$(newroot t3)
$ROM "$T/root/roms/Test Game (USA).a26" >/dev/null
mkdir -p "$T/root/roms/Action"
P="0:0;40:$CROSS;42:0;150:$L3R3;152:0;160:$UP;162:0;164:$UP;166:0;170:$CROSS;172:0"
P="$P;200:$CROSS;202:0;300:$CROSS;340:0;$(SEQ 360 $L3R3 $UP $UP $CROSS);420:$OPTIONS;422:0;430:$CROSS;432:0"
rc=$(run "$T" "$P" "20,35,120,190,330")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \$(grep -c 'running \"Test Game (USA)\" (Atari 2600)' $T/root/logs/boot.log) = 2 ] && $BANDS $T/dump/flip00330.ppm GRRRRRRRRRRR >/dev/null" "started a second time in the same session (Stella made anew): it runs, Cross fires"
expect "[ -f $T/dump/flip00020.ppm ]" "the shelf was shown"
expect "$CHECK $T/dump/flip00120.ppm 960 540 red >/dev/null" "the game picked on the shelf runs"
expect "grep -q 'last_rom=.*Test Game (USA).a26' $T/root/stella-ps5.ini" "the shelf remembers the last game"
expect "grep -q 'game closed' $T/root/logs/boot.log" "back to the list closed the game"
nosan "$T"
fi

if want 4; then
echo "== 4. a 50 Hz (PAL) game runs at 50 frames a second, in PAL colours"
T=$(newroot t4)
$ROM "$T/root/roms/pal.a26" pal >/dev/null
start=$(date +%s.%N)
rc=$(run "$T" "0:0;$(QUITAT 300)" "90" "$T/root/roms/pal.a26")
end=$(date +%s.%N)
secs=$(python3 -c "print(round($end - $start, 2))")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "python3 -c 'import sys; sys.exit(0 if 5.0 <= $secs <= 9.5 else 1)'" "300 frames took ${secs}s (~6 s at 50 fps)"
expect "grep -q 'running \"pal\" (Atari 2600), 50.0' $T/root/logs/boot.log && grep -qE 'Stella: \"pal\", md5 [0-9a-f]*, PAL' $T/root/logs/boot.log" "Stella detects the PAL game (312 lines) and runs it at 50 Hz"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && ! $CHECK $T/dump/flip00090.ppm 960 540 177 40 39 20 >/dev/null" "in the PAL palette (\$64: red, not NTSC's \$44)"
nosan "$T"
fi

if want 5; then
echo "== 5. a game for paddles: the screen says so (the pad stands in for joysticks only), and the game starts all the same"
T=$(newroot t5)
$ROM "$T/root/roms/paddles.a26" paddles >/dev/null
$ROM "$T/root/roms/joy.a26" >/dev/null
rc=$(STELLAPS5_HOST_REALTIME=1 run "$T" "0:0;$(QUITAT 620)" "60,600" "$T/root/roms/paddles.a26")
rc2=$(run "$T" "0:0;$(QUITAT 300)" "" "$T/root/roms/joy.a26")
cp $T/root/logs/boot.prev.log $T/paddles.log 2>/dev/null
expect "[ $rc = 0 ] && [ $rc2 = 0 ]" "exit codes 0 (got $rc, $rc2)"
expect "grep -q 'controllers Paddles / Joystick' $T/paddles.log && grep -q \"needs a controller the pad doesn't stand in for yet: Paddles\" $T/paddles.log" "Stella takes it for a paddle game: logged"
expect "$CHECK $T/dump/flip00600.ppm 960 540 red >/dev/null" "the game runs"
expect "python3 - $T/dump/flip00060.ppm $T/dump/flip00600.ppm <<'PY'
import sys
from PIL import Image
def white(p):
    a = Image.open(p).convert('RGB')
    return sum(1 for y in range(0, 1080, 2) for x in range(0, 1920, 2) if min(a.getpixel((x, y))) > 200)
sys.exit(0 if white(sys.argv[1]) > 200 and white(sys.argv[2]) == 0 else 1)
PY" "the message is on the screen at first (white text), gone 10 s later"
expect "! grep -q 'needs a controller' $T/root/logs/boot.log" "a joystick game: no message"
nosan "$T"
fi

if want 6; then
echo "== 6. integer scale and scanlines from the settings file; load state with L2 + Down"
T=$(newroot t6)
$ROM "$T/root/roms/test.a26" >/dev/null
printf 'scale=1\nscanlines=1\n' >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;60:$L2UP;62:0;80:$L2DOWN;82:0;$(QUITAT 100)" "50" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'Integer scale, scanlines' $T/root/logs/boot.log" "integer scale with scanlines"
expect "$RECT $T/dump/flip00050.ppm $T/root/logs/boot.log red sides >/dev/null" "red picture where the log says"
expect "python3 - $T/dump/flip00050.ppm <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
reds = {px[(y * w + 960) * 3] for y in range(300, 800)}
top = max(reds)
sys.exit(0 if top >= 150 and any(0.35 * top <= r <= 0.65 * top for r in reds) else 1)
PY" "scanline rows are darker"
expect "grep -q 'load state 1: ok' $T/root/logs/boot.log" "L2 + Down loaded the state"
nosan "$T"
fi

if want 7; then
echo "== 7. a zipped game; the sound stays fed and near the 60 ms target; the TIA's tone comes out clean, at its pitch"
T=$(newroot t7)
$ROM "$T/game.a26" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'game.a26'); z.close()" "$T/root/roms/game.zip" "$T/game.a26"
rc=$(STELLAPS5_HOST_REALTIME=1 run "$T" "0:0;$(QUITAT 760)" "100" "$T/root/roms/game.zip")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'loading .*game.zip -> game.a26' $T/root/logs/boot.log" "the game inside the zip is loaded"
expect "$CHECK $T/dump/flip00100.ppm 960 540 red >/dev/null" "the zipped game runs"
u2=$(grep -o 'audio queued .*underruns [0-9]*' "$T/root/logs/boot.log" | sed -n 2p | grep -o '[0-9]*$')
u3=$(grep -o 'audio queued .*underruns [0-9]*' "$T/root/logs/boot.log" | sed -n 3p | grep -o '[0-9]*$')
lat=$(grep -o 'audio queued [0-9]* ([0-9]* ms)' "$T/root/logs/boot.log" | sed -n 3p | grep -o '([0-9]*' | tr -d '(')
under=$(( ${u3:-999} - ${u2:-0} ))
expect "[ $under -le 2 ]" "audio underruns over 5 s of play: $under"
expect "[ ${lat:-0} -ge 15 ] && [ ${lat:-0} -le 120 ]" "audio latency ${lat:-?} ms"
nosan "$T"
# the sound itself: the test game's channel 0 plays AUDC 4, AUDF 15 -- 31440 / 32 = 982.5 Hz, a square wave --, Stella's
# 31440 Hz resampled to the PS5's 48 kHz, on both sides
T=$(newroot t7s)
$ROM "$T/root/roms/tone.a26" sound >/dev/null
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_AUDIO_DUMP=$T/audio.raw run "$T" "0:0;$(QUITAT 240)" "" "$T/root/roms/tone.a26")
python3 tests/check_tone.py $T/audio.raw 982.5 982.5 --square --min-snr 12 >$T/tone.txt; trc=$?
expect "[ $rc = 0 ] && [ $trc = 0 ]" "the tone: 982.5 Hz on both sides, a clean square ($(tr '\n' ' ' <$T/tone.txt | cut -c1-160))"
nosan "$T"
fi

if want 8; then
echo "== 8. little video memory: 720p scan-out; memory busy at first: retries"
T=$(newroot t8)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_DIRECT_MAX_MIB=12 run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'scan-out 1280x720' $T/root/logs/boot.log" "fell back to 1280x720"
expect "head -2 $T/dump/flip00090.ppm | grep -q '1280 720'" "the flips are 1280x720"
expect "$RECT $T/dump/flip00090.ppm $T/root/logs/boot.log red sides >/dev/null" "game picture in place (720p)"
T=$(newroot t8b)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_DIRECT_FAIL_FIRST=9 run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'retrying (1)' $T/root/logs/boot.log && grep -q 'scan-out 1920x1080' $T/root/logs/boot.log" "retried, then got 1080p"
fi

if want 9; then
echo "== 9. StellaPS5.elf installs the app (icon, libc.prx and licenses included), stays as the helper, repairs"
T=$(newroot t9)
APP=$T/homebrew/PPSA99927
inst() { # dir -> runs the installer; it stays running as the helper when the port is free
	STELLAPS5_ROOT=$1/root STELLAPS5_HOMEBREW=$1/homebrew STELLAPS5_APPMETA=$1/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >>"$1/inst.txt" 2>&1
}
stop_helpers
inst "$T" &
expect "waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'" "the installer stays running as the helper"
expect "cmp -s $APP/sce_sys/icon0.png app/sce_sys/icon0.png" "icon0.png installed (the Stella PS5 icon)"
expect "cmp -s $APP/sce_sys/pic0.dds app/sce_sys/pic0.dds && cmp -s $APP/sce_sys/pic1.dds app/sce_sys/pic1.dds && cmp -s $APP/sce_sys/param.json app/sce_sys/param.json" "pic0.dds/pic1.dds (background) and param.json installed"
expect "cmp -s $APP/eboot.bin tests/fake-eboot.bin" "eboot.bin installed"
expect "python3 tests/check_art.py app/sce_sys" "the art: pic0.dds/pic1.dds 3840x2160 BC7 (DX10, one mip), the key art encoded (PSNR > 40 dB); icon0.png a 512x512 8-bit RGB PNG, the chosen icon (icon-source.png)"
expect "cmp -s $APP/sce_module/libc.prx tests/fake-libc.prx" "sce_module/libc.prx installed"
expect "cmp -s $APP/licenses/COPYING.txt ../COPYING && cmp -s $APP/licenses/LGPL-2.1.txt app/licenses/LGPL-2.1.txt && cmp -s $APP/licenses/NOTICES.txt app/licenses/NOTICES.txt" "licenses/ installed (COPYING.txt = the GPL 3, LGPL-2.1.txt, NOTICES.txt)"
python3 tools/make_notices.py .. "$T/notices" >/dev/null
expect "diff -r -q app/licenses $T/notices" "app/licenses/ is what tools/make_notices.py makes from the sources (make notices)"
expect "grep -q 'Permission is hereby granted' app/licenses/NOTICES.txt && grep -q 'RAD Game Tools' app/licenses/NOTICES.txt && grep -q 'BupChip' app/licenses/NOTICES.txt" "NOTICES.txt carries miniz's MIT notice and BupChip's zlib notice"
expect "grep -q 'Stella PS5 [0-9.]* installed. Open it from the Stella PS5 icon' $T/inst.txt" "install notification says to open the icon"
expect "! ls $APP/*.part $APP/sce_sys/*.part $APP/sce_module/*.part 2>/dev/null | grep -q ." "no .part files left"
inst "$T"
expect "grep -q 'is up to date' $T/root/logs/installer.log && ! grep -q 'wrote' $T/root/logs/installer.log" "sent again: nothing rewritten"
expect "grep -q 'another helper is running' $T/root/logs/installer.log" "sent again: the second copy leaves the helper to the first"
echo broken > $APP/sce_sys/icon0.png
inst "$T"
expect "cmp -s $APP/sce_sys/icon0.png app/sce_sys/icon0.png" "a damaged icon is put back"
META=$T/appmeta/PPSA99927
mkdir -p "$META" && echo old > "$META/pic0.png" && echo old > "$META/icon0.png" && cp app/sce_sys/param.json "$META/"
echo old > "$APP/sce_sys/pic0.png"
inst "$T"
expect "cmp -s $META/pic0.dds app/sce_sys/pic0.dds && cmp -s $META/pic1.dds app/sce_sys/pic1.dds && cmp -s $META/icon0.png app/sce_sys/icon0.png" "appmeta gets the background (pic0/pic1.dds) and icon"
expect "[ ! -f $META/pic0.png ] && [ ! -f $APP/sce_sys/pic0.png ]" "an old pic0.png is removed"
expect "grep -q 'Home screen art updated' $T/inst.txt" "the notification says the home screen art changed"
stop_helpers
expect "[ \$(find $T -path '*PPSA99009*' -o -path '*PPSA99010*' -o -path '*PPSA99011*' -o -path '*PPSA99013*' -o -path '*PPSA99015*' -o -path '*PPSA99925*' -o -path '*PPSA99203*' | wc -l) = 0 ]" "nothing written for our other ports (Snes9x, Mesen2, Genesis Plus GX, NES FCEUmm, Media Player Classic, Beetle PCE, PS5SX2)"
fi

if want 10; then
echo "== 10. covers per system: download, git-symlink, name by CRC, your own cover first, 404 remembered; each system's card"
T=$(newroot t10)
R26=Atari_-_2600; R78=Atari_-_7800; RLX=Atari_-_Lynx
mkdir -p "$T/srv/$R26/Named_Boxarts" "$T/srv/$R78/Named_Boxarts" "$T/srv/$RLX/Named_Boxarts" "$T/root/covers/Atari2600" \
	"$T/root/roms/Atari2600" "$T/root/roms/Atari7800" "$T/root/roms/AtariLynx"
python3 - "$T/srv" "$T/root/covers" "$R26" "$R78" "$RLX" <<'PY'
import sys
from PIL import Image
srv, covers, a26, a78, lnx = sys.argv[1:6]
m = f"{srv}/{a26}/Named_Boxarts"
Image.new('RGB', (512, 357), (255, 0, 0)).save(m + '/Combat (Europe).png')
Image.new('RGB', (512, 357), (0, 255, 0)).save(m + '/Adventure (Europe).png')
open(m + '/Adventure (USA).png', 'w').write('Adventure (Europe).png')
Image.new('RGB', (512, 357), (255, 255, 0)).save(m + '/Joust (USA).png')
Image.new('RGB', (360, 512), (0, 0, 255)).save(covers + '/Atari2600/Joust (USA).png')
Image.new('RGB', (360, 512), (255, 0, 255)).save(f"{srv}/{a78}/Named_Boxarts/Centipede (USA).png")
Image.new('RGB', (360, 512), (0, 255, 255)).save(f"{srv}/{lnx}/Named_Boxarts/Klax (World).png")
PY
for n in "Combat (Europe)" "Berzerk (Japan, USA) (En)" "Joust (USA)"; do $ROM "$T/root/roms/Atari2600/$n.a26" >/dev/null; done
$ROM "$T/root/roms/Atari7800/Centipede (USA).a78" >/dev/null
$ROM "$T/root/roms/AtariLynx/Klax (World).o" >/dev/null
$ROM "$T/root/roms/Atari2600/adv.a26" >/dev/null
# the CRC of the ROM names it (No-Intro's names)
python3 tests/forge_crc.py "$T/root/roms/Atari2600/adv.a26" $(crc_of a26 "Adventure (USA)") >/dev/null
PORT=18080
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
# shelf order (by title): Adventure, Berzerk, Centipede, Combat, Joust, Klax
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" STELLAPS5_HOST_REALTIME=1 run "$T" \
	"0:0;180:$RIGHT;182:0;230:$RIGHT;232:0;280:$RIGHT;282:0;330:$RIGHT;332:0;380:$RIGHT;382:0;430:$OPTIONS;432:0;440:$CROSS;442:0" "170,220,270,320,370,420")
kill $SRVPID 2>/dev/null
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'adv.a26 \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log" "adv.a26 named by its CRC"
expect "[ -f '$T/root/covers/Atari2600/Combat (Europe).png' ]" "a 2600 cover saved in covers/Atari2600"
expect "[ -f '$T/root/covers/Atari7800/Centipede (USA).png' ] && [ -f '$T/root/covers/AtariLynx/Klax (World).png' ]" "a 7800 and a Lynx cover from their repositories, in covers/Atari7800 and covers/AtariLynx"
expect "[ -f '$T/root/covers/Atari2600/Berzerk (Japan, USA) (En).missing' ]" "a 404 is remembered (.missing)"
expect "grep -q 'is a link to Adventure (Europe).png' $T/root/logs/boot.log" "a git-symlink cover is followed"
expect "! grep -q 'GET .*Joust' $T/root/logs/boot.log" "your own cover (covers/Atari2600/<ROM name>.png): nothing downloaded for it"
expect "$CHECK $T/dump/flip00170.ppm 960 420 0 255 0 >/dev/null" "Adventure shows the linked (green) cover"
expect "! $CHECK $T/dump/flip00220.ppm 960 420 255 0 0 >/dev/null 2>&1 && ! $CHECK $T/dump/flip00220.ppm 960 420 0 255 0 >/dev/null 2>&1" "no cover online: a placeholder card"
expect "$CHECK $T/dump/flip00270.ppm 960 420 255 0 255 >/dev/null" "Centipede shows the 7800's (magenta) cover"
expect "$CHECK $T/dump/flip00320.ppm 960 420 255 0 0 >/dev/null" "Combat shows the downloaded (red) cover"
expect "$CHECK $T/dump/flip00370.ppm 960 420 0 0 255 >/dev/null" "Joust shows your own (blue) cover, not the server's"
expect "$CHECK $T/dump/flip00420.ppm 960 420 0 255 255 >/dev/null" "Klax shows the Lynx's (cyan) cover"
nosan "$T"
# the placeholder card of each system: its own emblem (drawn here: a one-button joystick, a keypad controller, a
# two-button joystick, a handheld), top right; the 2600 joystick's red button, the handheld's green screen
T=$(newroot t10cards)
mkdir -p "$T/root/roms/Atari2600" "$T/root/roms/Atari5200" "$T/root/roms/Atari7800" "$T/root/roms/AtariLynx"
$ROM "$T/root/roms/Atari2600/Alpha Game (USA).a26" >/dev/null
$ROM "$T/root/roms/Atari5200/Bravo Game (USA).a52" >/dev/null
$ROM "$T/root/roms/Atari7800/Charlie Game (USA).a78" >/dev/null
$ROM "$T/root/roms/AtariLynx/Delta Game (USA).o" >/dev/null
rc=$(run "$T" "0:0;70:$RIGHT;72:0;120:$RIGHT;122:0;170:$RIGHT;172:0;$(SHELFQUIT_AT 230)" "60,110,160,210")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "python3 - $T/dump/flip00060.ppm $T/dump/flip00110.ppm $T/dump/flip00160.ppm $T/dump/flip00210.ppm <<'PY'
import sys
from PIL import Image, ImageChops, ImageStat
crops = [Image.open(p).convert('RGB').crop((1080, 150, 1320, 280)) for p in sys.argv[1:5]]
for i in range(4):
    for j in range(i + 1, 4):
        if max(ImageStat.Stat(ImageChops.difference(crops[i], crops[j])).mean) < 6:
            sys.exit(1)
PY" "four systems, four different emblems"
expect "$CHECK $T/dump/flip00060.ppm 1165 227 red >/dev/null && $CHECK $T/dump/flip00210.ppm 1200 227 green >/dev/null" "the 2600's joystick (its red button), the Lynx's handheld (its green screen)"
expect "python3 - $T/dump/flip00060.ppm <<'PY'
import sys
from PIL import Image
a = Image.open(sys.argv[1]).convert('RGB')
white = sum(1 for y in range(160, 275) for x in range(1100, 1320) if min(a.getpixel((x, y))) >= 225)
sys.exit(0 if white == 0 else 1)
PY" "no title letter over the emblem"
nosan "$T"
fi

if want 11; then
echo "== 11. offline: no download is tried, the shelf still works"
T=$(newroot t11)
$ROM "$T/root/roms/Combat (Europe).a26" >/dev/null
rc=$(run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "20")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'not connected' $T/root/logs/boot.log && ! grep -q 'GET ' $T/root/logs/boot.log" "no request made without a network"
fi

if want 12; then
echo "== 12. the shelf's system tabs: All games and the systems that have games; Down/Up go through them; the game picked there runs"
T=$(newroot t12)
mkdir -p "$T/root/roms/Atari7800" "$T/root/roms/Atari5200"
$ROM "$T/root/roms/Alpha (USA).a26" >/dev/null
$ROM "$T/root/roms/Atari7800/Beta (USA).a78" >/dev/null
$ROM "$T/root/roms/Atari5200/Delta (USA).a52" >/dev/null
# tabs: All games, Atari 2600, Atari 5200, Atari 7800. Down x4 -> back to All; Up -> Atari 7800; Cross -> Beta
P="0:0;$(REP 30 4 $DOWN);60:$UP;62:0;100:$CROSS;102:0"
rc=$(run "$T" "$P;$(QUITAT 200)" "190")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[shelf\] tab Atari 2600: 1 game(s)' $T/root/logs/boot.log && grep -q '\[shelf\] tab Atari 5200: 1 game(s)' $T/root/logs/boot.log && grep -q '\[shelf\] tab Atari 7800: 1 game(s)' $T/root/logs/boot.log" "a tab per system with games"
expect "[ \$(grep -c '\[shelf\] tab All games: 3 game(s)' $T/root/logs/boot.log) = 1 ] && ! grep -q '\[shelf\] tab Atari Lynx' $T/root/logs/boot.log" "no empty system: Down four times is back at All games"
expect "grep -q 'loading .*Beta (USA).a78' $T/root/logs/boot.log && grep -q 'running \"Beta (USA)\" (Atari 7800)' $T/root/logs/boot.log" "the 7800 game was started from its tab, by ProSystem"
expect "grep -q 'shelf_family=3$' $T/root/stella-ps5.ini" "the tab is remembered"
expect "$CHECK $T/dump/flip00190.ppm 960 540 red >/dev/null" "the game runs"
nosan "$T"
fi

if want 13; then
echo "== 13. settings from the shelf (Triangle); fast forward (R2) and rewind (L2 + R2); the pause menu saves a state"
T=$(newroot t13)
$ROM "$T/root/roms/Alpha (USA).a26" >/dev/null
# Triangle -> settings ("Shader" first, Down -> "Screen size"), Right -> integer scale, Circle -> back; Cross -> play
rc=$(STELLAPS5_HOST_REALTIME=1 run "$T" "0:0;30:$TRIANGLE;32:0;36:$DOWN;38:0;40:$RIGHT;42:0;50:$CIRCLE;52:0;70:$CROSS;72:0;150:$R2;250:0;300:$L2R2;340:0;360:$L3R3;362:0;370:$DOWN;372:0;380:$CROSS;382:0;400:$CIRCLE;402:0;$(QUITAT 440)" "45")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'scale=1' $T/root/stella-ps5.ini" "the settings screen changed the screen size"
expect "grep -q 'fast forward on' $T/root/logs/boot.log && grep -q 'fast forward off' $T/root/logs/boot.log" "R2 held: fast forward"
expect "grep -q 'rewind on' $T/root/logs/boot.log && grep -q 'rewind off' $T/root/logs/boot.log" "L2 + R2 held: rewind"
expect "[ -f '$T/root/states/Atari2600/Alpha (USA).state1' ]" "the pause menu's Save state wrote slot 1"
nosan "$T"
fi

if want 14; then
echo "== 14. the pad is shared with the system (handle 0x809b0081, as on the console)"
T=$(newroot t14)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_PAD_SHARED=1 STELLAPS5_HOST_REALTIME=1 run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 300)" "130" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'using 809b0081 (shared with the system)' $T/root/logs/boot.log" "the system's handle is used"
expect "[ \$(grep -c 'scePadOpen' $T/root/logs/boot.log) = 1 ]" "opened once, not every 2 seconds"
expect "$BANDS $T/dump/flip00130.ppm GRRRRRRRRRRR >/dev/null" "its buttons reach the game (Cross -> fire)"
fi

if want 15; then
echo "== 15. the app asks the helper to let it out of the sandbox"
T=$(newroot t15)
$ROM "$T/root/roms/test.a26" >/dev/null
QUIT=$(QUITAT 30)
STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'Stella helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "the helper said yes"
expect "grep -q 'jailbreak: Stella helper' $T/root/logs/boot.log" "logged before /data was open, written to boot.log"
expect "grep -q 'letting it out' $T/hroot/logs/helper.log" "the helper let the app's process out"
stop_helpers
T=$(newroot t15b)
$ROM "$T/root/roms/test.a26" >/dev/null
STELLAPS5_HOST_JB_TITLE=PPSA99013 STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.a26")
expect "grep -q 'title PPSA99013 is not Stella PS5' $T/hroot/logs/helper.log" "the helper lets out no other title (not even NES FCEUmm PS5)"
stop_helpers
fi

if want 16; then
QUIT=$(QUITAT 30)
echo "== 16. no helper running: the app hands its own helper to the ELF loader, then asks it"
T=$(newroot t16)
$ROM "$T/root/roms/test.a26" >/dev/null
python3 - "$STELLAPS5_ELFLDR_PORT" "$T" <<'PY' &
import os, socket, subprocess, sys
port, t = int(sys.argv[1]), sys.argv[2]
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', port)); s.listen(1); s.settimeout(60)
c, _ = s.accept()
data = b''
while True:
    d = c.recv(65536)
    if not d: break
    data += d
path = t + '/received.elf'
open(path, 'wb').write(data); os.chmod(path, 0o755)
env = dict(os.environ, STELLAPS5_ROOT=t + '/hroot', ASAN_OPTIONS='detect_leaks=0')
p = subprocess.Popen(['timeout', '30', path], env=env, stdout=open(t + '/helper.txt', 'w'), stderr=subprocess.STDOUT)
open(t + '/helper.pid', 'w').write(str(p.pid))
PY
LPID=$!
sleep 0.5
rc=$(run "$T" "$QUIT" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "cmp -s $T/received.elf $HELPER" "the ELF loader got the helper built into the app"
expect "grep -q 'jailbreak: Stella helper (started by the app)' $T/root/logs/boot.log" "then the helper it started let it out"
wait $LPID 2>/dev/null
stop_helpers
fi

if want 17; then
echo "== 17. no /data even so: the screen says what to do; the screen busy at first: retries"
T=$(newroot t17)
rm -rf "$T/root" && echo "not a folder" >"$T/root" # the app can't create its folders there
rc=$(run "$T" "0:0;30:$CROSS;32:0" "20")
expect "[ $rc = 2 ]" "exit code 2 (got $rc)"
expect "grep -q 'no access to /data' $T/out.txt" "logged: no access to /data"
expect "[ -f $T/dump/flip00020.ppm ]" "a message was on the screen"
T=$(newroot t17b)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_VIDEO_BUSY=3 run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \$(grep -c 'sceVideoOutOpen -> .*80290009' $T/root/logs/boot.log) = 3 ] && grep -q 'scan-out 1920x1080' $T/root/logs/boot.log" "VideoOut busy three times, then opened"
expect "grep -q 'splash screen hidden' $T/root/logs/boot.log" "the splash screen is hidden"
fi

if want 18; then
echo "== 18. covers in the background: the helper downloads them while the shelf runs; an older helper: the prefetch"
stop_helpers
T=$(newroot t18)
SRV=$T/srv/Atari_-_2600/Named_Boxarts; mkdir -p "$SRV" "$T/root/covers"
COVER="Combat (Europe).png"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$SRV/$COVER')"
$ROM "$T/root/roms/Combat (Europe).a26" >/dev/null
PORT=18081
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
STELLAPS5_ROOT=$T/root ASAN_OPTIONS=detect_leaks=0 timeout 120 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/root/logs/helper.log" "listening" || sleep 1
URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png"
rc=$(OFFLINE= COVER_URL="$URL" STELLAPS5_HOST_REALTIME=1 run "$T" "0:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "20,280")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'the helper downloads the covers while the app runs: nothing to wait for' $T/root/logs/boot.log && ! grep -q 'restarting' $T/root/logs/boot*.log" "the app starts at once: no download before /data, no restart"
expect "grep -q 'Atari2600/Combat (Europe).png' $T/root/covers/wanted.txt" "the missing cover goes to covers/wanted.txt"
expect "grep -q 'Atari2600/Combat (Europe).png -> 200' $T/root/logs/helper.log && cmp -s '$T/root/covers/Atari2600/$COVER' '$SRV/$COVER'" "the helper downloaded it into covers/Atari2600"
expect "! $CHECK $T/dump/flip00020.ppm 960 420 255 0 0 >/dev/null 2>&1 && $CHECK $T/dump/flip00280.ppm 960 420 255 0 0 >/dev/null" "the shelf showed its card, then the cover once it landed"
expect "grep -q '^0 1 idle' $T/root/covers/progress.txt" "covers/progress.txt: nothing left, 1 fetched"
kill $SRVPID 2>/dev/null
stop_helpers
# an older helper (the list without "covers: background"): the prefetch before /data
T=$(newroot t18b)
mkdir -p "$T/srv/Atari_-_2600/Named_Boxarts" "$T/root/covers"
cp "$SRV/$COVER" "$T/srv/Atari_-_2600/Named_Boxarts/"
$ROM "$T/root/roms/Combat (Europe).a26" >/dev/null
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
printf 'Atari2600/%s\thttp://127.0.0.1:%s/Atari_-_2600/Named_Boxarts/Combat%%20%%28Europe%%29.png\n' "$COVER" $PORT >"$T/wanted.txt"
python3 - "$STELLAPS5_HELPER_PORT" "$T/wanted.txt" <<'PY' &
import socket, struct, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(('127.0.0.1', int(sys.argv[1]))); s.listen(4); s.settimeout(60)
text = open(sys.argv[2], 'rb').read()
end = time.time() + 60
while time.time() < end:
    try:
        c, _ = s.accept()
    except OSError:
        break
    req = b''
    while len(req) < 0xA10:
        d = c.recv(0xA10 - len(req))
        if not d: break
        req += d
    if len(req) == 0xA10:
        magic, cmd, pid, ret = struct.unpack_from('<IiiI', req, 0)
        out = bytearray(req)
        if cmd == 6:
            struct.pack_into('<i', out, 12, len(text)); c.sendall(bytes(out) + text)
        elif cmd == 5:
            struct.pack_into('<i', out, 12, 0); out[16:18] = b'ok'; c.sendall(bytes(out))
    c.close()
PY
OLDPID=$!
sleep 0.5
rc=$(OFFLINE= COVER_URL="$URL" run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[prefetch\] 1 of 1 fetched' $T/root/logs/boot.log && cmp -s '$T/root/covers/Atari2600/$COVER' '$SRV/$COVER'" "an older helper: the cover is prefetched before /data and saved"
kill $OLDPID $SRVPID 2>/dev/null
stop_helpers
fi

if want 19; then
echo "== 19. the system folders: made in roms/ and covers/ at start, and bios/"
T=$(newroot t19)
$ROM "$T/root/roms/Combat (Europe).a26" >/dev/null
rc=$(run "$T" "0:0;60:$OPTIONS;62:0;70:$CROSS;72:0" "50")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
for d in Atari2600 Atari5200 Atari7800 AtariLynx; do
	expect "[ -d $T/root/roms/$d ] && [ -d $T/root/covers/$d ]" "roms/$d and covers/$d made"
done
expect "[ -d $T/root/bios ]" "bios/ made (5200.rom, lynxboot.img go there)"
expect "[ -f $T/dump/flip00050.ppm ]" "the shelf came up"
nosan "$T"
fi

if want 20; then
echo "== 20. controls: each system's button layout, kept per system; the settings screen (CONTROLS)"
T=$(newroot t20)
$ROM "$T/root/roms/test.a26" >/dev/null
# the 2600's fire moved from Cross to R3: Cross does nothing, R3 fires
printf 'a26_btn_fire=8\n' >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;100:$CROSS;140:0;160:$R3;200:0;$(QUITAT 220)" "130,190" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$BANDS $T/dump/flip00130.ppm $IDLE >/dev/null" "fire on R3: Cross no longer fires"
expect "$BANDS $T/dump/flip00190.ppm GRRRRRRRRRRR >/dev/null" "fire on R3: R3 fires"
nosan "$T"
# the settings screen: the CONTROLS rows start with "Buttons of" (the system), 22 rows below Shader; Right there shows
# the 5200's buttons, Down, Right: its Fire 1 moved from Cross to Circle; the 2600's stay as they were
T=$(newroot t20d)
$ROM "$T/root/roms/Alpha (USA).a26" >/dev/null
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;$(REP 40 22 $DOWN);180:$RIGHT;182:0;190:$DOWN;192:0;200:$RIGHT;202:0;210:$CIRCLE;212:0;$(SHELFQUIT_AT 230)" "205")
expect "[ $rc = 0 ]" "settings screen: exit code 0 (got $rc)"
expect "grep -q '^controls_system=1$' $T/root/stella-ps5.ini && grep -q '^a52_btn_fire1=1$' $T/root/stella-ps5.ini" "the settings screen moved the 5200's Fire 1 to Circle"
expect "grep -q '^a26_btn_fire=0$' $T/root/stella-ps5.ini" "the 2600's fire stays on Cross"
nosan "$T"
# over a game, the CONTROLS rows show its system's buttons
T=$(newroot t20e)
$ROM "$T/root/roms/test.a78" >/dev/null
rc=$(run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);$(REP 200 23 $DOWN);350:$RIGHT;352:0;360:$CIRCLE;362:0;370:$CIRCLE;372:0;$(QUITAT 400)" "" "$T/root/roms/test.a78")
expect "[ $rc = 0 ] && grep -q '^controls_system=2$' $T/root/stella-ps5.ini && grep -q '^a78_btn_1=1$' $T/root/stella-ps5.ini" "over a 7800 game: its buttons (Button 1 moved to Circle)"
nosan "$T"
fi

if want 21; then
echo "== 21. CRT shaders: CRT Easymode style by default, every shader draws, the choice is kept"
SHADERS=("Off" "CRT Easymode style" "crt-lottes" "crt-lottes-fast" "crt-1tap" "crt-2tap" "crt-hyllian-fast" "crt-nobody" "newpixie-mini" "crt-blurPi-sharp" "crt-blurPi-soft" "monoCRT")
T=$(newroot t21)
rm -f "$T/root/stella-ps5.ini" # a first start: no settings file yet
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 160)" "90,130" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'shader CRT Easymode style' $T/root/logs/boot.log" "a first start draws the game through CRT Easymode style"
expect "python3 - $T/dump/flip00090.ppm <<'PY'
import sys
d = open(sys.argv[1], 'rb').read().split(b'\n', 3)
w, h = map(int, d[1].split()); px = d[3]
R = lambda x, y: px[(y * w + x) * 3]
col = [R(960, y) for y in range(400, 460)] # down the middle: scanlines
row = [R(x, 540) for x in range(900, 960)] # across: the aperture grille
sys.exit(0 if max(col) > 150 and min(col) < 0.85 * max(col) and len(set(row)) > 1 else 1)
PY" "scanlines and a phosphor mask on the red picture"
expect "python3 -c \"import sys; d=open('$T/dump/flip00130.ppm','rb').read().split(b'\\n',3); w=int(d[1].split()[0]); p=d[3]; sys.exit(0 if max(p[(y*w+960)*3+1] for y in range(80,130)) > 120 else 1)\"" "Cross -> the fire band turns green through the shader"
expect "grep -q 'shader [0-9.]* ms' $T/root/logs/boot.log" "the shader's drawing time is logged"
nosan "$T"
for s in $(seq 1 11); do
	T=$(newroot t21s$s)
	echo "shader=$s" >>"$T/root/stella-ps5.ini"
	$ROM "$T/root/roms/test.a26" >/dev/null
	# (the heavier shaders under ASan run a few frames a second: the run ends at its 100th flip, rc 3, not by the menu)
	rc=$(MAXFLIPS=100 run "$T" "0:0" "90" "$T/root/roms/test.a26")
	expect "[ $rc = 3 ] && grep -q 'shader ${SHADERS[$s]})' $T/root/logs/boot.log && python3 -c \"import sys; d=open('$T/dump/flip00090.ppm','rb').read().split(b'\\n',3); w=int(d[1].split()[0]); p=d[3]; sys.exit(0 if max(p[(y*w+960)*3] for y in range(500,580)) > 60 else 1)\"" "${SHADERS[$s]} draws the picture"
	expect "! grep -q 'runtime error\|AddressSanitizer' $T/out.txt" "${SHADERS[$s]}: no sanitizer reports"
done
T=$(newroot t21m)
$ROM "$T/root/roms/Alpha (USA).a26" >/dev/null
# Triangle -> settings, the first row is Shader: Right three times -> crt-lottes-fast; Circle -> back; Options + Cross -> quit
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;40:$RIGHT;42:0;46:$RIGHT;48:0;52:$RIGHT;54:0;60:$CIRCLE;62:0;70:$OPTIONS;72:0;80:$CROSS;82:0" "50")
expect "grep -q '^shader=3$' $T/root/stella-ps5.ini" "the settings screen's Shader row is saved (shader=0 -> 3)"
fi

if want 22; then
echo "== 22. the helper: unknown titles refused, a slow client can't block it, wanted.txt must be a plain file"
stop_helpers
T=$(newroot t22)
$ROM "$T/root/roms/test.a26" >/dev/null
# a process whose title can't be read (here: the host reports none) is not let out
STELLAPS5_HOST_JB_TITLE= STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.a26")
expect "grep -q 'title unknown: not Stella PS5' $T/hroot/logs/helper.log && ! grep -q 'letting it out' $T/hroot/logs/helper.log" "a process of unknown title is refused (fail closed)"
stop_helpers
# a client that sends one byte a second doesn't hold the helper: the app's request still goes through
T=$(newroot t22b)
$ROM "$T/root/roms/test.a26" >/dev/null
STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
python3 - "$STELLAPS5_HELPER_PORT" <<'PY' &
import socket, sys, time
s = socket.create_connection(('127.0.0.1', int(sys.argv[1])))
try:
    for i in range(20):
        s.send(b'x'); time.sleep(1)
except OSError:
    pass
PY
DRIP=$!
sleep 0.5
start=$(date +%s)
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.a26")
secs=$(( $(date +%s) - start ))
expect "grep -q 'Stella helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "with a slow client connected, the app is still let out"
expect "[ $secs -lt 15 ]" "and without waiting for the slow client (${secs}s)"
kill $DRIP 2>/dev/null
stop_helpers
# covers/wanted.txt as a symbolic link to another file: not read
T=$(newroot t22c)
mkdir -p "$T/hroot/covers"
echo "secret" >"$T/secret.txt"
ln -s "$T/secret.txt" "$T/hroot/covers/wanted.txt"
STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
waitfor "$T/hroot/logs/helper.log" "listening" || sleep 1
python3 - "$STELLAPS5_HELPER_PORT" "$T/answer.bin" <<'PY'
import socket, struct, sys
s = socket.create_connection(('127.0.0.1', int(sys.argv[1])))
req = bytearray(0xA10)
struct.pack_into('<IiI', req, 0, 0xDEADBEEF, 6, 1234)
s.sendall(req)
data = b''
while True:
    d = s.recv(65536)
    if not d: break
    data += d
open(sys.argv[2], 'wb').write(data)
PY
expect "grep -q 'wanted-covers list, 0 bytes' $T/hroot/logs/helper.log && ! grep -q secret $T/answer.bin" "a wanted.txt that is a symbolic link is not followed"
stop_helpers
fi

if want 23; then
echo "== 23. states: one folder per system; each system's game saves and loads its own"
# a 2600 game and a 7800 game of the same name: each its own state
T=$(newroot t23)
$ROM "$T/root/roms/test.a26" >/dev/null
$ROM "$T/root/roms/test.a78" >/dev/null
rc=$(run "$T" "0:0;100:$L2UP;105:0;$(QUITAT 140)" "" "$T/root/roms/test.a26")
rc2=$(run "$T" "0:0;100:$L2DOWN;105:0;$(QUITAT 140)" "" "$T/root/roms/test.a78")
expect "[ $rc = 0 ] && [ $rc2 = 0 ]" "exit codes 0 (got $rc, $rc2)"
expect "[ -f $T/root/states/Atari2600/test.state1 ] && [ ! -e $T/root/states/Atari7800/test.state1 ]" "the 2600 game's state went to states/Atari2600/"
expect "! grep -q 'load state 1: ok' $T/root/logs/boot.log" "the 7800 game of the same name didn't load it"
nosan "$T"
# a state of each other system: saved, loaded, the game runs on (each core's own state)
for g in test.a52 test.a78 test.o; do
	T=$(newroot t23$g)
	mkdir -p "$T/root/bios" && head -c 512 /dev/zero >"$T/root/bios/lynxboot.img"
	$ROM "$T/root/roms/$g" >/dev/null
	B=$CROSS; [ $g = test.o ] && B=$CIRCLE
	rc=$(run "$T" "0:0;100:$L2UP;102:0;130:$L2DOWN;132:0;150:$B;190:0;$(QUITAT 220)" "140,180" "$T/root/roms/$g")
	expect "[ $rc = 0 ] && grep -q 'save state 1: ok' $T/root/logs/boot.log && grep -q 'load state 1: ok' $T/root/logs/boot.log && [ -n \"\$(ls $T/root/states/*/test.state1 2>/dev/null)\" ]" "$g: saved and loaded (states/$(ls $T/root/states 2>/dev/null | tr '\n' ' '))"
	expect "$CHECK $T/dump/flip00140.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00180.ppm 960 540 green >/dev/null" "$g: the game runs on after the load (fire: green)"
	nosan "$T"
done
fi

if want 24; then
echo "== 24. audit fixes: 720p picture edges, Resume doesn't fire, odd library files, long paths, covers kept, downloads after a tab change"
# 720p: the whole picture follows the game, its edges included (the damage is mapped to 1280x720)
T=$(newroot t24)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_DIRECT_MAX_MIB=12 run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "130" "$T/root/roms/test.a26")
expect "grep -q 'scan-out 1280x720' $T/root/logs/boot.log" "720p scan-out"
expect "$BANDS $T/dump/flip00130.ppm GRRRRRRRRRRR >/dev/null && $CHECK $T/dump/flip00130.ppm 140 69 green >/dev/null && $CHECK $T/dump/flip00130.ppm 1138 69 green >/dev/null" "720p: Cross turns the fire band green to its edges"
nosan "$T"
# Resume with Cross: while Cross is still held the game doesn't see fire (the band stays red)
T=$(newroot t24b)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(run "$T" "0:0;100:$L3R3;102:0;120:$CROSS;200:0;$(QUITAT 260)" "190" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$BANDS $T/dump/flip00190.ppm $IDLE >/dev/null" "Cross held after Resume doesn't reach the game"
nosan "$T"
# the library: a FIFO doesn't hang the scan; a zip entry with a 600-character name; a .a78 that isn't one
T=$(newroot t24c)
mkdir -p "$T/root/roms/Atari2600"
mkfifo "$T/root/roms/Atari2600/pipe.a26"
$ROM "$T/root/roms/Atari2600/a.a26" >/dev/null
$ROM "$T/root/roms/Atari2600/b.a26" >/dev/null
echo "not a game" >"$T/root/roms/notes.a78"
python3 - "$T/root/roms/Atari2600/long.zip" "$T/root/roms/Atari2600/a.a26" <<'PY'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1], 'w') as z:
    z.writestr('x' * 600 + '.a26', b'\0' * 1024)
    z.write(sys.argv[2], 'inner.a26')
PY
rc=$(MAXFLIPS=400 run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "[ $rc = 0 ]" "the shelf came up and quit (got $rc)"
expect "! grep -q 'pipe.a26' $T/root/logs/boot.log" "a FIFO is not listed (nor read)"
expect "grep -q 'long.zip \[a26\]' $T/root/logs/boot.log" "a zip with a 600-character entry name still lists its game"
expect "grep -q 'Atari2600/b.a26' $T/root/covers/crc-cache.txt" "the CRC cache holds b.a26"
rm -f "$T/root/roms/Atari2600/b.a26"
rc=$(MAXFLIPS=400 run "$T" "0:0;30:$OPTIONS;32:0;40:$CROSS;42:0" "")
expect "! grep -q 'Atari2600/b.a26' $T/root/covers/crc-cache.txt" "a deleted ROM leaves the CRC cache"
rc=$(MAXFLIPS=300 run "$T" "0:0" "60" "$T/root/roms/notes.a78")
expect "grep -q 'could not start .*notes.a78' $T/root/logs/boot.log && [ -f $T/dump/flip00060.ppm ]" "a file that isn't a game: ProSystem refuses it, the screen says so, nothing breaks"
nosan "$T"
# a path of 255 characters or more: the game is read whole and handed over in memory, so it runs
T=$(newroot t24d)
D="$T/root/roms/$(printf 'd%.0s' $(seq 1 120))/$(printf 'e%.0s' $(seq 1 120))"
mkdir -p "$D"
$ROM "$D/test.a26" >/dev/null
rc=$(MAXFLIPS=300 run "$T" "0:0;$(QUITAT 100)" "90" "$D/test.a26")
expect "[ $rc = 0 ] && grep -q 'running \"test\" (Atari 2600)' $T/root/logs/boot.log && $CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "a path of $(( ${#D} + 9 )) characters: the game runs"
nosan "$T"
# Square (fetch the cover again) while offline: the cover it has is kept
T=$(newroot t24e)
mkdir -p "$T/root/roms/Atari2600" "$T/root/covers/Atari2600"
$ROM "$T/root/roms/Atari2600/Combat (Europe).a26" >/dev/null
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$T/root/covers/Atari2600/Combat (Europe).png')"
rc=$(run "$T" "0:0;40:$SQUARE;42:0;80:$OPTIONS;82:0;90:$CROSS;92:0" "")
expect "[ -s '$T/root/covers/Atari2600/Combat (Europe).png' ]" "offline, Square doesn't delete the cover"
nosan "$T"
# online, Square on a ROM named as its official name: the server has none (404) -> the cover stays; the server has it ->
# the new one replaces it
cp "$T/root/covers/Atari2600/Combat (Europe).png" "$T/old.png"
SRV=$T/srv/Atari_-_2600/Named_Boxarts; mkdir -p "$SRV"
PORT=18094
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png"
SQ="0:0;40:$SQUARE;42:0;200:$OPTIONS;202:0;210:$CROSS;212:0"
rc=$(OFFLINE= COVER_URL="$URL" STELLAPS5_HOST_REALTIME=1 run "$T" "$SQ" "")
expect "cmp -s '$T/root/covers/Atari2600/Combat (Europe).png' $T/old.png && [ ! -e '$T/root/covers/Atari2600/Combat (Europe).refetch' ]" "the server has none (404): the cover stays"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(0,255,0)).save('$SRV/Combat (Europe).png')"
rm -f "$T/root/covers/Atari2600/"*.missing
rc=$(OFFLINE= COVER_URL="$URL" STELLAPS5_HOST_REALTIME=1 run "$T" "$SQ" "")
kill $SRVPID 2>/dev/null
expect "cmp -s '$T/root/covers/Atari2600/Combat (Europe).png' '$SRV/Combat (Europe).png'" "the server has it: the new cover replaces the old one"
nosan "$T"
# as on the console (a helper answers, so the shelf downloads nothing): Square puts the cover on the wanted list and the
# helper fetches it in the background (no restart); the old cover stays until the new one lands
T2=$(newroot t24g)
mkdir -p "$T2/root/roms/Atari2600" "$T2/root/covers/Atari2600"
$ROM "$T2/root/roms/Atari2600/Combat (Europe).a26" >/dev/null
cp "$T/old.png" "$T2/root/covers/Atari2600/Combat (Europe).png"
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
STELLAPS5_ROOT=$T2/root ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T2/helper.txt" 2>&1 &
waitfor "$T2/root/logs/helper.log" listening
rc=$(OFFLINE= COVER_URL="$URL" STELLAPS5_HOST_REALTIME=1 run "$T2" "0:0;40:$SQUARE;42:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "")
expect "grep -q 'Atari2600/Combat (Europe).png' $T2/root/covers/wanted.txt && ! grep -q 'restarting' $T2/root/logs/boot*.log" "console: Square lists the cover for the helper, no restart"
expect "grep -q 'Atari2600/Combat (Europe).png -> 200' $T2/root/logs/helper.log && cmp -s '$T2/root/covers/Atari2600/Combat (Europe).png' '$SRV/Combat (Europe).png' && [ ! -e '$T2/root/covers/Atari2600/Combat (Europe).refetch' ]" "the helper fetched the new cover over the old one"
kill $SRVPID 2>/dev/null
stop_helpers
# downloads still work after the shelf restarted its cover worker (another tab)
T=$(newroot t24f)
SRV=$T/srv/Atari_-_2600/Named_Boxarts; mkdir -p "$SRV" "$T/root/roms/Atari2600" "$T/root/roms/Atari7800" "$T/root/covers/Atari7800"
python3 -c "from PIL import Image; Image.new('RGB',(512,357),(255,0,0)).save('$SRV/Combat (Europe).png'); Image.new('RGB',(360,512),(0,0,255)).save('$T/root/covers/Atari7800/Centipede (USA).png')"
$ROM "$T/root/roms/Atari2600/Combat (Europe).a26" >/dev/null
$ROM "$T/root/roms/Atari7800/Centipede (USA).a78" >/dev/null
echo "shelf_family=3" >>"$T/root/stella-ps5.ini" # the 7800 tab: Centipede only; Up -> Atari 2600
PORT=18082
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" STELLAPS5_HOST_REALTIME=1 run "$T" \
	"0:0;60:$UP;62:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "")
kill $SRVPID 2>/dev/null
expect "grep -q 'tab Atari 2600' $T/root/logs/boot.log" "moved to another tab (the cover worker restarted)"
expect "cmp -s '$T/root/covers/Atari2600/Combat (Europe).png' '$SRV/Combat (Europe).png'" "the new tab's cover was downloaded"
nosan "$T"
fi

if want 25; then
echo "== 25. debug logs off: nothing written (app and helper), earlier logs kept; the setting switches them at once"
# off in the ini: the game runs, no line anywhere, the previous run's boot.log untouched
T=$(newroot t25)
$ROM "$T/root/roms/test.a26" >/dev/null
echo "debug_logs=0" >>"$T/root/stella-ps5.ini"
mkdir -p "$T/root/logs" && echo "OLD RUN" >"$T/root/logs/boot.log"
rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.a26")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "the game runs"
expect "[ \"\$(cat $T/root/logs/boot.log)\" = 'OLD RUN' ] && [ ! -e $T/root/logs/boot.prev.log ]" "the earlier boot.log is kept as it was, nothing new written"
expect "! grep -q '^\[stella-ps5' $T/out.txt" "nothing on stdout either"
expect "grep -q '^debug_logs=0' $T/root/stella-ps5.ini" "the setting stays off"
nosan "$T"
# the default: no debug_logs key and no test override -> nothing written, and the settings file says 0
T=$(newroot t25z)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_LOGS_DEFAULT= run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.a26")
expect "[ $rc = 0 ] && [ ! -e $T/root/logs/boot.log ] && ! grep -q '^\[stella-ps5' $T/out.txt" "debug logs off by default: no boot.log"
expect "grep -q '^debug_logs=0' $T/root/stella-ps5.ini" "the default settings file has debug_logs=0"
nosan "$T"
# Settings (Triangle) -> Debug logs (Up three times from Shader: Back, Updates, then Debug logs) -> Off: that line is the last one
T=$(newroot t25b)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;$(REP 50 3 $UP);70:$CROSS;72:0;90:$CIRCLE;92:0;$(SHELFQUIT_AT 120)" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '^debug_logs=0' $T/root/stella-ps5.ini" "Debug logs Off saved"
expect "tail -1 $T/root/logs/boot.log | grep -q 'debug logs turned off'" "the last line of boot.log says the logs were turned off"
nosan "$T"
# and back on: logging starts again in the same boot.log
T=$(newroot t25c)
$ROM "$T/root/roms/test.a26" >/dev/null
echo "debug_logs=0" >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;$(REP 50 3 $UP);70:$CROSS;72:0;90:$CIRCLE;92:0;$(SHELFQUIT_AT 120)" "")
expect "grep -q '^debug_logs=1' $T/root/stella-ps5.ini" "Debug logs On saved"
expect "grep -q 'debug logs turned on' $T/root/logs/boot.log && ! grep -q 'Stella PS5 1\.[0-9.]*, Stella' $T/root/logs/boot.log" "boot.log starts at the switch (nothing from before it)"
nosan "$T"
# the helper follows the setting too
stop_helpers
T=$(newroot t25d)
$ROM "$T/root/roms/test.a26" >/dev/null
mkdir -p "$T/hroot" && echo "debug_logs=0" >"$T/hroot/stella-ps5.ini"
STELLAPS5_ROOT=$T/hroot ASAN_OPTIONS=detect_leaks=0 timeout 60 "$HELPER" >"$T/helper.txt" 2>&1 &
sleep 1.5
rc=$(run "$T" "$(QUITAT 30)" "" "$T/root/roms/test.a26")
expect "grep -q 'Stella helper (port [0-9]*): ret 0' $T/root/logs/boot.log" "the helper still lets the app out"
expect "[ ! -e $T/hroot/logs/helper.log ] && ! grep -q 'helper\]' $T/helper.txt" "with debug logs off the helper writes no log"
stop_helpers
fi

if want 26; then
echo "== 26. the helper's own HTTPS (Mbed TLS): certificate checked, kept connection, chunks, redirects, 404"
stop_helpers
C=$WORK/t26-certs; rm -rf "$C"; mkdir -p "$C"
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/ca.key" -out "$C/ca.pem" -days 30 -subj "/CN=Stella PS5 Test CA" 2>/dev/null
openssl req -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/srv.key" -out "$C/srv.csr" -subj "/CN=localhost" 2>/dev/null
printf 'subjectAltName=DNS:localhost\nbasicConstraints=CA:FALSE\n' >"$C/ext.cnf"
openssl x509 -req -in "$C/srv.csr" -CA "$C/ca.pem" -CAkey "$C/ca.key" -CAcreateserial -out "$C/srv.pem" -days 30 -extfile "$C/ext.cnf" 2>/dev/null
openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:P-256 -nodes -keyout "$C/other.key" -out "$C/other.pem" -days 30 -subj "/CN=localhost" -addext "subjectAltName=DNS:localhost" 2>/dev/null
SRVROOT=$WORK/t26-srv; rm -rf "$SRVROOT"; mkdir -p "$SRVROOT/Repo/Named_Boxarts"
python3 -c "
from PIL import Image
for n,c in [('A b',(255,0,0)),('B',(0,255,0)),('C',(0,0,255))]: Image.new('RGB',(512,357),c).save('$SRVROOT/Repo/Named_Boxarts/'+n+'.png')"
PORT=18443
tls_case() { # name cert key mode host -> $T with the helper's run in it
	T=$(newroot "t26-$1")
	mkdir -p "$T/root/covers"
	python3 tests/https_server.py "$SRVROOT" $PORT "$2" "$3" "$4" 2>"$T/server.txt" &
	local sp=$!
	sleep 0.6
	local u="https://$5:$PORT"
	printf 'Atari2600/A.png\t%s/Repo/Named_Boxarts/A%%20b.png\nAtari2600/B.png\t%s/chunked/Repo/Named_Boxarts/B.png\nAtari2600/C.png\t%s/moved/Repo/Named_Boxarts/C.png\nAtari2600/D.png\t%s/Repo/Named_Boxarts/D.png\n' "$u" "$u" "$u" "$u" >"$T/root/covers/wanted.txt"
	STELLAPS5_EXTRA_CA=$C/ca.pem STELLAPS5_ROOT=$T/root ASAN_OPTIONS=detect_leaks=0 timeout ${6:-6} "$HELPER" >"$T/helper.txt" 2>&1
	kill $sp 2>/dev/null; wait $sp 2>/dev/null
	stop_helpers
}
tls_case good "$C/srv.pem" "$C/srv.key" keep localhost
H="$T/root/logs/helper.log"; D="$T/root/covers/Atari2600"; S="$SRVROOT/Repo/Named_Boxarts"
expect "grep -q '\[https\] Mbed TLS 3\.6\.[0-9]*: [0-9]* CA certificate(s), + the extra CA' $H" "Mbed TLS set up with Mozilla's CA list (+ the test CA)"
expect "cmp -s '$D/A.png' '$S/A b.png'" "a cover over HTTPS (Content-Length)"
expect "cmp -s '$D/B.png' '$S/B.png'" "a chunked answer"
expect "cmp -s '$D/C.png' '$S/C.png' && grep -q 'GET /moved/' $T/server.txt" "a redirect followed"
expect "[ -f '$D/D.missing' ] && [ ! -f '$D/D.png' ]" "a 404: marked missing"
expect "[ \$(grep -c 'kept connection' $H) = 3 ]" "one connection for the four requests"
expect "! grep -q '\[http\] ' $H" "the helper doesn't use libSceHttp2"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/helper.txt" "no sanitizer reports"
tls_case untrusted "$C/other.pem" "$C/other.key" keep localhost 4
H="$T/root/logs/helper.log"; D="$T/root/covers/Atari2600"
expect "grep -q 'Certificate verification failed' $H && [ -z \"\$(ls $D 2>/dev/null)\" ]" "a certificate from an unknown CA: refused, nothing saved, nothing marked missing"
tls_case wrongname "$C/srv.pem" "$C/srv.key" keep 127.0.0.1 4
H="$T/root/logs/helper.log"; D="$T/root/covers/Atari2600"
expect "grep -q 'Certificate verification failed' $H && [ -z \"\$(ls $D 2>/dev/null)\" ]" "a certificate for another name: refused"
tls_case closing "$C/srv.pem" "$C/srv.key" drop localhost
D="$T/root/covers/Atari2600"
expect "[ -f '$D/A.png' ] && [ -f '$D/B.png' ] && [ -f '$D/C.png' ]" "a server that closes kept connections: a new one each time"
fi

if want 27; then
echo "== 27. Stella's picture: the palettes, the TV effects (NTSC filter), phosphor, the HMOVE bars, the TV format, the aspect"
# the palettes: z26's and Stella's custom one are other reds than the standard palette's
for pal in 1 2; do
	T=$(newroot t27p$pal)
	$ROM "$T/root/roms/test.a26" >/dev/null
	echo "palette=$pal" >>"$T/root/stella-ps5.ini"
	rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.a26")
	expect "[ $rc = 0 ] && $CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && ! $CHECK $T/dump/flip00090.ppm 960 540 177 40 39 4 >/dev/null" "palette=$pal: still red, but not the standard palette's red"
	nosan "$T"
done
# the NTSC filter: Stella hands over its 568-pixel filtered picture, shown as wide as the unfiltered one
T=$(newroot t27tv)
$ROM "$T/root/roms/test.a26" >/dev/null
echo "tv_effects=1" >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.a26")
expect "[ $rc = 0 ] && grep -q 'picture 568x228 (228 lines, aspect 1.4035, Fit to screen) -> 202,0 1516x1080' $T/root/logs/boot.log && $CHECK $T/dump/flip00090.ppm 300 540 red >/dev/null && $CHECK $T/dump/flip00090.ppm 1620 540 red >/dev/null && $CHECK $T/dump/flip00090.ppm 190 540 black >/dev/null && $CHECK $T/dump/flip00090.ppm 1730 540 black >/dev/null" "TV effects Composite: the filtered picture (568 wide), the same shape on the screen (the filter's soft edges aside)"
nosan "$T"
# phosphor: the fire band blinking every frame (Cross on, off, on...) is a blend of red and green with it, pure without it
for ph in 1 2; do
	T=$(newroot t27ph$ph)
	$ROM "$T/root/roms/test.a26" >/dev/null
	echo "phosphor=$ph" >>"$T/root/stella-ps5.ini"
	P="0:0"; for f in $(seq 100 1 130); do P="$P;$f:$([ $((f % 2)) = 0 ] && echo $CROSS || echo 0)"; done
	rc=$(run "$T" "$P;$(QUITAT 150)" "120,121" "$T/root/roms/test.a26")
	if [ $ph = 1 ]; then
		expect "[ $rc = 0 ] && $CHECK $T/dump/flip00120.ppm 960 104 61 151 60 6 >/dev/null && $CHECK $T/dump/flip00121.ppm 960 104 177 40 39 6 >/dev/null" "phosphor off: each frame as it is (green, red)"
	else
		expect "[ $rc = 0 ] && ! $CHECK $T/dump/flip00120.ppm 960 104 61 151 60 20 >/dev/null && ! $CHECK $T/dump/flip00121.ppm 960 104 177 40 39 20 >/dev/null" "phosphor on: each frame blended with the last one"
	fi
	nosan "$T"
done
# the HMOVE bars hidden: 8 pixels fewer, the 4:3 shape that leaves; the TV format forced to PAL: 50 Hz, 274 lines;
# the aspect ratio 4:3
for opt in "crop_hmove=1:picture 152x228 (228 lines, aspect 1.3333" "tv_format=2:picture 160x274 (274 lines, aspect 1.1679" "aspect=1:picture 160x228 (228 lines, aspect 1.3333"; do
	T=$(newroot t27o)
	$ROM "$T/root/roms/test.a26" >/dev/null
	echo "${opt%%:*}" >>"$T/root/stella-ps5.ini"
	rc=$(run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.a26")
	expect "[ $rc = 0 ] && grep -q '${opt#*:}' $T/root/logs/boot.log" "${opt%%:*}: ${opt#*:})"
	nosan "$T"
done
T=$(newroot t27r)
$ROM "$T/root/roms/test.a26" >/dev/null
echo "tv_format=2" >>"$T/root/stella-ps5.ini"
rc=$(STELLAPS5_HOST_QUIT_AFTER=3 run "$T" "0:0" "" "$T/root/roms/test.a26")
expect "grep -q 'running \"test\" (Atari 2600), 50.0' $T/root/logs/boot.log" "TV format PAL: an NTSC game at 50 Hz"
fi

# what a run showed untranslated (STELLAPS5_HOST_I18N_MISSING), less the names that stay as they are (groups 28, 38)
NAMES_PY="names = {'Alpha (USA)', 'Alpha', 'Gamma (World)', 'Gamma', 'Atari 2600', 'Atari 5200', 'Atari 7800', 'Atari Lynx',
         'A26', 'O', 'L1', 'R1', 'R3', 'OPTIONS', 'Touchpad', 'Cross', 'Circle', 'Square', 'Triangle', 'None', 'A', 'B',
         'English', 'Español', 'Português (Brasil)', 'Français', 'Deutsch', 'S-Video', 'RGB', '4:3', '16:9', 'z26',
         'Stella', 'a5200', 'ProSystem', 'Gearlynx', 'SMB (Windows, Samba, NAS)', 'NFS', 'USB', 'Joystick',
         'paddles', 'test'}"
untranslated() { # missing.txt fe_i18n.cpp
	python3 - "$1" $PWD/frontend/fe_i18n.cpp <<PY
import os, re, sys
src = open(sys.argv[2], encoding='utf-8').read()
translated = set(re.findall(r'"((?:[^"\\\\]|\\\\.)*)"', src))
spec = re.compile(r'%(?:%|[-+ #0]*\d*(?:\.\d+)?(?:hh|h|ll|l|z)?[diouxXeEfgGcsp])')
formats = [re.compile('^' + ''.join('.*' if spec.fullmatch(part) else re.escape(part)
                                    for part in re.split('(' + spec.pattern + ')', f)) + '\$')
           for f in translated if '%' in f]
$NAMES_PY
for line in (open(sys.argv[1], encoding='utf-8') if os.path.exists(sys.argv[1]) else []):
    t = line.rstrip('\n')
    if t in translated or t in names or not re.search('[A-Za-z]{2}', t) or re.fullmatch(r'[\d%. ]+', t):
        continue
    if any(f.match(t) for f in formats):
        continue
    if t.startswith('↣') or re.match(r'Atari ?(2600|5200|7800|Lynx)( |$)', t):
        continue
    print(t)
PY
}
if want 28; then
echo "== 28. languages: English by default; Spanish, Portuguese (Brazil), French and German translated"
expect "python3 $PWD/tests/check_i18n.py $PWD/frontend/fe_i18n.cpp >/dev/null" "every translation has its five languages and the English format specifiers"
L2RIGHT=$(printf %x $((0x$L2 | 0x$RIGHT)))
for L in 1 2 3 4; do
	T=$(newroot t28_$L)
	echo "language=$L" >>"$T/root/stella-ps5.ini"
	mkdir -p "$T/root/roms/AtariLynx" "$T/root/roms/Atari2600"
	$ROM "$T/root/roms/Atari2600/Alpha (USA).a26" >/dev/null
	$ROM "$T/root/roms/AtariLynx/Gamma (World).o" >/dev/null
	# the settings screen scrolled through (and the CONTROLS of the 5200, 7800, Lynx); the Lynx game without its boot ROM
	# (the error box); the 2600 game: a save, the next slot, the pause menu walked down, back to the list; quit (the question)
	pad="0:0;20:$TRIANGLE;22:0"; f=30
	for i in $(seq 1 22); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 6)); done
	for s in 1 2 3; do pad="$pad;$f:$RIGHT;$((f + 2)):0"; f=$((f + 6)); for i in $(seq 1 12); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 6)); done; for i in $(seq 1 12); do pad="$pad;$f:$UP;$((f + 2)):0"; f=$((f + 6)); done; done
	pad="$pad;$f:$RIGHT;$((f + 2)):0"; f=$((f + 6))
	for i in $(seq 1 30); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 6)); done
	pad="$pad;$((f + 4)):$CIRCLE;$((f + 6)):0;$((f + 20)):$RIGHT;$((f + 22)):0;$((f + 30)):$CROSS;$((f + 32)):0"; f=$((f + 70))
	pad="$pad;$f:$CROSS;$((f + 2)):0;$((f + 20)):$LEFT;$((f + 22)):0"; f=$((f + 40))
	pad="$pad;$f:$CROSS;$((f + 2)):0"; f=$((f + 100))
	pad="$pad;$f:$L2UP;$((f + 4)):0;$((f + 30)):$L2RIGHT;$((f + 34)):0"; f=$((f + 60))
	pad="$pad;$f:$L3R3;$((f + 2)):0"; f=$((f + 10))
	for i in $(seq 1 13); do pad="$pad;$f:$DOWN;$((f + 2)):0"; f=$((f + 8)); done
	pad="$pad;$((f + 10)):$UP;$((f + 12)):0;$((f + 20)):$CROSS;$((f + 22)):0"; f=$((f + 60))
	pad="$pad;$f:$OPTIONS;$((f + 2)):0;$((f + 10)):$CROSS;$((f + 12)):0"
	rm -f $T/missing.txt
	rc=$(STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "$pad" "")
	# and the shelf with no games
	T2=$(newroot t28e_$L)
	echo "language=$L" >>"$T2/root/stella-ps5.ini"
	rc2=$(STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T2" "0:0;40:$OPTIONS;42:0;50:$CROSS;52:0" "")
	# what was shown untranslated: only names (the games', the systems', the buttons') and texts already translated
	untranslated $T/missing.txt > $T/untranslated.txt
	expect "[ $rc = 0 ] && [ $rc2 = 0 ] && [ -s $T/missing.txt ] && [ ! -s $T/untranslated.txt ]" "language $L: every text shown is translated ($(head -c 300 $T/untranslated.txt | tr '\n' '|'))"
	expect "grep -q 'save state 1: ok' $T/root/logs/boot.log && grep -q 'missing firmware: lynxboot.img' $T/root/logs/boot.log && grep -q 'game closed' $T/root/logs/boot.log" "language $L: the walk went through the error box, the game and the pause menu"
	expect "grep -q '^language=$L$' $T/root/stella-ps5.ini" "language $L: kept in the settings file"
	nosan "$T"
	rm -rf "$T2"
done
# the messages of a game that needs paddles, of a 5200 game without 5200.rom, of an update ShadowMount+'s image can't take
for L in 1 2 3 4; do
	T=$(newroot t28m_$L)
	echo "language=$L" >>"$T/root/stella-ps5.ini"
	$ROM "$T/root/roms/paddles.a26" paddles >/dev/null
	$ROM "$T/root/roms/test.a52" >/dev/null
	rm -f $T/missing.txt
	rc=$(STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/paddles.a26")
	rc2=$(STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.a52")
	untranslated $T/missing.txt > $T/untranslated.txt
	expect "[ $rc = 0 ] && [ $rc2 = 0 ] && [ $(grep -ci 'paddles\|5200.rom' $T/missing.txt) -ge 3 ] && [ ! -s $T/untranslated.txt ]" "language $L: the paddles message and the 5200 BIOS message are translated ($(head -c 200 $T/untranslated.txt | tr '\n' '|'))"
	nosan "$T"
done
# the network screens: Settings, Network shares with a share that does not connect, Add a share, the keyboard on
# Server (Circle closes it), the share type switched to NFS, Circle asks to discard (Up, Cross: discard), Connect again;
# back to the shelf: R1 to its Network tab (the share's state), Square scans again, L1 back, quit. Network shares is
# Up five times from Shader (Back, Updates, Debug logs, Language, Network shares).
for L in 1 2 3 4; do
	T=$(newroot t28net_$L)
	echo "language=$L" >>"$T/root/stella-ps5.ini"
	printf 'smb://10.0.0.5/games\nsmb://10.0.0.5:99999/games2\n' >"$T/root/shares.txt"
	$ROM "$T/root/roms/Alpha (USA).a26" >/dev/null
	key() { pad="$pad;$f:$1;$((f + 2)):0"; f=$((f + ${2:-6})); } # one press, then the next one $2 flips later
	pad="0:0;20:$TRIANGLE;22:0"; f=30
	for i in $(seq 1 5); do key $UP; done
	key $CROSS 20; key $DOWN; key $CROSS 20; key $CROSS 20; key $CIRCLE 20
	key $UP; key $RIGHT 20; key $CIRCLE 20; key $UP; key $CROSS 20
	key $DOWN; key $CROSS 20; key $CIRCLE 20; key $CIRCLE 30
	key $R1 30; key $SQUARE 60; key $L1 30
	key $OPTIONS 10; key $CROSS 10
	rm -f $T/missing.txt
	rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR=$T/nothing STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "$pad" "")
	untranslated $T/missing.txt > $T/untranslated.txt
	expect "[ $rc = 0 ] && grep -q 'network tab: scan again' $T/root/logs/boot.log && grep -q 'shares.txt: 1 line(s) not valid, the first: shares.txt line 2: the port must be a number from 1 to 65535' $T/root/logs/boot.log && [ -s $T/missing.txt ] && [ ! -s $T/untranslated.txt ]" "language $L: the network screens, the Network tab and what is wrong with a shares.txt line are translated ($(head -c 200 $T/untranslated.txt | tr '\n' '|'))"
	nosan "$T"
done
# Settings, Language (first row of SYSTEM: Up four times from Shader: Back, Updates, Debug logs, Language), Right: Spanish
T=$(newroot t28s)
rc=$(run "$T" "0:0;30:$TRIANGLE;32:0;$(REP 50 4 $UP);80:$RIGHT;82:0;100:$CIRCLE;102:0;110:$OPTIONS;112:0;120:$CROSS;122:0" "")
expect "[ $rc = 0 ] && grep -q '^language=1$' $T/root/stella-ps5.ini" "Settings, Language, Right: Español saved"
# English stays the default: no setting, English on the screen (the texts pass through as they are)
T=$(newroot t28en)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_I18N_MISSING=$T/missing.txt run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ] && [ ! -s $T/missing.txt ] && grep -q '^language=0$' $T/root/stella-ps5.ini" "English by default (language=0): nothing is looked up"
fi

# ---- network shares: netfs, games on a share, the Network shares screen, the shelf's storage tabs ------------------
# From NES FCEUmm PS5 1.1 (Beetle PCE PS5 1.1.1's, Genesis Plus GX PS5 2.1.1's, thatboialex's design in Snes9x PS5). The
# host build's share backend (host/netfs_dirbackend.cpp) answers every smb:// and nfs:// address with the folder
# STELLAPS5_HOST_NETFS_DIR names.

if want 29; then
echo "== 29. netfs: share addresses and redaction, routing, the libc --wrap layer, minizip and zlib on a share, the watchdog (in-memory share)"
NETFS_OUT=$(ASAN_OPTIONS=detect_leaks=1 timeout 120 "$PWD/build/host/test-netfs" 2>&1)
NETFS_RC=$?
echo "$NETFS_OUT" | tail -3 | sed 's/^/  /'
expect "[ $NETFS_RC = 0 ]" "netfs: every check passes"
expect "! echo \"\$NETFS_OUT\" | grep -q 'runtime error\|Sanitizer'" "no sanitizer reports"
fi

if want 30; then
echo "== 30. games on a share: on the shelf, a 2600 game and a Lynx game started from it (read whole), states and the BIOS on the console"
# a. a 2600 game on an SMB share: listed, started from the shelf (read whole), its save state on the console
T=$(newroot t30)
mkdir -p "$T/share/Atari2600"
$ROM "$T/share/Atari2600/Net Game (USA).a26" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;280:$CROSS;320:0;340:$L2UP;345:0;$(QUITAT 400)" "300")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q '\[net\] 1 share(s) in shares.txt' $T/root/logs/boot.log" "shares.txt was read"
expect "grep -q '\[net\] Connected to smb://10.0.0.5/games' $T/root/logs/boot.log" "the share connected"
expect "grep -q 'loading /smb/10.0.0.5/games/Atari2600/Net Game (USA).a26' $T/root/logs/boot.log && grep -q 'running \"Net Game (USA)\" (Atari 2600)' $T/root/logs/boot.log" "the game was started from the share"
expect "$BANDS $T/dump/flip00300.ppm GRRRRRRRRRRR >/dev/null" "it runs (Cross -> fire)"
expect "[ -f '$T/root/states/Atari2600/Net Game (USA).state1' ] && [ ! -e '$T/share/states' ] && [ \$(find $T/share -type f | wc -l) = 1 ]" "its state is on the console, nothing is written to the share"
expect "grep -q 'last_rom=/smb/10.0.0.5/games/Atari2600/Net Game (USA).a26' $T/root/stella-ps5.ini" "the shelf remembers it"
nosan "$T"
# a power cycle (pause menu, Down eleven times) starts the game again from memory: the share is not read again
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;$(SEQ 280 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);420:$CROSS;460:0;$(QUITAT 500)" "450")
expect "[ $rc = 0 ] && grep -q 'power cycle: Net Game (USA).a26 kept in memory, not read again' $T/root/logs/boot.log && [ \$(grep -c 'running \"Net Game (USA)\"' $T/root/logs/boot.log) = 2 ]" "a power cycle restarts the game from memory, not from the share"
expect "$BANDS $T/dump/flip00450.ppm GRRRRRRRRRRR >/dev/null" "and it runs (Cross -> fire)"
nosan "$T"
# b. a Lynx game on an NFS share, lynxboot.img on the console: started from the share
T=$(newroot t30lnx)
mkdir -p "$T/root/bios" "$T/share/AtariLynx"
head -c 512 /dev/zero >"$T/root/bios/lynxboot.img"
$ROM "$T/share/AtariLynx/Hand (World).o" >/dev/null
echo "nfs://10.0.0.5/export/atari" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=600 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$CROSS;152:0;$(QUITAT 300)" "")
expect "[ $rc = 0 ] && grep -q 'loading /nfs/10.0.0.5/export/atari/AtariLynx/Hand (World).o' $T/root/logs/boot.log && grep -q 'running \"Hand (World)\" (Atari Lynx)' $T/root/logs/boot.log" "a Lynx game on a share starts (its boot ROM from the console)"
expect "[ \$(find $T/share -type f | wc -l) = 1 ]" "nothing is written to the share"
nosan "$T"
# the same with 200 ms on every read, as a slow link: the game is read whole before it starts, and runs
T=$(newroot t30slow)
mkdir -p "$T/share"
$ROM "$T/share/Slow (USA).a26" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_NETFS_DELAY_MS=200 STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;200:$CROSS;202:0;400:$CROSS;440:0;$(QUITAT 500)" "420")
expect "[ $rc = 0 ] && grep -q 'running \"Slow (USA)\" (Atari 2600)' $T/root/logs/boot.log && $BANDS $T/dump/flip00420.ppm GRRRRRRRRRRR >/dev/null" "a slow link (200 ms a read): the game starts and runs"
expect "! grep -q 'in realtime' $T/root/logs/boot.log" "and the running game never waits for the share"
nosan "$T"
# c. a share that does not connect: the shelf still works and says why
T=$(newroot t30f)
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;100:$OPTIONS;102:0;110:$CROSS;112:0" "")
expect "[ $rc = 0 ] && grep -q '\[games\] 0 ROM(s)' $T/root/logs/boot.log" "a share that does not connect leaves the shelf working"
expect "grep -q '\[net\] The folder games is not on 10.0.0.5' $T/root/logs/boot.log" "and says why"
nosan "$T"
# d. shares.txt: written with its comments when missing; a bad line is named in the log, never its text
T=$(newroot t30n)
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 40)" "")
expect "[ $rc = 0 ] && grep -q '^# Stella PS5 network shares' $T/root/shares.txt && ! grep -q '^smb://\|^nfs://' $T/root/shares.txt" "a commented shares.txt is written when there is none"
printf 'smb://bob@10.0.0.5/games?password=Hunter2x\nftp://10.0.0.5/x?password=Hunter2y\n' >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;$(SHELFQUIT_AT 40)" "")
expect "[ $rc = 0 ] && grep -q 'shares.txt line 2: ' $T/root/logs/boot.log" "a line that is not an address is named by its number"
expect "! grep -rq 'Hunter2' $T/root/logs $T/out.txt" "no password in a log or on stdout"
nosan "$T"
fi

if want 31; then
echo "== 31. a game on a share with a made-up file name is named by its CRC in the background, its cover follows; a zip's system too"
T=$(newroot t31)
mkdir -p "$T/srv/Atari_-_2600/Named_Boxarts" "$T/share/misc"
python3 -c "from PIL import Image; import sys; Image.new('RGB', (512, 357), (0, 255, 0)).save(sys.argv[1])" "$T/srv/Atari_-_2600/Named_Boxarts/Adventure (USA).png"
ADV=$(crc_of a26 "Adventure (USA)")
$ROM "$T/share/adv.a26" >/dev/null
python3 tests/forge_crc.py "$T/share/adv.a26" $ADV >/dev/null
# a 7800 game in a zip, in a folder whose name says nothing: listed as a 2600 game until its zip is read
$ROM "$T/seven.a78" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Seven.a78'); z.close()" "$T/share/misc/seven.zip" "$T/seven.a78"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
PORT=18082
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" STELLAPS5_HOST_REALTIME=1 MAXFLIPS=420 STELLAPS5_HOST_NETFS_DIR="$T/share" \
	run "$T" "0:0" "400")
kill $SRVPID 2>/dev/null
expect "grep -q '\[games\] naming 2 network ROM(s) by CRC' $T/root/logs/boot.log" "the pass was started for the two games the scan did not read"
expect "grep -q 'adv.a26 \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log" "the next scan names the game by its CRC"
expect "grep -q 'misc/seven.zip \[a26\]' $T/root/logs/boot.log && grep -q 'misc/seven.zip \[a78\]' $T/root/logs/boot.log" "the zip moves to the 7800 once its directory was read"
expect "grep -q '/smb/10.0.0.5/games/adv.a26' $T/root/covers/crc-cache.txt && grep -q '^sys:/smb/10.0.0.5/games/misc/seven.zip' $T/root/covers/crc-cache.txt" "both are kept for the next start"
expect "[ -f '$T/root/covers/Atari2600/Adventure (USA).png' ]" "the cover of the game named late was downloaded"
nosan "$T"
# the next start reads nothing over the network: the cache names both
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=200 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "[ \$(grep -c 'adv.a26 \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log) -ge 1 ] && grep -q 'misc/seven.zip \[a78\]' $T/root/logs/boot.log && ! grep -q 'naming .* network ROM' $T/root/logs/boot.log" "the next start: named from the cache, no pass needed"
nosan "$T"
# a read error while the share stays connected is not the file's fault: nothing is remembered, the zip stays on the
# shelf, and the next start reads both again
T=$(newroot t31e)
mkdir -p "$T/share/misc"
$ROM "$T/share/adv.a26" >/dev/null
python3 tests/forge_crc.py "$T/share/adv.a26" $ADV >/dev/null
$ROM "$T/seven.a78" >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Seven.a78'); z.close()" "$T/share/misc/seven.zip" "$T/seven.a78"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_NETFS_EIO=adv.a26,seven.zip STELLAPS5_HOST_REALTIME=1 MAXFLIPS=400 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "grep -q 'network CRC pass: 2 ROM(s) read\$' $T/root/logs/boot.log" "read errors: the pass tried both"
expect "! grep -q 'adv.a26.*fail' $T/root/covers/crc-cache.txt && ! grep -q '^sys:.*seven.zip' $T/root/covers/crc-cache.txt" "and remembered no failure"
expect "[ \$(grep -c 'misc/seven.zip \[a26\]' $T/root/logs/boot.log) -ge 2 ]" "the zip is still on the shelf after the pass"
nosan "$T"
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=400 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0" "")
expect "grep -q 'adv.a26 \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log && grep -q 'misc/seven.zip \[a78\]' $T/root/logs/boot.log" "the next start reads both again and names them"
nosan "$T"
# Settings (Triangle) while the pass reads a ROM (3 s a read): the pass stops first; the ROM it was reading is read again
# once Settings is left
T=$(newroot t31s)
mkdir -p "$T/share"
$ROM "$T/share/adv.a26" >/dev/null
python3 tests/forge_crc.py "$T/share/adv.a26" $ADV >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_NETFS_DELAY_MS=3000 STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;60:$TRIANGLE;62:0;90:$CIRCLE;92:0" "")
expect "grep -q 'network CRC pass: 0 ROM(s) read (stopped)' $T/root/logs/boot.log" "Settings stops the pass while it reads"
expect "grep -q 'adv.a26 \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log" "and, once Settings is left, the ROM it was reading is read again and named"
nosan "$T"
fi

if want 32; then
echo "== 32. the Network shares screen: add, change, test and remove a share with the on-screen keyboard (Settings, Network shares)"
# tests/osk_pad.py turns the steps into a pad script (the keyboard's layout is its model). Settings is Triangle on the shelf; Up
# five times from Shader reaches Network shares (Back, Updates, Debug logs, Language, Network shares). Every run ends:
# Circle twice, OPTIONS, Cross.
net_pad() { python3 tests/osk_pad.py 2>/dev/null; }
net_quit='press CIRCLE 2
press OPTIONS
wait 8
press CROSS'
# a. a new share: server, share, folder with a space, user, a password that needs Shift and both kinds of symbols
T=$(newroot t32)
mkdir -p "$T/share"
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
field 10.0.0.5
press DOWN
field games
press DOWN
field Atari Games
press DOWN
field jason
press DOWN
field Zq9@pw&1
press DOWN 6
press CROSS
wait 20
$net_quit
STEPS
)
rc=$(STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "$PAD" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \"\$(tail -n 1 $T/root/shares.txt)\" = 'smb://jason@10.0.0.5/games/Atari%20Games?password=Zq9%40pw%261' ]" "the share is written as one escaped line"
expect "grep -q '^# Stella PS5 network shares' $T/root/shares.txt" "after the comments the app wrote"
expect "grep -q '\[net\] shares.txt: saved smb://jason@10.0.0.5/games/Atari%20Games' $T/root/logs/boot.log" "the log says what was saved"
expect "grep -q '\[net\] Connected to smb://jason@10.0.0.5/games/Atari%20Games' $T/root/logs/boot.log" "and the share connects at once, without a restart"
expect "! grep -rqE 'Zq9|pw%26' $T/root/logs $T/out.txt" "the password is in no log and not on stdout"
nosan "$T"
# b. change one share (its note stays), test the connection, remove the other
T=$(newroot t32b)
mkdir -p "$T/share/Sub" && echo x >"$T/share/a.a26"
printf '# my shares\nsmb://old/x   # my note\nnfs://nas/export/atari\n' >"$T/root/shares.txt"
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
press DOWN 3
field bob
press DOWN 6
press CROSS
wait 120
press DOWN
press CROSS
wait 20
press DOWN
press CROSS
press DOWN 7
press CROSS
wait 8
press UP
press CROSS
wait 20
$net_quit
STEPS
)
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "$PAD" "")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "[ \"\$(cat $T/root/shares.txt)\" = \"\$(printf '# my shares\nsmb://bob@old/x   # my note')\" ]" "one line changed (note kept), the other was removed, the comment stays"
expect "grep -q '\[net\] test of smb://bob@old/x: Connected. The folder has 1 file and 1 folder.' $T/root/logs/boot.log" "Test connection reports what is in the folder"
expect "grep -q '\[net\] shares.txt: removed nfs://nas/export/atari' $T/root/logs/boot.log" "the removal is logged"
nosan "$T"
# c. Save with nothing filled in changes nothing
T=$(newroot t32c)
PAD=$(net_pad <<STEPS
frame 40
press TRIANGLE
press UP 5
press CROSS
press CROSS
press DOWN 10
press CROSS
wait 8
press CIRCLE
$net_quit
STEPS
)
rc=$(run "$T" "$PAD" "")
expect "[ $rc = 0 ] && ! grep -q '^smb://\|^nfs://' $T/root/shares.txt" "an empty form is not saved (and Circle on a form with no changes leaves it at once)"
nosan "$T"
# d. over a game (Settings from the pause menu) the row is there but can't be chosen (the game may be reading from a share):
# the pause menu: Down nine times to Settings (a 2600 game: its five switches and buttons come first); there, Up five
# times from Shader passes Network shares and lands on Download covers, which Cross switches off; Circle, then Down four
# times to Quit
T=$(newroot t32d)
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(MAXFLIPS=900 run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS $UP $UP $UP $UP $UP $CROSS $CIRCLE $DOWN $DOWN $DOWN $DOWN $CROSS)" "" "$T/root/roms/test.a26")
expect "[ $rc = 0 ] && grep -q '^covers_download=0' $T/root/stella-ps5.ini" "over a game: Network shares is skipped (Up five times reaches Download covers)"
nosan "$T"
fi

if want 33; then
echo "== 33. shelf tabs: Internal and Network apart, L1 / R1 between them, the last tab kept, Square on Network scans again; L2 / R2 skip 10"
# The tab bar sits at the top: the open tab's pill is the accent colour (78,62,170), the other one dark (32,30,58); (795,30) is
# inside the Internal pill and (1120,30) inside the Network pill (above their names).
tab_pills() { # dump the active tab: Internal | Network
	local f=$1 on=$2
	if [ "$on" = Internal ]; then
		$CHECK "$f" 795 30 78 62 170 >/dev/null && $CHECK "$f" 1120 30 32 30 58 >/dev/null
	else
		$CHECK "$f" 795 30 32 30 58 >/dev/null && $CHECK "$f" 1120 30 78 62 170 >/dev/null
	fi
}
T=$(newroot t33)
mkdir -p "$T/share"
$ROM "$T/root/roms/Local Game (USA).a26" >/dev/null
$ROM "$T/share/Net Game (USA).a26" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;220:$R1;222:0;300:$SQUARE;302:0;380:$L1;382:0;420:$OPTIONS;422:0;430:$CROSS;432:0" "200,260,400")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "tab_pills $T/dump/flip00200.ppm Internal" "the shelf opens on the Internal tab"
expect "tab_pills $T/dump/flip00260.ppm Network" "R1 goes to the Network tab"
expect "tab_pills $T/dump/flip00400.ppm Internal" "L1 comes back"
expect "grep -q '\[shelf\] tab Network: 1 game' $T/root/logs/boot.log" "the Network tab holds the game on the share"
expect "grep -q '\[shelf\] tab Internal: 1 game' $T/root/logs/boot.log" "and the Internal tab only the local one"
expect "grep -q '\[shelf\] network tab: scan again\$' $T/root/logs/boot.log" "Square on the Network tab scans again"
expect "[ \$(grep -c '\[games\] 2 ROM' $T/root/logs/boot.log) -ge 2 ]" "and the scan found both games again"
expect "awk '/network tab: scan again/{f=1;next} f&&/\[covers\]/{c++} f&&/\[shelf\] tab /{exit} END{exit !(f && c == 0)}' $T/root/logs/boot.log" "the same games after the scan: the covers are not loaded again"
expect "grep -q 'last_tab=0' $T/root/stella-ps5.ini" "the tab left on is kept (Internal)"
nosan "$T"
T=$(newroot t33b)
mkdir -p "$T/share"
$ROM "$T/root/roms/Local Game (USA).a26" >/dev/null
$ROM "$T/share/Net Game (USA).a26" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
echo "last_tab=2" >>"$T/root/stella-ps5.ini"
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;300:$OPTIONS;302:0;310:$CROSS;312:0" "40,250")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00040.ppm Network" "with last_tab=2 and a share set up the shelf opens on Network, before the share is up"
expect "tab_pills $T/dump/flip00250.ppm Network" "and stays there when its games arrive"
expect "grep -q 'last_tab=2' $T/root/stella-ps5.ini" "and keeps the tab on quit"
T=$(newroot t33c)
$ROM "$T/root/roms/Local Game (USA).a26" >/dev/null
echo "last_tab=2" >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;60:$R1;62:0;70:$SQUARE;72:0;90:$L1;92:0;120:$OPTIONS;122:0;130:$CROSS;132:0" "40,80,110")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00040.ppm Internal" "last_tab=2 without a share set up opens on the games instead"
expect "tab_pills $T/dump/flip00080.ppm Network && grep -q '\[shelf\] tab Network: 0 game' $T/root/logs/boot.log" "R1 shows the empty Network tab"
expect "grep -q '\[shelf\] network tab: scan again\$' $T/root/logs/boot.log" "Square on it scans again, with no share to ask"
expect "tab_pills $T/dump/flip00110.ppm Internal && grep -q 'last_tab=0' $T/root/stella-ps5.ini" "L1 goes back and the tab is kept"
nosan "$T"
T=$(newroot t33d)
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 STELLAPS5_HOST_NETFS_DIR="$T/nothing" run "$T" "0:0;150:$SQUARE;152:0;330:$OPTIONS;332:0;340:$CROSS;342:0" "100")
expect "[ $rc = 0 ] && tab_pills $T/dump/flip00100.ppm Network" "a share that is down leaves a shelf with no games on the Network tab"
expect "grep -q '\[shelf\] network tab: scan again, reconnecting' $T/root/logs/boot.log" "Square connects it again"
expect "[ \$(grep -c 'The folder games is not on 10.0.0.5' $T/root/logs/boot.log) -ge 2 ]" "and says why each time"
nosan "$T"
# the system tabs (Up / Down) work inside the storage tab; L2 / R2 skip 10 games
T=$(newroot t33e)
mkdir -p "$T/share"
for i in $(seq -w 1 12); do $ROM "$T/root/roms/Game $i (USA).a26" >/dev/null; done
$ROM "$T/root/roms/Zeta (USA).a26" >/dev/null
$ROM "$T/share/Net Game (USA).a26" >/dev/null
$ROM "$T/share/Zap (USA).a78" >/dev/null
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
# (back to the shelf from a game: the pause menu, Up twice, Cross)
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;150:$R2;152:0;170:$CROSS;172:0;$(SEQ 230 $L3R3 $UP $UP $CROSS);320:$R1;322:0;340:$DOWN;342:0;380:$CROSS;382:0;$(QUITAT 480)" "")
expect "[ $rc = 0 ] && grep -q 'loading .*/Game 11 (USA).a26' $T/root/logs/boot.log" "R2 skips 10 games (Game 01 -> Game 11)"
expect "grep -q '\[shelf\] tab Atari 2600: 1 game' $T/root/logs/boot.log && grep -q 'loading /smb/10.0.0.5/games/Net Game (USA).a26' $T/root/logs/boot.log" "on the Network tab, Down shows its Atari 2600 tab, whose game runs"
nosan "$T"
fi

# ---- 7-Zip: games inside a .7z (frontend/fe_7z.cpp, the LZMA SDK's C decoder); the archives are tests/7z/
if want 34; then
echo "== 34. 7z: the reader on every kind of archive; a .7z on the shelf named and covered as its .zip twin, started, run; a 7800 game in a .7z; what is refused, and why; a .7z on a share"
SZ_OUT=$(ASAN_OPTIONS=detect_leaks=1 timeout 120 "$PWD/build/host/test-7z" "$PWD/tests/7z" "$PWD/build/host/test-7z-card.a26" 2>&1)
SZ_RC=$?
echo "$SZ_OUT" | tail -3 | sed 's/^/  /'
expect "[ $SZ_RC = 0 ]" "the 7z reader: LZMA, LZMA2, PPMd, BCJ, Delta, a solid block, the directory's CRCs; empty, truncated, corrupt, BZip2, AES, too big: every check passes"
expect "! echo \"\$SZ_OUT\" | grep -q 'runtime error\|Sanitizer'" "no sanitizer reports"
# a. the same game as a .7z and as a .zip: both named by its CRC (No-Intro's), one cover for both; the .7z runs. A third,
# a 7800 game, is named by the CRC past its 128-byte header
T=$(newroot t34)
mkdir -p "$T/srv/Atari_-_2600/Named_Boxarts" "$T/root/roms/Atari2600"
python3 -c "from PIL import Image; import sys; Image.new('RGB', (512, 357), (0, 255, 0)).save(sys.argv[1])" "$T/srv/Atari_-_2600/Named_Boxarts/Adventure (USA).png"
cp tests/7z/named.7z "$T/root/roms/Atari2600/seven.7z"
cp tests/7z/header.7z "$T/root/roms/unpacked.7z"
$ROM "$T/splat.a26" >/dev/null
python3 tests/forge_crc.py "$T/splat.a26" $(crc_of a26 "Adventure (USA)") >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'splat.a26'); z.close()" "$T/root/roms/Atari2600/zipped.zip" "$T/splat.a26"
PORT=18083
(cd "$T/srv" && exec python3 -m http.server $PORT --bind 127.0.0.1 >/dev/null 2>&1) &
SRVPID=$!
sleep 1
# the shelf: the three games (Adventure twice: by path, seven.7z first); the 7z is selected, Cross starts it
rc=$(OFFLINE= COVER_URL="http://127.0.0.1:$PORT/\${repo}/Named_Boxarts/\${name}.png" STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 run "$T" \
	"0:0;150:$CROSS;152:0;280:$CROSS;320:0;$(QUITAT 400)" "140,300")
kill $SRVPID 2>/dev/null
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'Atari2600/seven.7z \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log && grep -q 'Atari2600/zipped.zip \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log" "the .7z is named by its CRC, as its .zip twin"
expect "grep -q 'unpacked.7z \[a78\] -> \"Asteroids (USA)\" (by CRC)' $T/root/logs/boot.log" "a 7800 game in a .7z: unpacked, named by the CRC past its header"
expect "[ -f '$T/root/covers/Atari2600/Adventure (USA).png' ] && $CHECK $T/dump/flip00140.ppm 960 420 0 255 0 >/dev/null" "and shows the cover the .zip gets"
expect "grep -q 'loading .*seven.7z -> splat.a26' $T/root/logs/boot.log && grep -q 'running \"seven\" (Atari 2600)' $T/root/logs/boot.log" "the game inside the .7z is loaded"
expect "$BANDS $T/dump/flip00300.ppm GRRRRRRRRRRR >/dev/null" "and runs as the .zip's does (Cross -> fire)"
expect "grep -q '^sys:.*seven.7z' $T/root/covers/crc-cache.txt" "the .7z's system is cached as a zip's"
nosan "$T"
# b. a 7800 game after a text file in the archive: its system from the game inside; started as one
T=$(newroot t34b)
cp tests/7z/a78.7z "$T/root/roms/seven78.7z"
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q 'seven78.7z \[a78\]' $T/root/logs/boot.log" "a .a78 in a .7z is listed as a 7800 game"
rc=$(run "$T" "0:0;$(QUITAT 200)" "" "$T/root/roms/seven78.7z")
expect "[ $rc = 0 ] && grep -q 'loading .*seven78.7z -> Seven (USA).a78' $T/root/logs/boot.log && grep -q 'running \"seven78\" (Atari 7800)' $T/root/logs/boot.log" "and starts as one"
nosan "$T"
# c. what the shelf leaves out (nothing it lists, no game, a broken directory) and what says why it can't start
T=$(newroot t34c)
for f in other nogame empty truncated bzip2 aes aeshdr big corrupt lzma; do cp tests/7z/$f.7z "$T/root/roms/$f.7z"; done
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q '\[games\] 5 ROM(s)' $T/root/logs/boot.log" "the shelf lists the five archives with a game in a readable directory"
expect "! grep -q 'other.7z \[\|nogame.7z \[\|empty.7z \[\|truncated.7z \[\|aeshdr.7z \[' $T/root/logs/boot.log" "an archive with nothing the app lists, with no game, a cut or name-encrypted one: not listed"
nosan "$T"
for f in bzip2:"can't unpack" aes:"is encrypted" big:"is 614400 bytes, over 524288 for the Atari 2600" corrupt:"damaged"; do
	n=${f%%:*}; why=${f#*:}
	rc=$(MAXFLIPS=300 run "$T" "0:0;60:$CROSS;62:0" "50" "$T/root/roms/$n.7z")
	expect "grep -q \"could not start .*$n.7z: .*$why\" $T/root/logs/boot.log && [ -f $T/dump/flip00050.ppm ]" "$n.7z: not started; the screen says why ($why)"
	[ $n = big ] && expect "grep -q '\[ui\] message: big.7z is larger than any Atari 2600 game.' $T/root/logs/boot.log" "big.7z (a 600 KiB .a26 inside): the screen says it is larger than any Atari 2600 game"
	nosan "$T"
done
# d. a .7z on a share: listed before it is opened (its folder's name), named by its CRC in the background, started from the share
T=$(newroot t34d)
mkdir -p "$T/share/Atari2600"
cp tests/7z/named.7z "$T/share/Atari2600/seven.7z"
echo "smb://10.0.0.5/games" >"$T/root/shares.txt"
rc=$(STELLAPS5_HOST_REALTIME=1 MAXFLIPS=900 STELLAPS5_HOST_NETFS_DIR="$T/share" run "$T" "0:0;300:$CROSS;302:0;430:$CROSS;470:0;$(QUITAT 520)" "450")
expect "[ $rc = 0 ] && grep -q '\[games\] naming 1 network ROM(s) by CRC' $T/root/logs/boot.log" "the .7z on the share is named in the background (the scan doesn't open it)"
expect "grep -q 'Atari2600/seven.7z \[a26\] -> \"Adventure (USA)\" (by CRC)' $T/root/logs/boot.log" "by the CRC of the game inside"
expect "grep -q 'loading /smb/10.0.0.5/games/Atari2600/seven.7z -> splat.a26' $T/root/logs/boot.log && $BANDS $T/dump/flip00450.ppm GRRRRRRRRRRR >/dev/null" "it starts from the share and runs"
nosan "$T"
fi

# ---- the other systems: Atari 5200 (a5200), 7800 (ProSystem), Lynx (Gearlynx), in the same eboot
if want 35; then
echo "== 35. the Atari 5200 (a5200): the analog stick (proportional), the fire buttons, its BIOS"
T=$(newroot t35)
$ROM "$T/root/roms/test.a52" >/dev/null
# Cross (Fire 1) held; the left stick a third of the way right, then all the way; the D-pad right
rc=$(run "$T" "0:0;100:$CROSS;140:0;160:0,170,128,128,128;200:0,255,128,128,128;240:$RIGHT;280:0;$(QUITAT 300)" "90,130,190,230,270" "$T/root/roms/test.a52")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'loading .*test.a52 (32768 bytes) as an Atari 5200 game (a5200)' $T/root/logs/boot.log && grep -q 'running \"test\" (Atari 5200), 59.9\|running \"test\" (Atari 5200), 60.0' $T/root/logs/boot.log" "a5200 runs it"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "red; Cross -> Fire 1 (green)"
expect "$CHECK $T/dump/flip00190.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00230.ppm 960 540 blue >/dev/null" "the stick is proportional: a third of the way right isn't past the game's mark, all the way is (blue)"
expect "$CHECK $T/dump/flip00270.ppm 960 540 blue >/dev/null" "the D-pad: all the way"
expect "grep -q \"5200.rom not in .*: Altirra's 5200 BIOS (built into a5200) runs\" $T/root/logs/boot.log" "no 5200.rom: the built-in BIOS runs, and the screen says so"
expect "[ -d $T/root/states/Atari5200 ]" "states/Atari5200"
nosan "$T"
# a power cycle (the pause menu: Start, Pause, the keypad, then Settings, Reset, Power cycle: Down nine times): a5200 made
# anew in the same session, the game runs on
rc=$(run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);260:$CROSS;300:0;$(QUITAT 320)" "290" "$T/root/roms/test.a52")
expect "[ $rc = 0 ] && grep -q 'power cycle' $T/root/logs/boot.log && [ \$(grep -c 'running \"test\" (Atari 5200)' $T/root/logs/boot.log) = 2 ] && $CHECK $T/dump/flip00290.ppm 960 540 green >/dev/null" "a power cycle: the game starts again and runs"
nosan "$T"
# a .bin in roms/Atari5200 is a 5200 game; the same at the top of roms/ (no folder to go by, no CRC known) a 2600 one
T=$(newroot t35b)
mkdir -p "$T/root/roms/Atari5200"
$ROM "$T/root/roms/Atari5200/game.bin" 5200 >/dev/null
rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/Atari5200/game.bin")
expect "[ $rc = 0 ] && grep -q 'as an Atari 5200 game (a5200)' $T/root/logs/boot.log && $CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "a .bin in roms/Atari5200: a 5200 game"
nosan "$T"
fi

if want 36; then
echo "== 36. the Atari 7800 (ProSystem): the joystick, its buttons, the difficulty switches; an .a78 and a headerless .bin"
T=$(newroot t36)
$ROM "$T/root/roms/test.a78" >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;160:$RIGHT;200:0;220:$L1;222:0;$(QUITAT 300)" "90,130,190" "$T/root/roms/test.a78")
expect "[ $rc = 0 ]" "exit code 0 (got $rc)"
expect "grep -q 'as an Atari 7800 game (ProSystem)' $T/root/logs/boot.log && grep -q 'running \"test\" (Atari 7800)' $T/root/logs/boot.log" "ProSystem runs it"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null && $CHECK $T/dump/flip00190.ppm 960 540 blue >/dev/null" "red; Cross -> button 1 (green); right (blue)"
expect "grep -q '7800 left difficulty A' $T/root/logs/boot.log" "L1 flips the left difficulty switch"
nosan "$T"
# a power cycle (Pause, Select, Reset, the two difficulty switches, Settings, Reset, Power cycle: Down eleven times)
rc=$(run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);260:$CROSS;300:0;$(QUITAT 320)" "290" "$T/root/roms/test.a78")
expect "[ $rc = 0 ] && grep -q 'power cycle' $T/root/logs/boot.log && [ \$(grep -c 'running \"test\" (Atari 7800)' $T/root/logs/boot.log) = 2 ] && $CHECK $T/dump/flip00290.ppm 960 540 green >/dev/null" "a power cycle: the game starts again and runs"
nosan "$T"
T=$(newroot t36b)
mkdir -p "$T/root/roms/Atari7800"
$ROM "$T/root/roms/Atari7800/game.bin" 7800bin >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "130" "$T/root/roms/Atari7800/game.bin")
expect "[ $rc = 0 ] && grep -q 'as an Atari 7800 game (ProSystem)' $T/root/logs/boot.log && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "a headerless .bin in roms/Atari7800: a 7800 game, runs"
nosan "$T"
fi

if want 37; then
echo "== 37. the Atari Lynx (Gearlynx): its boot ROM (said when missing), A and B, the screen rotation"
T=$(newroot t37)
$ROM "$T/root/roms/test.o" >/dev/null
rc=$(MAXFLIPS=300 run "$T" "0:0;60:$CROSS;62:0;$(SHELFQUIT_AT 100)" "50" "$T/root/roms/test.o")
expect "grep -q 'missing firmware: lynxboot.img (checked before loading)' $T/root/logs/boot.log && ! grep -q 'running \"test\"' $T/root/logs/boot.log && [ -f $T/dump/flip00050.ppm ]" "no lynxboot.img: not started; the screen says which file and where"
mkdir -p "$T/root/bios" && head -c 512 /dev/zero >"$T/root/bios/lynxboot.img"
rc=$(run "$T" "0:0;100:$CIRCLE;140:0;160:$CROSS;200:0;$(QUITAT 300)" "90,130,190" "$T/root/roms/test.o")
expect "[ $rc = 0 ] && grep -q 'as an Atari Lynx game (Gearlynx)' $T/root/logs/boot.log && grep -q 'running \"test\" (Atari Lynx)' $T/root/logs/boot.log" "with bios/lynxboot.img: Gearlynx runs it (a BS93 homebrew, loaded into RAM)"
expect "$CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null && $CHECK $T/dump/flip00190.ppm 960 540 blue >/dev/null" "red; Circle -> A (green); Cross -> B (blue)"
expect "grep -q 'picture 160x102' $T/root/logs/boot.log" "the Lynx's 160x102 screen"
nosan "$T"
# a power cycle (Pause, Option 1, Option 2, Settings, Reset, Power cycle: Down nine times)
rc=$(run "$T" "0:0;$(SEQ 100 $L3R3 $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $DOWN $CROSS);260:$CIRCLE;300:0;$(QUITAT 320)" "290" "$T/root/roms/test.o")
expect "[ $rc = 0 ] && grep -q 'power cycle' $T/root/logs/boot.log && [ \$(grep -c 'running \"test\" (Atari Lynx)' $T/root/logs/boot.log) = 2 ] && $CHECK $T/dump/flip00290.ppm 960 540 green >/dev/null" "a power cycle: the game starts again and runs"
nosan "$T"
T=$(newroot t37r)
mkdir -p "$T/root/bios" && head -c 512 /dev/zero >"$T/root/bios/lynxboot.img"
$ROM "$T/root/roms/test.o" >/dev/null
echo "lnx_rotation=2" >>"$T/root/stella-ps5.ini"
rc=$(run "$T" "0:0;$(QUITAT 100)" "90" "$T/root/roms/test.o")
expect "[ $rc = 0 ] && grep -q 'picture 102x160' $T/root/logs/boot.log && $CHECK $T/dump/flip00090.ppm 960 540 red >/dev/null" "Screen rotation: the picture turned (102x160)"
nosan "$T"
fi

# ---- updates: the helper checks GitHub's releases, downloads, checks every file, installs (Beetle PCE PS5 1.1.1's
# updater, with Media Player Classic PS5 1.5's ShadowMount+ fix: the update goes to the folder the console runs the app
# from; an image or a read-only folder is said before anything is downloaded)
if want 38; then
echo "== 38. updates from GitHub's releases (Settings, SYSTEM, Updates): the helper checks, downloads, checks every file, installs"
T=$(newroot t38)
UPD="python3 tests/update_client.py $STELLAPS5_HELPER_PORT"
stop38() { stop_helpers; pkill -f 'http.server 1[89][0-9][0-9][0-9]' 2>/dev/null; sleep 0.3; }
stop38
SRV=$T/srv; mkdir -p "$SRV" "$T/appmeta/PPSA99927"; echo old >"$T/appmeta/PPSA99927/icon0.png"
SPORT=$((18000 + RANDOM % 1900))
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
# the release: the installed app with a new eboot, a newer param.json, a new file; the old manifest lists a file that goes
release() { # tag contentVersion [bad|nodigest]
	rm -rf "$T/rel" && mkdir -p "$T/rel" && cp -r "$T/homebrew/PPSA99927" "$T/rel/PPSA99927" && rm -f "$T/rel/PPSA99927/manifest.sha256" "$T/rel/PPSA99927/sce_sys/old.bin"
	echo "eboot $1" >"$T/rel/PPSA99927/eboot.bin"; echo "new art" >"$T/rel/PPSA99927/sce_sys/icon0.png"; echo extra >"$T/rel/PPSA99927/sce_sys/extra.bin"; echo "notices $1" >>"$T/rel/PPSA99927/licenses/NOTICES.txt"
	sed -i "s/\"contentVersion\": \"[0-9.]*\"/\"contentVersion\": \"$2\"/" "$T/rel/PPSA99927/sce_sys/param.json"
	python3 tools/make_release_zip.py "$T/rel/PPSA99927" PPSA99927 "$SRV/StellaPS5-$1-PPSA99927.zip" >/dev/null
	local sha size
	sha=$(sha256sum "$SRV/StellaPS5-$1-PPSA99927.zip" | cut -c1-64); size=$(stat -c %s "$SRV/StellaPS5-$1-PPSA99927.zip")
	[ "${3:-}" = bad ] && sha=0000${sha:4}
	local digest="\"digest\":\"sha256:$sha\","
	[ "${3:-}" = nodigest ] && digest=""
	printf '{"tag_name":"%s","name":"Stella PS5 %s","assets":[{"name":"StellaPS5-%s.elf","size":1,"browser_download_url":"http://127.0.0.1:%s/x.elf"},{"name":"StellaPS5-%s-PPSA99927.zip","size":%s,%s"browser_download_url":"http://127.0.0.1:%s/StellaPS5-%s-PPSA99927.zip"}]}' \
		"$1" "$1" "$1" $SPORT "$1" $size "$digest" $SPORT "$1" >"$SRV/latest.json"
}
# the app installed by the payload (1.0), which then stays as the helper; the updater asks the test server
STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T/inst.txt" 2>&1 &
expect "waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'" "the payload installed 1.0 and serves as the helper"
expect "[ \"\$($UPD status 1.0 | cut -d'|' -f1)\" = idle ]" "the updater answers: idle"
expect "grep -q \"\[update\] the app's folder is $T/homebrew/PPSA99927\" $T/root/logs/installer.log || grep -q '\[update\] .*$T/homebrew/PPSA99927' $T/root/logs/installer.log" "the helper looks up where the app runs from: /data/homebrew (no ShadowMount+ mount)"
release v1.0 01.000.000
expect "$UPD check 1.0 --until uptodate,failed | grep -q '^uptodate|1.0|'" "the newest release is this version: up to date"
echo gone >"$T/homebrew/PPSA99927/sce_sys/old.bin"
printf '%s *sce_sys/old.bin\n' "$(sha256sum <"$T/homebrew/PPSA99927/sce_sys/old.bin" | cut -c1-64)" >"$T/homebrew/PPSA99927/manifest.sha256"
release v9.9 99.000.000 bad
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "a newer release (9.9): available, with its size"
cp "$T/homebrew/PPSA99927/eboot.bin" "$T/eboot.before"
expect "$UPD install 1.0 --until done,failed | grep -q \"^failed|.*SHA-256 doesn't match\"" "a download whose SHA-256 isn't GitHub's: refused"
expect "cmp -s $T/homebrew/PPSA99927/eboot.bin $T/eboot.before && [ -z \"\$(find $T/homebrew -name '*.new')\" ]" "and nothing was changed"
release v9.9 99.000.000
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "checked again"
expect "$UPD install 1.0 --until done,failed | grep -q '^done|9.9|'" "downloaded, every file checked, installed"
expect "grep -q 'eboot v9.9' $T/homebrew/PPSA99927/eboot.bin && grep -q '\"contentVersion\": \"99.000.000\"' $T/homebrew/PPSA99927/sce_sys/param.json" "the new eboot.bin and param.json in place"
expect "[ -f $T/homebrew/PPSA99927/sce_sys/extra.bin ] && [ ! -e $T/homebrew/PPSA99927/sce_sys/old.bin ] && grep -q 'sce_sys/extra.bin' $T/homebrew/PPSA99927/manifest.sha256" "a new file added, one the old manifest had removed, the manifest kept"
expect "tail -n1 $T/homebrew/PPSA99927/licenses/NOTICES.txt | grep -qx 'notices v9.9' && grep -q ' \\*licenses/COPYING.txt\$' $T/homebrew/PPSA99927/manifest.sha256" "the licenses/ folder in the release zip: updated and in the manifest"
expect "grep -q 'new art' $T/appmeta/PPSA99927/icon0.png && [ -z \"\$(find $T/homebrew -name '*.new')\" ]" "the home screen's copy of the art refreshed; no .new files left"
expect "$UPD status 9.9 | grep -q '^done|'" "the result stays until the app is opened again"
stop38
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
# the old payload sent again (an autoload): the newer version is left alone
STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T/inst2.txt" 2>&1 &
expect "waitfor $T/inst2.txt 'a newer version is installed'" "an older payload sent again: says a newer version is installed"
expect "grep -q 'eboot v9.9' $T/homebrew/PPSA99927/eboot.bin && grep -q 'new art' $T/appmeta/PPSA99927/icon0.png" "and leaves the update (and its art) as they are"
expect "$UPD check 9.9 --until uptodate,available,failed | grep -q '^uptodate|9.9|'" "the app says which version it is: an older helper finds it up to date"
stop38
# the app: at start the daily check (the helper finds 9.9); Settings (Triangle), Up twice to "Updates" (it shows
# "Install 9.9"), Cross: the helper installs it; Cross again: the app restarts into the new eboot
T2=$(newroot t38b)
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_ROOT=$T2/root STELLAPS5_HOMEBREW=$T2/homebrew STELLAPS5_APPMETA=$T2/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T2/inst.txt" 2>&1 &
waitfor $T2/root/logs/installer.log 'listening on 127.0.0.1'
$ROM "$T2/root/roms/test.a26" >/dev/null
# (the script waits for the states, not a number of flips: the check's answer before Cross on Install, the end of
# the install before Cross on Installed: restart)
rc=$(STELLAPS5_HOST_PAD_GATE="55:$T2/root/logs/boot.log:the helper's state: available 9.9;149:$T2/root/logs/boot.log:[update] 9.9 installed" STELLAPS5_HOST_REALTIME=1 STELLAPS5_APPMETA=$T2/appmeta run "$T2" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;150:$CROSS;152:0;$(SHELFQUIT_AT 300)" "54,140")
expect "grep -q 'daily check asked of the helper' $T2/root/logs/boot.log && [ -f $T2/root/update-check.txt ]" "at start: the daily check asked of the helper"
expect "grep -q 'update install' $T2/root/logs/installer.log && grep -q 'eboot v9.9' $T2/homebrew/PPSA99927/eboot.bin" "Cross on \"Install 9.9\": the helper installs it"
expect "grep -q 'is available: Settings, SYSTEM, Updates' $T2/inst.txt $T2/root/logs/installer.log" "the daily check found 9.9: a notification says so"
expect "grep -q \"sceSystemServiceLoadExec($T2/homebrew/PPSA99927/eboot.bin)\" $T2/out.txt && grep -q '^9.9$' $T2/root/update-pending.txt" "Cross on \"Installed: restart\": the app restarts into the new eboot.bin; the next start checks the version"
nosan "$T2"
# the start after: still 1.0 although 9.9 was installed (ShadowMount+ still shows the old files): the app says so and
# what to do; a start that runs the version noted: said once, the note gone
rc=$(run "$T2" "0:0;60:$CROSS;62:0;$(SHELFQUIT_AT 100)" "")
expect "[ $rc = 0 ] && grep -q \"\[update\] after the update to 9.9 (in $T2/homebrew/PPSA99927): this is 1.0: still the old version\" $T2/root/logs/boot.log && grep -q '\[ui\] message: Stella PS5 9.9 was installed in $T2/homebrew/PPSA99927, but this is still 1.0' $T2/root/logs/boot.log" "the next start: still the old version: the app says so (close it, ShadowMount+ again, or restart)"
printf '1.0\n/x\n0\n' >$T2/root/update-pending.txt
rc=$(run "$T2" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q 'this is 1.0: the new version runs' $T2/root/logs/boot.log && grep -q 'Stella PS5 updated to 1.0.' $T2/out.txt && [ ! -e $T2/root/update-pending.txt ]" "an update seen running: said once, the note gone"
stop38
# a release whose zip GitHub gives no SHA-256 for (no "digest"): refused before anything is downloaded (as Media Player
# Classic PS5 1.5); the app shows why, translated (German here: no text left untranslated)
T3=$(newroot t38c)
release v9.9 99.000.000 nodigest
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_ROOT=$T3/root STELLAPS5_HOMEBREW=$T3/homebrew STELLAPS5_APPMETA=$T3/appmeta \
	ASAN_OPTIONS=detect_leaks=0 timeout 120 "$INSTALLER" >"$T3/inst.txt" 2>&1 &
waitfor $T3/root/logs/installer.log 'listening on 127.0.0.1'
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "a release without a SHA-256 from GitHub: found (9.9)"
expect "$UPD install 1.0 --until done,failed | grep -q \"^failed|.*GitHub gave no SHA-256 for the download, so it can't be checked: nothing was changed\"" "... refused: GitHub gave no SHA-256, so it can't be checked"
expect "! grep -q 'downloading http' $T3/root/logs/installer.log && grep -q '\"contentVersion\": \"01.000.000\"' $T3/homebrew/PPSA99927/sce_sys/param.json" "... before anything is downloaded; nothing changed"
echo "language=4" >>"$T3/root/stella-ps5.ini"; date +%s >"$T3/root/update-check.txt"
$ROM "$T3/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_I18N_MISSING=$T3/missing.txt run "$T3" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;100:$CIRCLE;102:0;$(SHELFQUIT_AT 140)" "90")
untranslated $T3/missing.txt > $T3/untranslated.txt
expect "[ $rc = 0 ] && grep -q \"the helper's state: failed\" $T3/root/logs/boot.log && ! grep -q 'SHA-256' $T3/missing.txt && [ ! -s $T3/untranslated.txt ]" "the app (German) shows why, translated ($(head -c 200 $T3/untranslated.txt | tr '\n' '|'))"
nosan "$T3"
stop38
fi

if want 39; then
echo "== 39. ShadowMount+ (Media Player Classic PS5 1.5's fix): the update goes where the console runs the app from; images and read-only folders are said, not written"
stop39() { stop_helpers; pkill -f 'http.server 1[89][0-9][0-9][0-9]' 2>/dev/null; sleep 0.3; }
stop39
T=$(newroot t39)
# the mount table's names are cut at 87 bytes, as the console's: with a long work dir the folders live under a short one
M=$T; [ ${#T} -le 56 ] || M=$(mktemp -d /tmp/stella39.XXXXXX)
SX=$M/sysex; U=$M/usb0/homebrew/PPSA99927
# the app as the payload installs it, then a copy on a USB drive that ShadowMount+ runs (nullfs-mounted on /system_ex/app)
STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$T/inst0.txt" 2>&1 &
waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'
stop39
rm -rf $SX $M/usb0 && mkdir -p $SX/app $M/usb0/homebrew && cp -r $T/homebrew/PPSA99927 $U
sums() { ( cd $1 && find . -type f | sort | xargs sha256sum ); }
ver() { sed -i "s/\"contentVersion\": \"[0-9.]*\"/\"contentVersion\": \"$2\"/" $1/sce_sys/param.json; }
ln -s $U $SX/app/PPSA99927
MNT="$SX/app/PPSA99927|nullfs|$U|rw;$M/usb0|exfatfs|/dev/da0s1|rw"
# the payload with env ($2...) set, its output in $T/$1.txt (until its helper listens)
inst39() {
	local out=$T/$1.txt; shift
	env STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/shadowmnt STELLAPS5_USER_APP=$M/userapp STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$M/hb2 STELLAPS5_APPMETA=$T/appmeta "$@" ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$out" 2>&1 &
	for i in $(seq 1 100); do grep -q 'listening on 127.0.0.1' $out 2>/dev/null && break; sleep 0.1; done
	stop39
}
# a. the payload with ShadowMount+ running the app from that USB folder: it brings that copy up to date (through the
# mount), not a second one in /data/homebrew
echo "eboot usb old" >$U/eboot.bin; ver $U 00.900.000
inst39 inst1 STELLAPS5_HOST_MOUNTS="$MNT"
expect "grep -q 'updated to 1.0 (in $U, the copy ShadowMount+ runs)' $T/inst1.txt && ! grep -q 'eboot usb old' $U/eboot.bin && grep -q '\"contentVersion\": \"01.000.000\"' $U/sce_sys/param.json && [ ! -e $M/hb2 ]" "the payload: the copy ShadowMount+ runs (a USB folder) updated through the mount, no second copy"
# b. the helper's updates go there too: a release installed through the mount, /data/homebrew's copy untouched
SRV=$T/srv; mkdir -p $SRV
SPORT=$((18000 + RANDOM % 1900))
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
rm -rf $T/rel && mkdir -p $T/rel && cp -r $U $T/rel/PPSA99927 && rm -f $T/rel/PPSA99927/manifest.sha256
echo "eboot v9.9" >$T/rel/PPSA99927/eboot.bin; ver $T/rel/PPSA99927 99.000.000
python3 tools/make_release_zip.py $T/rel/PPSA99927 PPSA99927 $SRV/StellaPS5-v9.9-PPSA99927.zip >/dev/null
sha=$(sha256sum $SRV/StellaPS5-v9.9-PPSA99927.zip | cut -c1-64); size=$(stat -c %s $SRV/StellaPS5-v9.9-PPSA99927.zip)
printf '{"tag_name":"v9.9","assets":[{"name":"StellaPS5-v9.9-PPSA99927.zip","size":%s,"digest":"sha256:%s","browser_download_url":"http://127.0.0.1:%s/StellaPS5-v9.9-PPSA99927.zip"}]}' $size $sha $SPORT >$SRV/latest.json
sums $T/homebrew/PPSA99927 >$T/home.sums
UPD="python3 tests/update_client.py $STELLAPS5_HELPER_PORT"
helper39() { # the helper (the payload, the app up to date) with env ($@)
	env STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/shadowmnt STELLAPS5_USER_APP=$M/userapp STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta "$@" ASAN_OPTIONS=detect_leaks=0 timeout 120 "$HELPER" >"$T/helper.txt" 2>&1 &
	sleep 0.3
	waitfor $T/root/logs/helper.log 'listening on 127.0.0.1'
}
rm -f $T/root/logs/helper.log
helper39 STELLAPS5_HOST_MOUNTS="$MNT"
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|' && $UPD install 1.0 --until done,failed | grep -q '^done|9.9|'" "the helper: 9.9 found, installed"
expect "grep -q \"$SX/app/PPSA99927 is a nullfs mount of $U (on $M/usb0, exfatfs, from /dev/da0s1): files go through $SX/app/PPSA99927\" $T/root/logs/helper.log" "the app runs from a USB drive's folder (a nullfs mount): found, written through the mount"
expect "grep -q 'eboot v9.9' $U/eboot.bin && grep -q '\"contentVersion\": \"99.000.000\"' $U/sce_sys/param.json" "... the update is in that folder"
expect "sums $T/homebrew/PPSA99927 | cmp -s - $T/home.sums" "... /data/homebrew's copy untouched (no second copy written)"
stop39
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
# c. from an image ShadowMount+ mounted (read-only): the helper refuses to write it; the app says so before downloading
I=$M/shadowmnt/img1/PPSA99927
rm -rf $M/shadowmnt $SX/app/PPSA99927 && mkdir -p $M/shadowmnt/img1 && cp -r $U $I && ln -s $I $SX/app/PPSA99927
ver $I 01.000.000; sums $I >$T/img.sums
IMNT="$SX/app/PPSA99927|nullfs|$I|rw;$M/shadowmnt/img1|ufs|/dev/lvd2|ro"
rm -f $T/root/logs/helper.log
helper39 STELLAPS5_HOST_MOUNTS="$IMNT"
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|' && $UPD install 1.0 --until done,failed | grep -q '^failed|.*image that ShadowMount+ mounted'" "the helper: an image ShadowMount+ mounted can't take the update: refused"
expect "sums $I | cmp -s - $T/img.sums && ! grep -q 'GET .*\.zip' $T/root/logs/helper.log" "... nothing downloaded, nothing written"
$ROM "$T/root/roms/test.a26" >/dev/null
rc=$(STELLAPS5_HOST_PAD_GATE="55:$T/root/logs/boot.log:the helper's state: available 9.9" STELLAPS5_HOST_REALTIME=1 STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/shadowmnt STELLAPS5_USER_APP=$M/userapp STELLAPS5_HOST_MOUNTS="$IMNT" run "$T" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;100:$CROSS;102:0;120:$CIRCLE;122:0;$(SHELFQUIT_AT 200)" "90")
expect "[ $rc = 0 ] && grep -q '\[ui\] message: Stella PS5 9.9 is available (you have 1.0).' $T/root/logs/boot.log && grep -q 'This copy runs from an image that ShadowMount+ mounted ($I)' $T/root/logs/boot.log" "the app: Updates says the image can't take it, before anything is downloaded"
expect "grep -q '9.9 is available, but it can.t be installed here' $T/root/logs/boot.log && sums $I | cmp -s - $T/img.sums" "... and nothing was written"
nosan "$T"
cp $T/root/logs/boot.log $T/boot-c.log; cp $T/out.txt $T/out-c.txt
stop39
# d. the payload and that image: with a newer version (the app is newer), this one (ready), an older one (says it can't be
# written there); nothing written, the helper runs
ver $I 99.000.000; sums $I >$T/img.sums
inst39 inst4 STELLAPS5_HOST_MOUNTS="$IMNT"
expect "grep -q 'a newer version is installed; this payload (1.0) left it as it is' $T/inst4.txt && ! grep -q 'can.t be written' $T/inst4.txt && grep -q 'listening on 127.0.0.1' $T/inst4.txt && sums $I | cmp -s - $T/img.sums && [ ! -e $M/hb2 ]" "the payload and an image with a newer version: says the app is newer, writes nothing, the helper runs"
ver $I 01.000.000; sums $I >$T/img.sums
inst39 inst5 STELLAPS5_HOST_MOUNTS="$IMNT"
expect "grep -q 'Stella PS5 1.0 is ready' $T/inst5.txt && ! grep -q 'can.t be written' $T/inst5.txt && sums $I | cmp -s - $T/img.sums && [ ! -e $M/hb2 ]" "... with this version: ready, nothing written"
ver $I 00.900.000; sums $I >$T/img.sums
inst39 inst6 STELLAPS5_HOST_MOUNTS="$IMNT"
expect "grep -q 'runs from an image that ShadowMount+ mounted ($I): an update can.t be written into it' $T/inst6.txt && grep -q 'listening on 127.0.0.1' $T/inst6.txt && sums $I | cmp -s - $T/img.sums && [ ! -e $M/hb2 ]" "... with an older version: says it can't be written there, writes nothing, the helper runs"
# e. a read-only USB folder: the app says so in its words
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
sleep 0.5
rm -f $T/root/logs/boot.log
helper39 STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$I|rw;$M/shadowmnt/img1|exfatfs|/dev/da1s1|ro" STELLAPS5_SHADOWMNT=$M/no-images
rc=$(STELLAPS5_HOST_PAD_GATE="159:$T/root/logs/boot.log:the helper's state: available 9.9" STELLAPS5_HOST_REALTIME=1 STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/no-images STELLAPS5_USER_APP=$M/userapp STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$I|rw;$M/shadowmnt/img1|exfatfs|/dev/da1s1|ro" run "$T" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;160:$CROSS;162:0;200:$CROSS;202:0;220:$CIRCLE;222:0;$(SHELFQUIT_AT 300)" "")
expect "grep -q \"This copy runs from $I, where the app can't write\" $T/root/logs/boot.log && sums $I | cmp -s - $T/img.sums" "a read-only folder: said before downloading, nothing written"
cp $T/root/logs/boot.log $T/boot-e.log
stop39
# f. nothing written where it isn't sure to be the app the console runs: a folder whose param.json names another title
O=$M/other/PPSA99927
rm -rf $M/other $SX/app/PPSA99927 && mkdir -p $M/other && cp -r $U $O && ln -s $O $SX/app/PPSA99927
sed -i 's/"titleId": "PPSA99927"/"titleId": "PPSA00001"/' $O/sce_sys/param.json; ver $O 00.900.000; sums $O >$T/o.sums
inst39 inst7 STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$O|rw"
expect "grep -q 'mounted on $SX/app/PPSA99927 can.t be identified for sure' $T/inst7.txt && sums $O | cmp -s - $T/o.sums && [ ! -e $M/hb2 ]" "a mounted folder whose param.json names another title: the payload writes nothing (no second copy)"
rm -f $T/root/logs/boot.log
# (the app asks the helper, which reads the mount table: a title can't call statfs / getfsstat, see k)
helper39 STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$O|rw"
rc=$(STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/shadowmnt STELLAPS5_USER_APP=$M/userapp run "$T" "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;80:$CIRCLE;82:0;$(SHELFQUIT_AT 120)" "")
stop39
expect "grep -q '\[update\] (from the helper) $SX/app/PPSA99927 is a nullfs mount of $O.*: not written' $T/root/logs/boot.log" "... the app (the helper's answer) doesn't take it for its own either"
# g. a backport's unionfs over ShadowMount+'s nullfs mount: the folder beneath is written directly, not through the layer
rm -rf $SX/app/PPSA99927 && ln -s $U $SX/app/PPSA99927; ver $U 00.900.000
inst39 inst8 STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$U|rw;$M/usb0|exfatfs|/dev/da0s1|rw;$SX/app/PPSA99927|unionfs|$M/backport|rw"
expect "grep -q \"$SX/app/PPSA99927 is a unionfs mount over a nullfs mount of $U (on $M/usb0, exfatfs, from /dev/da0s1): files go through $U\" $T/inst8.txt && grep -q '\"contentVersion\": \"01.000.000\"' $U/sce_sys/param.json && [ ! -e $M/hb2 ]" "a unionfs layer over the mount: the folder beneath written directly"
# h. statfs fails: the mount table (getfsstat) says what is mounted; neither answering: nothing written
ver $U 00.900.000
inst39 inst10 STELLAPS5_HOST_NO_STATFS=1 STELLAPS5_HOST_MOUNTS="$MNT"
expect "grep -q \"$SX/app/PPSA99927 is a nullfs mount of $U (on $M/usb0, exfatfs, from /dev/da0s1): files go through $SX/app/PPSA99927\" $T/inst10.txt && grep -q '\"contentVersion\": \"01.000.000\"' $U/sce_sys/param.json" "statfs failing: the mount table finds the mount"
ver $U 00.900.000; sums $U >$T/u.sums
inst39 inst11 STELLAPS5_HOST_NO_STATFS=1 STELLAPS5_HOST_NO_GETFSSTAT=1 STELLAPS5_HOST_MOUNTS="$MNT"
expect "grep -q 'what is mounted on $SX/app/PPSA99927 can.t be read' $T/inst11.txt && sums $U | cmp -s - $T/u.sums && [ ! -e $M/hb2 ]" "neither statfs nor the mount table answering: nothing written, no second copy"
# i. the payload before ShadowMount+ has mounted the app (autoload order): the folder of ShadowMount+'s link
# /user/app/PPSA99927/mount.lnk is updated; a link to a folder not there: nothing; an image not mounted yet: ready
rm -rf $SX/app/PPSA99927 $M/userapp && mkdir -p $M/userapp/PPSA99927 && echo "$U" >$M/userapp/PPSA99927/mount.lnk
inst39 inst12 STELLAPS5_HOST_MOUNTS="$M/usb0|exfatfs|/dev/da0s1|rw"
expect "grep -q 'updated to 1.0 (in $U, the copy ShadowMount+ runs)' $T/inst12.txt && grep -q '\"contentVersion\": \"01.000.000\"' $U/sce_sys/param.json && [ ! -e $M/hb2 ]" "no mount yet: the folder of ShadowMount+'s mount.lnk updated, no second copy"
echo "$M/gone/PPSA99927" >$M/userapp/PPSA99927/mount.lnk
inst39 inst13
expect "grep -q 'runs the app from $M/gone/PPSA99927, which isn.t there now' $T/inst13.txt && grep -q 'listening on 127.0.0.1' $T/inst13.txt && [ ! -e $M/hb2 ] && [ ! -e $M/gone ]" "... its folder not there (a drive not connected yet): nothing written, no second copy"
echo "$M/shadowmnt/img2/PPSA99927" >$M/userapp/PPSA99927/mount.lnk
inst39 inst15
expect "grep -q 'Stella PS5 1.0 is ready' $T/inst15.txt && ! grep -q 'isn.t there now' $T/inst15.txt && [ ! -e $M/hb2 ] && [ ! -e $M/shadowmnt/img2 ]" "... a link to an image not mounted yet: ready, nothing written"
echo "PPSA99927" >$M/userapp/PPSA99927/mount.lnk
inst39 inst17
expect "grep -q 'link holds \"PPSA99927\", not a folder.s path: ignored' $T/inst17.txt && grep -q 'Stella PS5 1.0 installed' $T/inst17.txt && [ -f $M/hb2/PPSA99927/eboot.bin ]" "... a link that isn't a path: ignored, installed in /data/homebrew"
rm -rf $M/hb2 $M/userapp
# j. extended storage on an LVD device but writable: not taken for an image; written
mkdir -p $SX/app && rm -rf $SX/app/PPSA99927 && ln -s $U $SX/app/PPSA99927; ver $U 00.900.000
inst39 inst14 STELLAPS5_HOST_MOUNTS="$SX/app/PPSA99927|nullfs|$U|rw;$M/usb0|ufs|/dev/lvd5|rw"
expect "grep -q 'updated to 1.0 (in $U, the copy ShadowMount+ runs)' $T/inst14.txt && grep -q '\"contentVersion\": \"01.000.000\"' $U/sce_sys/param.json" "a writable folder on an LVD device: updated (not taken for an image)"
# k. the app doesn't read the mount table: statfs / getfsstat are only in the SDK's libkernel_sys, which a title can't
# load (Media Player Classic PS5 1.5 imported it and crashed at launch, its #3; group 45 checks the eboot). It asks the
# helper (c, e, f above). Without the helper's answer -- a helper that doesn't know the request (it closes the
# connection), or none -- ShadowMount+'s link is taken when its folder is what /system_ex/app shows (param.json, the same
# eboot.bin), written directly, never through the mount; without a link, or with one to another folder, the app says it
# can't tell and nothing is written
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
sleep 0.5
rm -rf $SX/app/PPSA99927 && ln -s $U $SX/app/PPSA99927; ver $U 00.900.000; echo "eboot usb old" >$U/eboot.bin; rm -f $U/manifest.sha256
mkdir -p $M/userapp/PPSA99927 && echo "$U" >$M/userapp/PPSA99927/mount.lnk
# the app: Settings, Updates (9.9 available), Cross: Install (or the message why not, closed by Cross), back, quit
app39k() { # pad, env...
	local pad=$1; shift
	rm -f $T/root/logs/boot.log
	( export STELLAPS5_HOST_PAD_GATE="55:$T/root/logs/boot.log:the helper's state: available 9.9" STELLAPS5_HOST_REALTIME=1 \
		STELLAPS5_SYSTEM_EX=$SX STELLAPS5_SHADOWMNT=$M/shadowmnt STELLAPS5_USER_APP=$M/userapp "$@"; run "$T" "$pad" "" )
}
KPAD="0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;100:$CROSS;102:0;120:$CIRCLE;122:0;$(SHELFQUIT_AT 200)"
helper39 STELLAPS5_HOST_MOUNTS="$MNT" STELLAPS5_HOST_HELPER_OLD=1
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "k: a helper that doesn't know the request: 9.9 available"
rc=$(app39k "0:0;30:$TRIANGLE;32:0;40:$UP;42:0;46:$UP;48:0;56:$CROSS;58:0;120:$CIRCLE;122:0;$(SHELFQUIT_AT 200)")
waitfor $T/root/logs/helper.log 'PPSA99927 installed\|installed in'
expect "[ $rc = 0 ] && grep -q \"\[update\] the helper didn't say where the app is: it closed the connection\" $T/root/logs/boot.log && grep -q \"\[update\] $SX/app/PPSA99927 shows the app (no mount table); ShadowMount+'s link: $U ($M/userapp/PPSA99927/mount.lnk)\" $T/root/logs/boot.log && ! grep -q \"can't be installed here\" $T/root/logs/boot.log" "... the app: ShadowMount+'s link, the same eboot.bin as the mount shows: taken, the update allowed"
expect "grep -q 'eboot v9.9' $U/eboot.bin && grep -q '\"contentVersion\": \"99.000.000\"' $U/sce_sys/param.json" "... the helper installs it there"
stop39
# (the next starts don't say "9.9 was installed, but this is still 1.0")
rm -f $T/root/update-pending.txt
ver $U 00.900.000; echo "eboot usb old" >$U/eboot.bin; rm -f $U/manifest.sha256; sums $U >$T/u.sums
# no helper to answer (that request goes to a port nobody listens on) and no ShadowMount+ link
rm -f $M/userapp/PPSA99927/mount.lnk
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
sleep 0.5
helper39 STELLAPS5_HOST_MOUNTS="$MNT"
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "k: 9.9 available"
rc=$(app39k "$KPAD" STELLAPS5_HOST_APPPLACE_PORT=$((STELLAPS5_HELPER_PORT + 7)))
stop39; cp $T/root/logs/boot.log $T/boot-k2.log
expect "[ $rc = 0 ] && grep -q '\[update\] no helper on port .* to ask where the app is' $T/root/logs/boot.log && grep -q 'no ShadowMount+ link: not written' $T/root/logs/boot.log && grep -q \"can't be installed here: Stella PS5: the folder this copy runs from can't be told\" $T/root/logs/boot.log && sums $U | cmp -s - $T/u.sums" "... no helper's answer and no ShadowMount+ link: the app says it can't tell, nothing written"
# a link to another folder than the one the mount shows (another eboot.bin)
O2=$M/other2/PPSA99927
rm -rf $M/other2 && mkdir -p $M/other2 && cp -r $U $O2 && echo "another eboot" >$O2/eboot.bin && sums $O2 >$T/o2.sums
echo "$O2" >$M/userapp/PPSA99927/mount.lnk
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
sleep 0.5
helper39 STELLAPS5_HOST_MOUNTS="$MNT" STELLAPS5_HOST_HELPER_OLD=1
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|'" "k: 9.9 available"
rc=$(app39k "$KPAD")
stop39; cp $T/root/logs/boot.log $T/boot-k3.log
expect "[ $rc = 0 ] && grep -q \"ShadowMount+'s link: $O2 .*: not the folder the mount shows\" $T/root/logs/boot.log && grep -q \"can't be installed here\" $T/root/logs/boot.log && sums $U | cmp -s - $T/u.sums && sums $O2 | cmp -s - $T/o2.sums" "... a link to another folder than the one shown (another eboot.bin): the app says it can't tell, nothing written"
rm -rf $M/userapp $M/other2
# l. the helper's answer checked: a folder whose param.json names another title is not written, whatever the helper
# says; a helper that took the unlock request and never answered isn't asked again (the start doesn't wait 5 s more);
# one that answers the unlock but not this request: "no answer in 5 s", told apart from one that closes the connection
O3=$M/other3/PPSA99927
rm -rf $M/other3 && mkdir -p $M/other3 && cp -r $U $O3 && sed -i 's/"titleId": "PPSA99927"/"titleId": "PPSA00001"/' $O3/sce_sys/param.json && sums $O3 >$T/o3.sums
printf 'place=1\nmount=\nsource=%s\ndir=%s\nfs=exfatfs\nwritable=1\nimage=0\nunsure=0\nvia_link=0\nimage_pending=0\nreason=\nlog=%s is the app\n' $O3 $O3 $O3 >$T/place.txt
helper39 STELLAPS5_HOST_HELPER_PLACE=$T/place.txt
rm -f $T/root/logs/boot.log
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
stop39; cp $T/root/logs/boot.log $T/boot-l1.log
expect "[ $rc = 0 ] && grep -q \"\[update\] (from the helper) $O3 is the app; but $O3 isn't this app's (its param.json doesn't name PPSA99927): not written\" $T/root/logs/boot.log && sums $O3 | cmp -s - $T/o3.sums" "l: a helper answering another title's folder: not taken (not writable), nothing written"
rm -rf $M/other3
python3 -c "
import socket, sys, time
s = socket.socket(); s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1); s.bind(('127.0.0.1', int(sys.argv[1]))); s.listen(8)
held = []
while True:
    c, _ = s.accept(); held.append(c)  # takes every request, never answers
" $STELLAPS5_HELPER_PORT >/dev/null 2>&1 &
MUTE=$!
sleep 0.5
rm -f $T/root/logs/boot.log
rc=$(MAXFLIPS=200 run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
kill $MUTE 2>/dev/null; wait $MUTE 2>/dev/null
expect "[ $rc = 0 ] && grep -q 'Stella helper (port $STELLAPS5_HELPER_PORT): no answer in 5 s' $T/root/logs/boot.log && grep -q \"the helper on port $STELLAPS5_HELPER_PORT didn't answer the unlock request: not asked where the app is either\" $T/root/logs/boot.log && python3 -c \"
import re, sys
lines = open(sys.argv[1]).read().splitlines()
t = lambda pat: next(float(re.match(r'\\[ *([0-9.]+)\\]', l).group(1)) for l in lines if pat in l)
sys.exit(0 if t('not asked where the app is') - t('no answer in 5 s') < 1.0 else 1)\" $T/root/logs/boot.log" "l: a helper that never answers the unlock: not asked where the app is (no 5 s more at the start)"
helper39 STELLAPS5_HOST_MOUNTS="$MNT" STELLAPS5_HOST_HELPER_OLD=2
rm -f $T/root/logs/boot.log
rc=$(MAXFLIPS=600 run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
stop39
expect "[ $rc = 0 ] && grep -q \"the helper didn't say where the app is: no answer in 5 s\" $T/root/logs/boot.log && ! grep -q 'it closed the connection' $T/root/logs/boot.log" "l: a helper that takes the request and doesn't answer: 'no answer in 5 s' (not 'closed')"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/inst*.txt $T/helper.txt" "the payload and the helper: no sanitizer reports"
nosan "$T"
stop39
[ "$M" = "$T" ] || rm -rf "$M"
fi

if want 40; then
echo "== 40. the installer keeps to its own title: our other ports' folders untouched"
stop_helpers
T=$(newroot t40)
for id in PPSA99013 PPSA99015 PPSA99925; do mkdir -p "$T/homebrew/$id/sce_sys"; echo "$id" >"$T/homebrew/$id/eboot.bin"; echo "{\"titleId\": \"$id\"}" >"$T/homebrew/$id/sce_sys/param.json"; done
before=$(cd "$T/homebrew" && find . -type f -exec md5sum {} + | sort)
STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$T/inst.txt" 2>&1 &
expect "waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'" "installed, then stays as the helper"
expect "cmp -s $T/homebrew/PPSA99927/eboot.bin tests/fake-eboot.bin && grep -q '\"titleId\": \"PPSA99927\"' $T/homebrew/PPSA99927/sce_sys/param.json" "the app installed as PPSA99927"
after=$(cd "$T/homebrew" && find . -type f -not -path './PPSA99927/*' -exec md5sum {} + | sort)
expect "[ \"\$before\" = \"\$after\" ] && [ ! -e $T/appmeta/PPSA99013 ]" "the other ports' folders and art: untouched"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/inst.txt" "no sanitizer reports"
stop_helpers
fi

if want 41; then
echo "== 41. a USB drive: its games on the USB tab (stella/roms/<system>), started from it; states and covers' names on the console"
T=$(newroot t41)
USB=$T/mnt/usb0/stella/roms
mkdir -p "$USB/Atari7800"
$ROM "$T/root/roms/Local Game (USA).a26" >/dev/null
$ROM "$USB/Atari7800/Usb Game (USA).a78" >/dev/null
# R1: the USB tab (Internal, USB, Network); Cross: its game; L2 + Up saves a state; back to the list; quit
rc=$(STELLAPS5_MNT=$T/mnt run "$T" "0:0;60:$R1;62:0;100:$CROSS;102:0;200:$CROSS;240:0;260:$L2UP;262:0;$(SEQ 300 $L3R3 $UP $UP $CROSS);$(SHELFQUIT_AT 400)" "80,230")
expect "[ $rc = 0 ] && grep -q '\[shelf\] tab USB: 1 game' $T/root/logs/boot.log && grep -q '\[shelf\] tab Internal: 1 game' $T/root/logs/boot.log" "the USB tab holds the drive's game, Internal the console's"
expect "grep -q \"loading $USB/Atari7800/Usb Game (USA).a78\" $T/root/logs/boot.log && grep -q 'running \"Usb Game (USA)\" (Atari 7800)' $T/root/logs/boot.log && $CHECK $T/dump/flip00230.ppm 960 540 green >/dev/null" "the 7800 game on the drive runs (Cross: green)"
expect "[ -f '$T/root/states/Atari7800/Usb Game (USA).state1' ] && [ ! -e $T/mnt/usb0/stella/states ]" "its state is on the console, nothing written to the drive"
expect "grep -q 'usb0/stella/roms/Atari7800/Usb Game (USA).a78' $T/root/covers/crc-cache.txt" "its CRC is cached"
nosan "$T"
# the drive unplugged: the shelf goes on without it, and the cache keeps its games (for when it is back)
mv "$T/mnt" "$T/mnt-away"
rc=$(STELLAPS5_MNT=$T/mnt run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q '\[games\] 1 ROM(s)' $T/root/logs/boot.log && grep -q 'usb0/stella/roms/Atari7800/Usb Game (USA).a78' $T/root/covers/crc-cache.txt" "the drive unplugged: one game, the drive's kept in the cache"
nosan "$T"
fi

if want 42; then
echo "== 42. files that aren't what they say: refused with a message, or run without hanging; ProSystem's BIOS; a5200's joystick ranges"
# a. a 1-byte 2600 image (Stella's detection read past it) and 6 NOPs: refused, the screen says why
for f in one:'\x00' nops:'\xea\xea\xea\xea\xea\xea'; do
	n=${f%%:*}
	T=$(newroot t42$n)
	printf "${f#*:}" >"$T/root/roms/$n.a26"
	rc=$(MAXFLIPS=200 run "$T" "0:0;60:$CROSS;62:0" "50" "$T/root/roms/$n.a26")
	expect "grep -q \"could not start .*$n.a26: not a valid Atari 2600 game\" $T/root/logs/boot.log && [ -f $T/dump/flip00050.ppm ]" "$n.a26 ($(stat -c %s $T/root/roms/$n.a26) bytes): not a valid Atari 2600 game, the screen says so"
	nosan "$T"
done
# b. a 129-byte program that doesn't end its frames (Stella's port looped for ever on it, waiting for one): the app
# stays alive and the pause menu quits
T=$(newroot t42hang)
cp tests/odd/hang129.bin "$T/root/roms/hang.bin"
rc=$(run "$T" "0:0;$(QUITAT 420)" "" "$T/root/roms/hang.bin")
expect "[ $rc = 0 ] && grep -q 'running \"hang\" (Atari 2600)' $T/root/logs/boot.log && grep -q 'game closed' $T/root/logs/boot.log" "a program that doesn't end its frames: no hang, the pause menu quits (rc $rc)"
nosan "$T"
# c. .a78 headers ProSystem would read past the end of: Absolute ($0100) and Souper ($1000) with a 4 KiB body
for ty in 01 10; do
	T=$(newroot t42a78$ty)
	python3 - "$T/root/roms/bad$ty.a78" $ty <<'PY'
import sys
h = bytearray(128); h[0] = 1; h[1:10] = b'ATARI7800'
h[49:53] = (4096).to_bytes(4, 'big'); h[53] = int(sys.argv[2], 16); h[55] = h[56] = 1
h[100:128] = b'ACTUAL CART DATA STARTS HERE'
open(sys.argv[1], 'wb').write(bytes(h) + b'\xea' * 4096)
PY
	rc=$(MAXFLIPS=200 run "$T" "0:0;60:$CROSS;62:0" "50" "$T/root/roms/bad$ty.a78")
	expect "grep -q 'could not start .*bad$ty.a78: not a valid Atari 7800 game' $T/root/logs/boot.log" "a .a78 of type \$${ty}00 with a 4 KiB body: refused"
	nosan "$T"
done
# d. ProSystem's BIOS isn't used: with "7800 BIOS (U).rom" in bios/, an NTSC game, then a PAL one in the same session
# (ProSystem kept its BIOS on: the PAL game showed nothing)
T=$(newroot t42bios)
mkdir -p "$T/root/bios" && head -c 4096 /dev/zero >"$T/root/bios/7800 BIOS (U).rom"
$ROM "$T/root/roms/A Ntsc (USA).a78" >/dev/null
$ROM "$T/root/roms/B Pal (Europe).a78" >/dev/null
python3 -c "import sys; p=sys.argv[1]; d=bytearray(open(p,'rb').read()); d[57]=1; open(p,'wb').write(d)" "$T/root/roms/B Pal (Europe).a78"
rc=$(run "$T" "0:0;40:$CROSS;42:0;$(SEQ 120 $L3R3 $UP $UP $CROSS);180:$RIGHT;182:0;200:$CROSS;202:0;$(QUITAT 300)" "280")
expect "[ $rc = 0 ] && grep -q 'running \"B Pal (Europe)\" (Atari 7800), 50' $T/root/logs/boot.log && $CHECK $T/dump/flip00280.ppm 960 540 red >/dev/null" "the PAL game after the NTSC one runs (ProSystem gets a system folder of its own, with no BIOS)"
nosan "$T"
# e. a headerless 7800 game gets a header (ProSystem kept the last game's cartridge type without one)
T=$(newroot t42noh)
mkdir -p "$T/root/roms/Atari7800"
$ROM "$T/root/roms/Atari7800/plain.bin" 7800bin >/dev/null
rc=$(run "$T" "0:0;100:$CROSS;140:0;$(QUITAT 200)" "130" "$T/root/roms/Atari7800/plain.bin")
expect "[ $rc = 0 ] && grep -q 'no .a78 header: one made (32768 bytes, a plain cartridge)' $T/root/logs/boot.log && $CHECK $T/dump/flip00130.ppm 960 540 green >/dev/null" "a headerless 7800 game: a header made, it runs"
nosan "$T"
# f. every core's options start at the core's own defaults: a5200's joystick ranges per game ("auto")
T=$(newroot t42opt)
$ROM "$T/root/roms/test.a52" >/dev/null
rc=$(run "$T" "0:0;$(QUITAT 100)" "" "$T/root/roms/test.a52")
expect "[ $rc = 0 ] && grep -q 'a5200 options: .*a5200_analog_sensitivity=auto' $T/root/logs/boot.log && grep -q 'a5200 options: .*a5200_digital_sensitivity=auto' $T/root/logs/boot.log" "a5200's joystick ranges: its per-game ones (auto), not 100 %"
expect "grep -q 'a5200 options: .*a5200_analog_deadzone=15' $T/root/logs/boot.log" "an option the app doesn't set has a5200's own default (the deadzone: 15 %)"
nosan "$T"
fi

if want 43; then
echo "== 43. the biggest games: 512 KiB Lynx, 7800 (with their headers) and 5200 cartridges, from a folder, a zip and a 7z; one byte more refused"
T=$(newroot t43)
mkdir -p "$T/root/roms/AtariLynx" "$T/root/bios" && head -c 512 /dev/zero >"$T/root/bios/lynxboot.img"
python3 - "$T/big.lnx" "$T/silver.a78" <<'PY'
import sys
h = bytearray(64); h[0:4] = b'LYNX'; h[4:6] = (2048).to_bytes(2, 'little'); h[8:10] = (1).to_bytes(2, 'little')
h[10:20] = b'Big Lynx'.ljust(10, b'\0'); h[42:46] = b'Test'
open(sys.argv[1], 'wb').write(bytes(h) + bytes(512 * 1024))
a = bytearray(128); a[0] = 1; a[1:10] = b'ATARI7800'; a[49:53] = (512 * 1024).to_bytes(4, 'big'); a[54] = 0x02
a[55] = a[56] = 1; a[100:128] = b'ACTUAL CART DATA STARTS HERE'
open(sys.argv[2], 'wb').write(bytes(a) + bytes(512 * 1024))
PY
cp "$T/big.lnx" "$T/root/roms/AtariLynx/tennis.lnx"
python3 tests/forge_crc.py "$T/root/roms/AtariLynx/tennis.lnx" $(crc_of lnx "Jimmy Connors' Tennis (USA, Europe)") 0x1040 64 >/dev/null
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'Big.lnx'); z.close()" "$T/root/roms/biglynx.zip" "$T/big.lnx"
python3 -c "import py7zr,sys; z=py7zr.SevenZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Silver.a78'); z.close()" "$T/root/roms/silver.7z" "$T/silver.a78"
rc=$(run "$T" "0:0;$(SHELFQUIT_AT 60)" "")
expect "[ $rc = 0 ] && grep -q 'tennis.lnx \[lnx\] -> \"Jimmy Connors. Tennis (USA, Europe)\" (by CRC)' $T/root/logs/boot.log" "a 524,352-byte .lnx is read and named by its CRC"
expect "grep -q 'biglynx.zip \[lnx\]' $T/root/logs/boot.log && grep -q 'silver.7z \[a78\]' $T/root/logs/boot.log" "the zipped .lnx and the 7-Zipped .a78 are listed"
rc=$(MAXFLIPS=200 run "$T" "0:0" "" "$T/root/roms/biglynx.zip")
expect "grep -q 'loading .*biglynx.zip -> Big.lnx (524352 bytes) as an Atari Lynx game' $T/root/logs/boot.log" "the zipped 512 KiB Lynx cartridge starts"
nosan "$T"
rc=$(MAXFLIPS=200 run "$T" "0:0" "" "$T/root/roms/silver.7z")
expect "grep -q 'loading .*silver.7z -> Silver.a78 (524416 bytes) as an Atari 7800 game' $T/root/logs/boot.log" "the 7-Zipped 512 KiB 7800 cartridge starts"
nosan "$T"
# the 5200: a5200 takes carts up to 512 KiB (Bosconian's size); one byte more is bigger than any 5200 game, said so
# (not "could not unpack") from a zip, a 7z or a folder
mkdir -p "$T/big5200"
python3 -c "import sys; open(sys.argv[1],'wb').write(bytes(512 * 1024)); open(sys.argv[2],'wb').write(bytes(512 * 1024 + 1))" "$T/big5200/Big.a52" "$T/big5200/Over.a52"
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'Big.a52'); z.close()" "$T/root/roms/big5200.zip" "$T/big5200/Big.a52"
python3 -c "import zipfile,sys; z=zipfile.ZipFile(sys.argv[1],'w',zipfile.ZIP_DEFLATED); z.write(sys.argv[2],'Over.a52'); z.close()" "$T/root/roms/over5200.zip" "$T/big5200/Over.a52"
python3 -c "import py7zr,sys; z=py7zr.SevenZipFile(sys.argv[1],'w'); z.write(sys.argv[2],'Over.a52'); z.close()" "$T/root/roms/over5200.7z" "$T/big5200/Over.a52"
cp "$T/big5200/Over.a52" "$T/root/roms/Over5200.a52"
rc=$(MAXFLIPS=200 run "$T" "0:0" "" "$T/root/roms/big5200.zip")
expect "grep -q 'loading .*big5200.zip -> Big.a52 (524288 bytes) as an Atari 5200 game (a5200)' $T/root/logs/boot.log && grep -q 'running .* (Atari 5200)' $T/root/logs/boot.log" "a zipped 512 KiB 5200 cartridge starts (a5200)"
nosan "$T"
for f in over5200.zip over5200.7z Over5200.a52; do
	rc=$(MAXFLIPS=120 run "$T" "0:0" "" "$T/root/roms/$f")
	expect "grep -q 'could not start .*$f: .* is 524289 bytes, over 524288 for the Atari 5200' $T/root/logs/boot.log && ! grep -q 'could not unpack\\|could not be read' $T/root/logs/boot.log" "$f (512 KiB + 1 byte): bigger than any Atari 5200 game, said so"
	nosan "$T"
done
fi

if want 44; then
echo "== 44. the install's journal (Media Player Classic PS5 1.5's): a failed rename puts the old version back, a cut-off install is put back when the helper starts, a damaged journal is left to the payload"
stop44() { stop_helpers; pkill -f 'http.server 1[89][0-9][0-9][0-9]' 2>/dev/null; sleep 0.3; }
stop44
T=$(newroot t44)
H=$T/homebrew/PPSA99927
STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$T/inst0.txt" 2>&1 &
waitfor $T/root/logs/installer.log 'listening on 127.0.0.1'
stop44
SRV=$T/srv; mkdir -p $SRV
SPORT=$((18000 + RANDOM % 1900))
(cd "$SRV" && exec python3 -m http.server $SPORT --bind 127.0.0.1 >/dev/null 2>&1) &
rm -rf $T/rel && mkdir -p $T/rel && cp -r $H $T/rel/PPSA99927 && rm -f $T/rel/PPSA99927/manifest.sha256
echo "eboot v9.9" >$T/rel/PPSA99927/eboot.bin; echo extra >$T/rel/PPSA99927/sce_sys/extra.bin
sed -i 's/"contentVersion": "[0-9.]*"/"contentVersion": "99.000.000"/' $T/rel/PPSA99927/sce_sys/param.json
python3 tools/make_release_zip.py $T/rel/PPSA99927 PPSA99927 $SRV/StellaPS5-v9.9-PPSA99927.zip >/dev/null
sha=$(sha256sum $SRV/StellaPS5-v9.9-PPSA99927.zip | cut -c1-64); size=$(stat -c %s $SRV/StellaPS5-v9.9-PPSA99927.zip)
printf '{"tag_name":"v9.9","assets":[{"name":"StellaPS5-v9.9-PPSA99927.zip","size":%s,"digest":"sha256:%s","browser_download_url":"http://127.0.0.1:%s/StellaPS5-v9.9-PPSA99927.zip"}]}' $size $sha $SPORT >$SRV/latest.json
sums() { ( cd $H && find . -type f | sort | xargs sha256sum ); }
sums >$T/before.sums
UPD="python3 tests/update_client.py $STELLAPS5_HELPER_PORT"
helper44() { # the helper, with env ($@)
	rm -f $T/root/logs/helper.log
	env STELLAPS5_UPDATE_API=http://127.0.0.1:$SPORT/latest.json STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta "$@" ASAN_OPTIONS=detect_leaks=0 timeout 120 "$HELPER" >>"$T/helper.txt" 2>&1 &
	sleep 0.3
	waitfor $T/root/logs/helper.log 'listening on 127.0.0.1'
}
# a. a file that can't be put in place half way (param.json, after others): every old file put back
helper44 STELLAPS5_HOST_UPDATE_FAIL_RENAME=sce_sys/param.json
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|' && $UPD install 1.0 --until done,failed | grep -q \"^failed|.*Couldn't replace sce_sys/param.json .*the old version was put back\"" "a rename fails half way: the install says the old version was put back"
expect "sums | cmp -s - $T/before.sums" "... and it was: every file as before (no .new, .old or journal left)"
stop_helpers
# b. an install cut off half way (the console turned off): put back when the helper next starts
sha() { sha256sum <"$1" | head -c 1048576 | cut -c1-64; }
python3 - $H <<'PY'
import hashlib, os, sys
h = sys.argv[1]
def fsha(p):
    return hashlib.sha256(open(p, 'rb').read()[:1 << 20]).hexdigest()
os.rename(h + '/eboot.bin', h + '/eboot.bin.old.123-1')
open(h + '/update.journal', 'w').write('stellaps5-update-journal 1 123-1\n1\t%s\teboot.bin\n0\t-\tsce_sys/added.bin\n1\t%s\tsce_sys/param.json\nend\n'
    % (fsha(h + '/eboot.bin.old.123-1'), fsha(h + '/sce_sys/param.json')))
open(h + '/eboot.bin', 'w').write('half-installed eboot\n')
open(h + '/sce_sys/added.bin', 'w').write('a file the cut-off install added\n')
open(h + '/sce_sys/param.json.new', 'w').write('new param\n')
PY
helper44
expect "grep -q 'an install was cut off half way: the old version put back (3 file(s))' $T/root/logs/helper.log" "the next start: the interrupted install found"
expect "sums | cmp -s - $T/before.sums" "... and the old version put back (the old eboot.bin, the added file gone, no journal)"
# c. then the update installs, with no journal or old copies left
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|' && $UPD install 1.0 --until done,failed | grep -q '^done|9.9|'" "then the update installs"
expect "grep -q 'eboot v9.9' $H/eboot.bin && [ -f $H/sce_sys/extra.bin ] && [ -z \"\$(find $H -name '*.new' -o -name '*.old.*' -o -name 'update.journal*')\" ]" "... every file in place, nothing left behind"
stop_helpers
# d. a damaged journal (empty): nothing guessed, nothing installed, the payload repairs the app
: >$H/update.journal
echo "something" >$H/eboot.bin.old.9-9
helper44
expect "grep -q \"can't be read as a whole journal: left alone\" $T/root/logs/helper.log && grep -q 'its record is damaged' $T/helper.txt" "an empty journal: left alone, a notification says to send the payload"
expect "$UPD check 1.0 --until available,failed | grep -q '^available|9.9|' && $UPD install 1.0 --until done,failed | grep -q \"^failed|.*An earlier update isn't finished\"" "... and no update installed over it"
stop_helpers
STELLAPS5_ROOT=$T/root STELLAPS5_HOMEBREW=$T/homebrew STELLAPS5_APPMETA=$T/appmeta ASAN_OPTIONS=detect_leaks=0 timeout 60 "$INSTALLER" >"$T/inst-repair.txt" 2>&1 &
waitfor $T/inst-repair.txt 'listening on 127.0.0.1'
expect "grep -q 'an update wasn.t finished; repairing the app' $T/inst-repair.txt && [ ! -e $H/update.journal ] && [ ! -e $H/eboot.bin.old.9-9 ] && cmp -s $H/eboot.bin tests/fake-eboot.bin" "the payload repairs it: its files back, the journal and the leftovers gone"
expect "! grep -q 'runtime error\|AddressSanitizer' $T/helper.txt $T/inst-repair.txt" "no sanitizer reports"
stop44
fi

if want 45; then
echo "== 45. the eboot imports only what a title may load (no libkernel_sys / libkernel_web: Media Player Classic PS5 1.5's crash at launch, its #3)"
SDKLIB=${PS5_PAYLOAD_SDK:-/opt/ps5-payload-sdk}/target/lib
if [ -f build/app/stella-pie.elf ] && [ -d "$SDKLIB" ]; then
	python3 tools/check_imports.py build/app/stella-pie.elf "$SDKLIB" >"$WORK/imports.txt" 2>&1
	rc=$?
	expect "[ $rc = 0 ] && grep -q '^check_imports: ok' $WORK/imports.txt && ! grep -q 'libkernel_sys\|libkernel_web' $WORK/imports.txt" "the app's PIE (build/app/stella-pie.elf): only allowed modules, every import found in one of them"
else
	echo "  (skipped: build/app/stella-pie.elf or the SDK isn't there -- make ps5 first)"
fi
# the check itself: a PIE calling statfs (as Media Player Classic PS5 1.5 did, and Stella PS5's app before) is refused,
# naming the symbol and libkernel_sys
if [ -d "$SDKLIB" ] && command -v ld.lld >/dev/null && command -v clang >/dev/null; then
	mkdir -p "$WORK/imp"
	printf 'int statfs(const char*, void*);\nint _start(void) { return statfs("/", 0); }\n' >"$WORK/imp/bad.c"
	clang -fPIC -c -o "$WORK/imp/bad.o" "$WORK/imp/bad.c" &&
		ld.lld -pie --no-dynamic-linker -e _start -o "$WORK/imp/bad.elf" "$WORK/imp/bad.o" --as-needed "$SDKLIB/libkernel.so" "$SDKLIB/libkernel_sys.so"
	python3 tools/check_imports.py "$WORK/imp/bad.elf" "$SDKLIB" >"$WORK/imp/out.txt" 2>&1
	rc=$?
	expect "[ $rc = 1 ] && grep -q 'NEEDED libkernel_sys.sprx: not a module the app may load' $WORK/imp/out.txt && grep -q 'import statfs: not in an allowed module (only in libkernel_sys.so)' $WORK/imp/out.txt" "a PIE importing statfs: refused, naming libkernel_sys"
	# and the app's link leaves libkernel_sys / libkernel_web out: such a symbol doesn't link at all
	make -s --eval 'print-app-stubs: ; @echo $(APP_STUBS)' print-app-stubs >"$WORK/imp/stubs.txt" 2>&1
	expect "grep -q '/libkernel.so' $WORK/imp/stubs.txt && ! grep -q 'libkernel_sys\|libkernel_web' $WORK/imp/stubs.txt" "the app's link takes no libkernel_sys / libkernel_web stub (such a symbol doesn't link at all)"
else
	echo "  (skipped: no SDK, ld.lld or clang for the check's own test)"
fi
fi

echo
echo "passed $PASS, failed $FAIL  (work dir $WORK)"
[ $FAIL = 0 ]
