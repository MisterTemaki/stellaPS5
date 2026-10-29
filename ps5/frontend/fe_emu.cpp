// Stella PS5 frontend: the core and its libretro callbacks (fe_emu.h).
//
// Frame pacing, as in Snes9x PS5:
//   - 60 Hz games (NTSC): Present waits for the flip, so the console's vsync paces the emulation, and the resampler
//     (the core's rate -> the output's 48 kHz) stretches the sound by up to 0.5% to keep about 60 ms queued.
//   - other rates (PAL and SECAM 2600 games at 50 Hz): the audio clock paces instead (wait while
//     more than 60 ms is queued); the flips don't wait (each frame still waits, before writing, for the buffer it
//     last flipped).
//   - In any case, if more than 120 ms is queued (vsync not blocking for some reason), wait on audio.
//   - Fast forward (hold R2): several frames per flip, no sound.
//
// SPDX-License-Identifier: MIT

#include "fe_emu.h"

#include "ProsperoNotify.h"
#include "fe_cores.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_settings.h"
#include "fe_stella.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoCrash.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "libretro.h"

#include "netfs.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <set>
#include <vector>

namespace emu
{
// value: what Stella's stella_ntsc_aspect / stella_pal_aspect gets ("par": its own pixel aspect); the other cores give
// theirs; 4:3 and 16:9 are the frontend's
const Choice kAspects[] = {
	{"Each system's own", "par"},
	{"4:3", ""},
	{"16:9", ""},
};
const int kAspectCount = int(sizeof(kAspects) / sizeof(kAspects[0]));

// value: Stella's palette name (PaletteHandler: standard, z26, custom; "user" needs a palette file, not offered)
const Choice kPalettes[] = {
	{"Standard (Stella's)", "standard"},
	{"z26", "z26"},
	{"Custom (Stella's adjustable)", "custom"},
};
const int kPaletteCount = int(sizeof(kPalettes) / sizeof(kPalettes[0]));

const Choice kTvEffects[] = {
	{"Off", "disabled"},
	{"Composite", "composite"},
	{"S-Video", "s-video"},
	{"RGB", "rgb"},
	{"Badly adjusted", "badly adjusted"},
};
const int kTvEffectCount = int(sizeof(kTvEffects) / sizeof(kTvEffects[0]));

const Choice kTvFormats[] = {
	{"Auto (Stella's database)", "auto"},
	{"NTSC (60 Hz)", "ntsc"},
	{"PAL (50 Hz)", "pal"},
	{"SECAM (50 Hz)", "secam"},
	{"NTSC at 50 Hz", "ntsc50"},
	{"PAL at 60 Hz", "pal60"},
	{"SECAM at 60 Hz", "secam60"},
};
const int kTvFormatCount = int(sizeof(kTvFormats) / sizeof(kTvFormats[0]));

const Choice kPhosphor[] = {
	{"By game (Stella's database)", "auto"},
	{"Off", "off"},
	{"On", "on"},
};
const int kPhosphorCount = int(sizeof(kPhosphor) / sizeof(kPhosphor[0]));

const Choice kStereo[] = {
	{"By game (Stella's database)", "auto"},
	{"Off", "off"},
	{"On", "on"},
};
const int kStereoCount = int(sizeof(kStereo) / sizeof(kStereo[0]));


const Choice kA52Blends[] = {
	{"Off", "disabled"},
	{"Simple (50:50)", "mix"},
	{"Ghosting (65%)", "ghost_65"},
	{"Ghosting (75%)", "ghost_75"},
	{"Ghosting (85%)", "ghost_85"},
	{"Ghosting (95%)", "ghost_95"},
};
const int kA52BlendCount = int(sizeof(kA52Blends) / sizeof(kA52Blends[0]));

const Choice kA52Hacks[] = {
	{"Off", "disabled"},
	{"Dual stick (right stick = player 2)", "dual_stick"},
	{"Swap ports (Wizard of Wor)", "swap_ports"},
};
const int kA52HackCount = int(sizeof(kA52Hacks) / sizeof(kA52Hacks[0]));

const Choice kLynxRotations[] = {
	{"Auto (Gearlynx's database)", "Auto"},
	{"Off", "Disabled"},
	{"Left", "Left"},
	{"Right", "Right"},
	{"180 degrees", "180"},
};
const int kLynxRotationCount = int(sizeof(kLynxRotations) / sizeof(kLynxRotations[0]));

const PadButton kPs5Buttons[] = {
	{"Cross", SCE_PAD_BUTTON_CROSS},
	{"Circle", SCE_PAD_BUTTON_CIRCLE},
	{"Square", SCE_PAD_BUTTON_SQUARE},
	{"Triangle", SCE_PAD_BUTTON_TRIANGLE},
	{"L1", SCE_PAD_BUTTON_L1},
	{"R1", SCE_PAD_BUTTON_R1},
	{"OPTIONS", SCE_PAD_BUTTON_OPTIONS},
	{"Touchpad", SCE_PAD_BUTTON_TOUCH_PAD},
	{"R3", SCE_PAD_BUTTON_R3},
	{"None", 0},
};
const int kPs5ButtonCount = int(sizeof(kPs5Buttons) / sizeof(kPs5Buttons[0]));
// kPs5Buttons' indexes, for the defaults below
enum
{
	P_CROSS,
	P_CIRCLE,
	P_SQUARE,
	P_TRIANGLE,
	P_L1,
	P_R1,
	P_OPTIONS,
	P_TOUCHPAD,
	P_R3,
	P_NONE
};

namespace
{
// Atari 2600 (Stella's libretro port, update_input): B = the joystick's fire button, A and Y = the Booster Grip's
// trigger and booster (A: the Genesis pad's second button), Select / Start = Game Select / Game Reset (from the first
// pad). The difficulty and TV type switches stay put: their button flips them (fe_stella.h).
const ConsoleButton k2600[] = {
	{"Fire", "btn_fire", RETRO_DEVICE_ID_JOYPAD_B, P_CROSS, Flip::None, ""},
	{"Fire (second button)", "btn_fire2", RETRO_DEVICE_ID_JOYPAD_B, P_SQUARE, Flip::None, ""},
	{"Booster Grip trigger / Genesis button C", "btn_trigger", RETRO_DEVICE_ID_JOYPAD_A, P_CIRCLE, Flip::None,
		"The Booster Grip's two extra buttons, for the games made for it."},
	{"Booster Grip booster", "btn_booster", RETRO_DEVICE_ID_JOYPAD_Y, P_NONE, Flip::None,
		"The Booster Grip's two extra buttons, for the games made for it."},
	{"Game Reset", "btn_reset", RETRO_DEVICE_ID_JOYPAD_START, P_OPTIONS, Flip::None, ""},
	{"Game Select", "btn_select", RETRO_DEVICE_ID_JOYPAD_SELECT, P_TOUCHPAD, Flip::None, ""},
	{"Left difficulty A / B", "btn_left_diff", -1, P_L1, Flip::LeftDifficulty,
		"Each press flips the switch; the pause menu shows where it is."},
	{"Right difficulty A / B", "btn_right_diff", -1, P_R1, Flip::RightDifficulty,
		"Each press flips the switch; the pause menu shows where it is."},
	{"Color / B&W", "btn_color", -1, P_TRIANGLE, Flip::TvType, "Each press flips the switch; the pause menu shows where it is."},
};
// Atari 5200 (a5200's update_input): A / B = the two fire buttons, Start, Select = Pause, Y = *, X = #, R = 0, R3 = 5,
// L = the on-screen keypad (a5200's: the D-pad picks a key, Fire 1 presses it); the left stick is the analog
// joystick, the right stick the keypad's 1-9 around 5 (as on the controller)
const ConsoleButton k5200[] = {
	{"Fire 1 (bottom)", "btn_fire1", RETRO_DEVICE_ID_JOYPAD_A, P_CROSS, Flip::None, ""},
	{"Fire 2 (top)", "btn_fire2", RETRO_DEVICE_ID_JOYPAD_B, P_CIRCLE, Flip::None, ""},
	{"Start", "btn_start", RETRO_DEVICE_ID_JOYPAD_START, P_OPTIONS, Flip::None, ""},
	{"Pause", "btn_pause", RETRO_DEVICE_ID_JOYPAD_SELECT, P_TOUCHPAD, Flip::None, ""},
	{"Keypad *", "btn_star", RETRO_DEVICE_ID_JOYPAD_Y, P_SQUARE, Flip::None,
		"The keypad: the right stick is 1 to 9 around 5 (as on the controller), R3 is 5."},
	{"Keypad #", "btn_hash", RETRO_DEVICE_ID_JOYPAD_X, P_TRIANGLE, Flip::None,
		"The keypad: the right stick is 1 to 9 around 5 (as on the controller), R3 is 5."},
	{"Keypad 0", "btn_zero", RETRO_DEVICE_ID_JOYPAD_R, P_R1, Flip::None,
		"The keypad: the right stick is 1 to 9 around 5 (as on the controller), R3 is 5."},
	{"Keypad 5", "btn_five", RETRO_DEVICE_ID_JOYPAD_R3, P_R3, Flip::None,
		"The keypad: the right stick is 1 to 9 around 5 (as on the controller), R3 is 5."},
	{"Keypad on the screen", "btn_osk", RETRO_DEVICE_ID_JOYPAD_L, P_L1, Flip::None,
		"Shows a5200's keypad: the D-pad picks a key, Fire 1 presses it, the same button hides it."},
};
// Atari 7800 (ProSystem's update_input): B / A = buttons 1 / 2 (B alone: the joystick's one button), X = Reset,
// Select, Start = Pause, L / R held = the difficulty switches on B (the frontend holds them: they stay put)
const ConsoleButton k7800[] = {
	{"Button 1 (left)", "btn_1", RETRO_DEVICE_ID_JOYPAD_B, P_CROSS, Flip::None, ""},
	{"Button 2 (right)", "btn_2", RETRO_DEVICE_ID_JOYPAD_A, P_CIRCLE, Flip::None, ""},
	{"Button 1 (second button)", "btn_1b", RETRO_DEVICE_ID_JOYPAD_B, P_SQUARE, Flip::None, ""},
	{"Pause", "btn_pause", RETRO_DEVICE_ID_JOYPAD_START, P_OPTIONS, Flip::None, ""},
	{"Select", "btn_select", RETRO_DEVICE_ID_JOYPAD_SELECT, P_TOUCHPAD, Flip::None, ""},
	{"Reset", "btn_reset", RETRO_DEVICE_ID_JOYPAD_X, P_TRIANGLE, Flip::None, ""},
	{"Left difficulty A / B", "btn_left_diff", -1, P_L1, Flip::LeftDifficulty,
		"Each press flips the switch; the pause menu shows where it is."},
	{"Right difficulty A / B", "btn_right_diff", -1, P_R1, Flip::RightDifficulty,
		"Each press flips the switch; the pause menu shows where it is."},
};
// Atari Lynx (Gearlynx's update_input): A, B, L / R = Option 1 / 2, Start = Pause
const ConsoleButton kLynx[] = {
	{"A", "btn_a", RETRO_DEVICE_ID_JOYPAD_A, P_CIRCLE, Flip::None, ""},
	{"B", "btn_b", RETRO_DEVICE_ID_JOYPAD_B, P_CROSS, Flip::None, ""},
	{"Option 1", "btn_opt1", RETRO_DEVICE_ID_JOYPAD_L, P_L1, Flip::None, ""},
	{"Option 2", "btn_opt2", RETRO_DEVICE_ID_JOYPAD_R, P_R1, Flip::None, ""},
	{"Pause", "btn_pause", RETRO_DEVICE_ID_JOYPAD_START, P_OPTIONS, Flip::None, ""},
	{"A (second button)", "btn_a2", RETRO_DEVICE_ID_JOYPAD_A, P_TRIANGLE, Flip::None, ""},
	{"B (second button)", "btn_b2", RETRO_DEVICE_ID_JOYPAD_B, P_SQUARE, Flip::None, ""},
};
const ButtonLayout kLayouts[fe::kSystemCount] = {
	{k2600, int(sizeof(k2600) / sizeof(k2600[0]))},
	{k5200, int(sizeof(k5200) / sizeof(k5200[0]))},
	{k7800, int(sizeof(k7800) / sizeof(k7800[0]))},
	{kLynx, int(sizeof(kLynx) / sizeof(kLynx[0]))},
};
static_assert(sizeof(k2600) / sizeof(k2600[0]) <= fe::kMaxButtons && sizeof(k5200) / sizeof(k5200[0]) <= fe::kMaxButtons &&
				  sizeof(k7800) / sizeof(k7800[0]) <= fe::kMaxButtons && sizeof(kLynx) / sizeof(kLynx[0]) <= fe::kMaxButtons,
	"fe::kMaxButtons");
} // namespace

const ButtonLayout& Buttons(int system)
{
	return kLayouts[system >= 0 && system < fe::kSystemCount ? system : 0];
}

const char* SystemLabel(int system)
{
	return fe::Info(fe::System(system >= 0 && system < fe::kSystemCount ? system : 0)).name;
}
} // namespace emu

namespace
{
// printf into a std::string (the translated formats)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}

constexpr int kTargetQueued = ps5audio::kRate * 60 / 1000; // 60 ms of sound
constexpr int kRewindEvery = 3; // frames between rewind snapshots
constexpr size_t kRewindBytes = 192u * 1024 * 1024;
// a console button pressed from the pause menu (Game Reset, Pause...): held this many frames (games read the
// buttons once a frame or less)
constexpr int kPressFrames = 8;
// the Lynx boot ROM Gearlynx needs (/data/stella/bios/lynxboot.img), and its size
constexpr const char* kLynxBios = "lynxboot.img";
constexpr long kLynxBiosSize = 512;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

bool FileExists(const std::string& p)
{
	struct stat st = {};
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

long FileSize(const std::string& p)
{
	struct stat st = {};
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) ? long(st.st_size) : -1;
}

uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

// The whole file, or false: one bigger than max_size is refused before anything is read, and a read that stops short
// or fails (a share that drops mid-read) is a failure, not a cut file. *too_big says which.
bool ReadFile(const std::string& path, std::vector<uint8_t>& out, uint64_t max_size = 256u << 20, bool* too_big = nullptr)
{
	out.clear();
	if (too_big)
		*too_big = false;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	off_t size = -1;
	if (fseeko(f, 0, SEEK_END) == 0)
		size = ftello(f);
	if (size < 0 || uint64_t(size) > max_size || fseeko(f, 0, SEEK_SET) != 0)
	{
		if (too_big && size >= 0 && uint64_t(size) > max_size)
			*too_big = true;
		fclose(f);
		return false;
	}
	out.resize(size_t(size));
	const size_t n = size ? fread(out.data(), 1, out.size(), f) : 0;
	const bool ok = n == out.size() && ferror(f) == 0;
	fclose(f);
	if (!ok)
		out.clear();
	return ok;
}

// data -> path.part, flushed to the disk (fsync) and closed without error, then renamed over path: the old file or
// the whole new one, never a cut one (a full disk or an I/O error shows up at fflush / fsync / fclose).
bool WriteFileAtomic(const std::string& path, const void* data, size_t size)
{
	const std::string tmp = path + ".part";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
	{
		remove(tmp.c_str());
		return false;
	}
	return true;
}

struct State
{
	bool inited = false;
	bool loaded = false;
	bool core_inited[size_t(fe::System::Count)] = {}; // retro_init done (once a run, when its first game starts)
	const fe::CoreApi* core = nullptr; // the running game's
	std::string path, name, dir; // the game
	std::string core_path; // the path the core is given: the game's, or for a game in an archive, its folder + its name inside
	fe::System system = fe::System::A2600;
	std::vector<uint8_t> rom; // the game's file (or the ROM in its .zip or .7z)
	std::string inner;        // the game's name inside its archive ("" for a plain file)
	// what a power cycle hands to the LoadGame it calls: the game already in memory, not read again (from a share)
	std::vector<uint8_t> kept_rom;
	std::string kept_rom_path, kept_inner;
	std::string sys_dir, save_dir;

	// the cores' options, as libretro variables (each core's keys have its own prefix: one map for all)
	std::map<std::string, std::string> opts;
	std::set<std::string> opts_logged; // the cores whose options were logged
	std::string prosystem_dir;         // ProSystem's system folder: one with no 7800 BIOS in it (see Environment)
	int no_picture_runs = 0;           // Stella runs in a row without a picture (a file that isn't a 2600 game)
	bool no_picture_said = false;
	bool opts_changed = false;

	// video
	unsigned pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
	int palette_frames = 0; // 2600: frames left to set the palette (after a start or an option change: Stella resets it)
	std::vector<uint32_t> frame;
	int fw = 0, fh = 0;
	bool new_frame = false;
	double fps = 60.0, core_aspect = 4.0 / 3.0;
	double sample_rate = 44100.0;

	// sound: the core's samples of this frame, and the resampler's state
	std::vector<int16_t> in;
	bool mute = false; // fast forward, rewind: drop the sound
	double frac = 0;
	int16_t prev_l = 0, prev_r = 0;
	std::vector<int16_t> out;
	std::vector<int16_t> silence; // the zeros that refill a dry ring

	// pads
	ps5input::PadState pads[ps5input::kMaxPads];
	uint32_t prev_p1 = 0;
	bool wait_release = true;
	uint32_t prev_flips = 0; // the switch buttons held last frame (a switch flips once per press)
	int press_frames[16] = {}; // RetroPad buttons of the first pad pressed from the pause menu: frames left
	// the 7800's difficulty switches, the frontend's (ProSystem reads them held: L / R = B)
	bool a78_left_b = true, a78_right_b = false;

	// fast forward, rewind
	bool turbo = false, rewinding = false;
	std::deque<std::vector<uint8_t>> rewind;
	size_t rewind_bytes = 0;
	int rewind_tick = 0;

	// battery saves (the Lynx's EEPROM, a 7800 game's high score cartridge...)
	uint64_t sram_hash = 0;
	size_t sram_size = 0;
	double next_sram_check = 0;

	// messages and the FPS counter
	std::string osd;
	double osd_until = 0;
	bool overlay_was_drawn = false;
	int fps_frames = 0;
	double fps_since = 0, fps_shown = 0;
	uint64_t frames = 0;
	double next_stats = 0;
	int stats_reports = 0;
	std::string warning; // the controller the game needs that the pad doesn't stand in for ("" = none)
};
State g;

// Stella PS5 1.0.1 (diagnostics): a game's start step by step -- the stage the crash report names, and with the debug
// logs on one boot.log line each, put on the disk (fsync) before the step, so the last line survives anything that
// takes the process down. Also the core's environment calls and every file the cores touch while it starts, up to
// the third frame.
int g_trace_frames = 0; // > 0: tracing, the frames still to trace
#define EMU_STEP(where)                                                                                            \
	do                                                                                                             \
	{                                                                                                              \
		STELLAPS5_STAGE(Emu, where);                                                                               \
		if (g_trace_frames > 0 && OrbisLogEnabled())                                                               \
		{                                                                                                          \
			OrbisLog("[emu] step: %s", where);                                                                     \
			OrbisLogSync();                                                                                        \
		}                                                                                                          \
	} while (0)

void TraceFile(const char* call, const char* path)
{
	OrbisLog("[file] %s %s", call, path);
}

void TraceStart(bool on)
{
	g_trace_frames = on ? 3 : 0;
	netfs_trace_files(on && OrbisLogEnabled() ? TraceFile : nullptr);
}

int Sys()
{
	return int(g.system);
}

// ---- libretro callbacks -------------------------------------------------------------------------------------
void CoreLog(enum retro_log_level level, const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	size_t n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	static const char* const kLevel[] = {"debug", "info", "warn", "error"};
	if (level == RETRO_LOG_DEBUG)
		return;
	OrbisLog("[core] %s%s%s", level == RETRO_LOG_INFO ? "" : kLevel[level < 4 ? level : 3],
		level == RETRO_LOG_INFO ? "" : ": ", buf);
}

bool Environment(unsigned cmd, void* data)
{
	if (g_trace_frames > 0 && OrbisLogEnabled() && cmd != RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE && cmd != RETRO_ENVIRONMENT_GET_VARIABLE)
		OrbisLog("[emu] environment %u%s", cmd & ~RETRO_ENVIRONMENT_EXPERIMENTAL,
			cmd & RETRO_ENVIRONMENT_EXPERIMENTAL ? " (experimental)" : "");
	switch (cmd)
	{
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			// ProSystem loads "7800 BIOS (U).rom" / "(E).rom" from its system folder and, once it has, keeps the BIOS
			// on for every later game (its bios_enabled is never cleared: a PAL game after an NTSC one with only the
			// (U) BIOS there shows nothing). The 7800 games start without it, and Stella PS5 doesn't use it: ProSystem
			// gets a folder of its own, with no BIOS in it
			*static_cast<const char**>(data) = g.system == fe::System::A7800 ? g.prosystem_dir.c_str() : g.sys_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
			*static_cast<const char**>(data) = g.save_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		{
			const unsigned f = *static_cast<const unsigned*>(data);
			if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888)
				return false;
			g.pixel_format = f;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
			static_cast<retro_log_callback*>(data)->log = CoreLog;
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE:
		{
			retro_variable* v = static_cast<retro_variable*>(data);
			auto it = v->key ? g.opts.find(v->key) : g.opts.end();
			if (it == g.opts.end())
			{
				v->value = nullptr;
				return false;
			}
			v->value = it->second.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			*static_cast<bool*>(data) = g.opts_changed;
			g.opts_changed = false;
			return true;
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
			return true;
		case RETRO_ENVIRONMENT_SET_GEOMETRY:
		{
			const retro_game_geometry* geo = static_cast<const retro_game_geometry*>(data);
			if (geo->aspect_ratio > 0.1f)
				g.core_aspect = geo->aspect_ratio;
			else if (geo->base_width && geo->base_height)
				g.core_aspect = double(geo->base_width) / geo->base_height;
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		{
			const retro_system_av_info* av = static_cast<const retro_system_av_info*>(data);
			if (av->geometry.aspect_ratio > 0.1f)
				g.core_aspect = av->geometry.aspect_ratio;
			if (av->timing.fps > 1)
				g.fps = av->timing.fps;
			if (av->timing.sample_rate > 1000)
				g.sample_rate = av->timing.sample_rate;
			OrbisLog("[emu] av info: %.3f fps, %.0f Hz", g.fps, g.sample_rate);
			return true;
		}
		case RETRO_ENVIRONMENT_SET_MESSAGE:
		{
			const retro_message* m = static_cast<const retro_message*>(data);
			if (m && m->msg)
			{
				OrbisLog("[core] message: %s", m->msg);
				emu::Osd(m->msg);
			}
			return true;
		}
		case RETRO_ENVIRONMENT_SET_VARIABLES:
		{
			// the core's options ("Description; default|other|..."): each one starts at the core's own default, so a
			// core that falls back to something else when an option is missing (a5200's joystick sensitivity: 1.0
			// instead of its per-game ranges) gets what it would in RetroArch; ApplySettings sets the ones we choose
			for (const retro_variable* v = static_cast<const retro_variable*>(data); v && v->key; v++)
			{
				if (!v->value || g.opts.count(v->key))
					continue;
				const char* semi = strstr(v->value, "; ");
				if (!semi)
					continue;
				std::string def = semi + 2;
				def = def.substr(0, def.find('|'));
				g.opts[v->key] = def;
			}
			return true;
		}
		// accepted and ignored
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
		case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
		case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
		case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
			return true;
		default:
			// no run-ahead (GET_AUDIO_VIDEO_ENABLE), no VFS (the cores use stdio: netfs answers through --wrap), no
			// message interface beyond SET_MESSAGE, core options as SET_VARIABLES (GET_CORE_OPTIONS_VERSION: 0)
			return false;
	}
}

inline uint32_t Expand565(uint16_t c)
{
	const uint32_t r = (c >> 11) & 31, gg = (c >> 5) & 63, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 2) | (gg >> 4)) << 8) | ((b << 3) | (b >> 2));
}

inline uint32_t Expand555(uint16_t c)
{
	const uint32_t r = (c >> 10) & 31, gg = (c >> 5) & 31, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 3) | (gg >> 2)) << 8) | ((b << 3) | (b >> 2));
}

void Video(const void* data, unsigned w, unsigned h, size_t pitch)
{
	if (!data || w == 0 || h == 0)
		return; // a duplicated frame: keep the last one
	g.fw = int(w);
	g.fh = int(h);
	g.no_picture_runs = 0;
	g.frame.resize(size_t(w) * h);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	for (unsigned y = 0; y < h; y++)
	{
		uint32_t* out = g.frame.data() + size_t(y) * w;
		if (g.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
		{
			const uint32_t* in = reinterpret_cast<const uint32_t*>(src + y * pitch);
			for (unsigned x = 0; x < w; x++)
				out[x] = in[x] | 0xff000000u;
		}
		else
		{
			const uint16_t* in = reinterpret_cast<const uint16_t*>(src + y * pitch);
			if (g.pixel_format == RETRO_PIXEL_FORMAT_RGB565)
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand565(in[x]);
			else
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand555(in[x]);
		}
	}
	g.new_frame = true;
}

void AudioSample(int16_t l, int16_t r)
{
	if (g.mute)
		return;
	g.in.push_back(l);
	g.in.push_back(r);
}

size_t AudioBatch(const int16_t* data, size_t frames)
{
	if (!g.mute)
		g.in.insert(g.in.end(), data, data + frames * 2);
	return frames;
}

void InputPoll()
{
	ps5input::Poll();
	for (int p = 0; p < ps5input::kMaxPads; p++)
		g.pads[p] = ps5input::Snapshot(p);
}

// The PS5 button a console button of the running game's system is on (Settings, CONTROLS), 0 when none.
unsigned ButtonBit(int i)
{
	const int b = fe::Config().buttons[Sys()][i];
	return b >= 0 && b < emu::kPs5ButtonCount ? emu::kPs5Buttons[b].bit : 0;
}

// Console buttons only the first player has: the 2600's Game Reset / Select (Stella reads them from the first pad
// only), the 5200's Start / Pause / keypad (a5200 too), the 7800's Pause / Select / Reset.
bool FirstPlayerOnly(int retro_id)
{
	switch (g.system)
	{
		case fe::System::A2600: return retro_id == RETRO_DEVICE_ID_JOYPAD_START || retro_id == RETRO_DEVICE_ID_JOYPAD_SELECT;
		case fe::System::A5200: return retro_id != RETRO_DEVICE_ID_JOYPAD_A && retro_id != RETRO_DEVICE_ID_JOYPAD_B;
		case fe::System::A7800:
			return retro_id == RETRO_DEVICE_ID_JOYPAD_START || retro_id == RETRO_DEVICE_ID_JOYPAD_SELECT ||
				   retro_id == RETRO_DEVICE_ID_JOYPAD_X;
		default: return false;
	}
}

// The RetroPad the core reads for one player, from the player's button layout (Settings, CONTROLS). The left stick is
// the D-pad too, but on the 5200, whose joystick is analog: there the stick reports itself (InputState).
uint16_t RetroPadBits(int port, const ps5input::PadState& p, bool dpad_off)
{
	uint16_t bits = 0;
	if (port == 0)
	{
		for (int id = 0; id < 16; id++)
			if (g.press_frames[id] > 0)
				bits |= uint16_t(1u << id);
		// the 7800's difficulty switches stay where they were put: ProSystem reads them held (L / R: on B)
		if (g.system == fe::System::A7800)
		{
			if (g.a78_left_b)
				bits |= uint16_t(1u << RETRO_DEVICE_ID_JOYPAD_L);
			if (g.a78_right_b)
				bits |= uint16_t(1u << RETRO_DEVICE_ID_JOYPAD_R);
		}
	}
	if (!p.connected)
		return bits;
	const emu::ButtonLayout& layout = emu::Buttons(Sys());
	for (int i = 0; i < layout.count; i++)
	{
		const int id = layout.buttons[i].retro_id;
		if (id < 0 || (port != 0 && FirstPlayerOnly(id)))
			continue;
		const unsigned bit = ButtonBit(i);
		if (bit && (p.raw_buttons & bit))
			bits |= uint16_t(1u << id);
	}
	struct Dir
	{
		unsigned id;
		uint32_t bit;
	};
	static const Dir kDirs[] = {
		{RETRO_DEVICE_ID_JOYPAD_UP, SCE_PAD_BUTTON_UP},
		{RETRO_DEVICE_ID_JOYPAD_DOWN, SCE_PAD_BUTTON_DOWN},
		{RETRO_DEVICE_ID_JOYPAD_LEFT, SCE_PAD_BUTTON_LEFT},
		{RETRO_DEVICE_ID_JOYPAD_RIGHT, SCE_PAD_BUTTON_RIGHT},
	};
	const uint32_t dirs = g.system == fe::System::A5200 ? p.raw_buttons : p.buttons;
	if (!dpad_off)
		for (const Dir& d : kDirs)
			if (dirs & d.bit)
				bits |= uint16_t(1u << d.id);
	return bits;
}

// A stick, as libretro's analog axes (-32768..32767; the pad's 0..255, 128 the centre)
int16_t Axis(uint8_t v)
{
	const int a = (int(v) - 128) * 256;
	return int16_t(std::max(-32768, std::min(32767, a)));
}

// Each port is a signed-in player's pad (Stella asks for the left joystick on port 0 and the right one on port 1; a
// paddle game asks for more ports, two paddles a jack).
int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port >= unsigned(ps5input::kMaxPads))
		return 0;
	// L2 is the hot-key modifier for player 1: the D-pad stays with the hot keys while it is held.
	// While the buttons that closed a menu (Cross, Circle) or opened it (L3 + R3) are still held, player 1's pad
	// is not passed on: Resume with Cross must not press the fire button in the game.
	const ps5input::PadState none;
	const bool hold = port == 0 && g.wait_release;
	const ps5input::PadState& pad = hold ? none : g.pads[port];
	const unsigned dev = device & RETRO_DEVICE_MASK;
	if (dev == RETRO_DEVICE_ANALOG)
	{
		// the 5200's analog joystick (left stick) and keypad (right stick); 7800 dual-stick games (right stick)
		if (!pad.connected || id > RETRO_DEVICE_ID_ANALOG_Y)
			return 0;
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			return Axis(id == RETRO_DEVICE_ID_ANALOG_X ? pad.lx : pad.ly);
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
			return Axis(id == RETRO_DEVICE_ID_ANALOG_X ? pad.rx : pad.ry);
		return 0;
	}
	if (dev != RETRO_DEVICE_JOYPAD || index != 0)
		return 0;
	const bool l2 = port == 0 && (g.pads[0].raw_buttons & SCE_PAD_BUTTON_L2) != 0;
	const uint16_t bits = RetroPadBits(int(port), pad, l2);
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return int16_t(bits);
	return id < 16 ? int16_t((bits >> id) & 1) : 0;
}

// ---- sound: the core's rate -> 48 kHz, the rate steered by the ring's fill ------------------------------------
void FlushAudio()
{
	const size_t frames = g.in.size() / 2;
	if (frames == 0)
		return;
	const fe::Settings& cfg = fe::Config();
	if (!cfg.audio || g.mute || !ps5audio::Available())
	{
		g.in.clear();
		return;
	}
	int queued = ps5audio::Queued();
	if (queued < kTargetQueued / 4)
	{
		// the ring ran (nearly) dry: a game just started, or came back from a menu, or the frame was late. Fill
		// it with silence up to the target at once; the rate control alone (0.5%) would take ~10 s to get there,
		// and play crackling meanwhile.
		g.silence.assign(size_t(kTargetQueued - queued) * 2, 0);
		ps5audio::Push(g.silence.data(), kTargetQueued - queued);
		queued = kTargetQueued;
	}
	double adj = double(kTargetQueued - queued) / kTargetQueued;
	adj = 1.0 + std::max(-1.0, std::min(1.0, adj)) * 0.005;
	const double step = g.sample_rate / (ps5audio::kRate * adj); // input frames per output frame
	const int vol = cfg.volume * 256 / 100;
	g.out.clear();
	double pos = g.frac; // position in [prev, in...]: 0 = prev, 1 = in[0]
	while (pos < double(frames))
	{
		const int i = int(pos);
		const int t = int((pos - i) * 256);
		const int16_t l0 = i == 0 ? g.prev_l : g.in[size_t(i - 1) * 2];
		const int16_t r0 = i == 0 ? g.prev_r : g.in[size_t(i - 1) * 2 + 1];
		const int16_t l1 = g.in[size_t(i) * 2];
		const int16_t r1 = g.in[size_t(i) * 2 + 1];
		const int l = (l0 * (256 - t) + l1 * t) >> 8;
		const int r = (r0 * (256 - t) + r1 * t) >> 8;
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, l * vol >> 8))));
		g.out.push_back(int16_t(std::max(-32768, std::min(32767, r * vol >> 8))));
		pos += step;
	}
	g.frac = pos - double(frames);
	g.prev_l = g.in[(frames - 1) * 2];
	g.prev_r = g.in[(frames - 1) * 2 + 1];
	g.in.clear();
	ps5audio::Push(g.out.data(), int(g.out.size() / 2));
}

// ---- states and saves: per system (a 2600 and a 7800 game can share a name) ---------------------------------
std::string SaveDir(const char* kind)
{
	return OrbisDir(kind) + "/" + fe::Info(g.system).folder;
}

std::string StatePath(int slot)
{
	return SaveDir("states") + "/" + g.name + ".state" + std::to_string(slot);
}

std::string SramPath()
{
	return SaveDir("saves") + "/" + g.name + ".srm";
}

void LoadSram()
{
	uint8_t* mem = static_cast<uint8_t*>(g.core->get_memory_data(RETRO_MEMORY_SAVE_RAM));
	const size_t size = g.core->get_memory_size(RETRO_MEMORY_SAVE_RAM);
	g.sram_size = mem ? size : 0;
	g.sram_hash = mem && size ? Fnv(mem, size) : 0;
	if (!mem || !size)
		return;
	std::vector<uint8_t> data;
	if (ReadFile(SramPath(), data) && !data.empty())
	{
		memcpy(mem, data.data(), std::min(size, data.size()));
		g.sram_hash = Fnv(mem, size);
		OrbisLog("[emu] battery save loaded: %s (%zu bytes)", SramPath().c_str(), data.size());
	}
}

// Writes the battery save when it changed.
void SaveSram()
{
	uint8_t* mem = g.core ? static_cast<uint8_t*>(g.core->get_memory_data(RETRO_MEMORY_SAVE_RAM)) : nullptr;
	const size_t size = g.sram_size;
	if (!mem || !size)
		return;
	const uint64_t h = Fnv(mem, size);
	if (h == g.sram_hash)
		return;
	const bool ok = WriteFileAtomic(SramPath(), mem, size);
	if (ok)
		g.sram_hash = h; // a failed write is tried again 3 s later and when the game closes
	OrbisLog("[emu] battery save -> %s (%s)", SramPath().c_str(), ok ? "ok" : "failed");
}

// The 2600's palette chosen: set for a few frames after a start or an option change, since Stella sets its own when
// it (re)creates the console (a start, a TV format change) -- fe_stella.h.
void ApplyPalette()
{
	if (g.system == fe::System::A2600)
		vcs::SetPalette(emu::kPalettes[fe::Config().palette % emu::kPaletteCount].value);
}

void ShowOverlays(ps5video::Rect* damage)
{
	const fe::Settings& cfg = fe::Config();
	const double now = Now();
	bool drawn = false;
	if (cfg.show_fps)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f fps", g.fps_shown);
		const int w = fe::TextWidth(buf, 3);
		ps5video::FillRect(ps5video::kWidth - w - 60, 20, w + 40, 50, ps5video::Rgb(0, 0, 0));
		fe::DrawText(ps5video::kWidth - w - 40, 28, buf, 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (!g.osd.empty() && now < g.osd_until)
	{
		const std::string text = fe::FitText(g.osd, 3, ps5video::kWidth - 160);
		const int w = fe::TextWidth(text.c_str(), 3);
		ps5video::DarkenRect(40, ps5video::kHeight - 100, w + 40, 60);
		fe::DrawText(60, ps5video::kHeight - 90, text.c_str(), 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (drawn || g.overlay_was_drawn)
		*damage = ps5video::Rect{0, 0, ps5video::kWidth, ps5video::kHeight};
	if (!drawn && g.overlay_was_drawn)
		ps5video::InvalidateFrame(); // clear what the overlay left outside the picture
	g.overlay_was_drawn = drawn;
}

double FrameAspect()
{
	const fe::Settings& cfg = fe::Config();
	switch (cfg.aspect)
	{
		case 1: return 4.0 / 3.0;
		case 2: return 16.0 / 9.0;
		default:
			// the 2600: Stella's own picture aspect, the same with or without the NTSC filter (fe_stella.h)
			if (g.loaded && g.system == fe::System::A2600)
				return vcs::PictureAspect(cfg.crop_hmove);
			return g.core_aspect;
	}
}

ps5video::Rect DrawLast()
{
	if (g.frame.empty())
		return ps5video::Rect{0, 0, 0, 0};
	const fe::Settings& cfg = fe::Config();
	return ps5video::DrawFrame(g.frame.data(), g.fw, g.fh, g.fh, FrameAspect(), ps5video::Scale(cfg.scale), cfg.smooth,
		cfg.scanlines, cfg.shader);
}

void RewindPush()
{
	const size_t size = g.core->serialize_size();
	if (size == 0)
		return;
	std::vector<uint8_t> s(size);
	if (!g.core->serialize(s.data(), size))
		return;
	g.rewind_bytes += size;
	g.rewind.push_back(std::move(s));
	while (g.rewind_bytes > kRewindBytes && !g.rewind.empty())
	{
		g.rewind_bytes -= g.rewind.front().size();
		g.rewind.pop_front();
	}
}

void RewindClear()
{
	g.rewind.clear();
	g.rewind_bytes = 0;
	g.rewind_tick = 0;
}

// The controller a 2600 game needs, as a sentence for the player ("" when the pad stands in for everything it uses).
std::string WarningFor(vcs::Need need, const std::string& name)
{
	switch (need)
	{
		case vcs::Need::None: return "";
		case vcs::Need::Paddles: return fe::T("This game needs paddles, which aren't supported yet.");
		case vcs::Need::Driving: return fe::T("This game needs driving controllers, which aren't supported yet.");
		case vcs::Need::Keypad: return fe::T("This game needs keypad controllers, which aren't supported yet.");
		case vcs::Need::TrakBall: return fe::T("This game needs a trackball, which isn't supported yet.");
		case vcs::Need::Mouse: return fe::T("This game needs a mouse, which isn't supported yet.");
		case vcs::Need::LightGun: return fe::T("This game needs a light gun, which isn't supported yet.");
		default: return Fmt(fe::T("This game needs another controller (%s), which isn't supported yet."), name.c_str());
	}
}

// A core's first game this run: its callbacks, retro_init
void StartCore(fe::System s)
{
	const fe::CoreApi& c = fe::CoreFor(s);
	g.core = &c;
	if (g.core_inited[size_t(s)])
		return;
	EMU_STEP("core: retro_set_environment");
	c.set_environment(Environment);
	EMU_STEP("core: the other callbacks");
	c.set_video_refresh(Video);
	c.set_audio_sample(AudioSample);
	c.set_audio_sample_batch(AudioBatch);
	c.set_input_poll(InputPoll);
	c.set_input_state(InputState);
	EMU_STEP("core: retro_init");
	c.init();
	EMU_STEP("core: retro_get_system_info");
	retro_system_info info = {};
	c.get_system_info(&info);
	OrbisLog("[emu] %s %s ready for the %s (libretro API %u; extensions %s)", info.library_name, info.library_version,
		fe::Info(s).name, c.api_version(), info.valid_extensions ? info.valid_extensions : "");
	g.core_inited[size_t(s)] = true;
}
// A file the core would read past the end of, or never make a picture of (found by fuzzing): what is wrong with it, in
// English for the log, or "" when the core can take it.
std::string RomProblem(fe::System s, const std::vector<uint8_t>& rom)
{
	char buf[160];
	if (s == fe::System::A2600)
	{
		// Stella's detection compares the image with signatures of up to 8 bytes and reads its first 0x13 bytes: a
		// smaller image runs it past the end (its search counts with size - signature, unsigned)
		if (rom.size() < 32)
		{
			snprintf(buf, sizeof(buf), "%zu bytes, smaller than any Atari 2600 cartridge", rom.size());
			return buf;
		}
		return "";
	}
	if (s == fe::System::A7800)
	{
		// ProSystem trusts the .a78 header's cartridge type: its banks are 16 KiB, read whole, and some types read a
		// fixed size (Absolute: 64 KiB; Activision: 128 KiB; Souper: bank 31, 512 KiB); a plain cartridge sits below
		// $10000 (64 KiB at most)
		const bool header = rom.size() > 128 && memcmp(rom.data() + 1, "ATARI7800", 9) == 0;
		const size_t body = header ? rom.size() - 128 : rom.size();
		if (rom.size() <= 128)
			return "too small for an Atari 7800 cartridge";
		if (!header)
			return body > 65536 && body % 16384 != 0 ? "no .a78 header, over 64 KiB and not in 16 KiB banks" : "";
		const uint8_t* h = rom.data();
		const uint32_t declared = uint32_t(h[49]) << 24 | uint32_t(h[50]) << 16 | uint32_t(h[51]) << 8 | h[52];
		const unsigned hi = h[53], lo = h[54];
		const bool banked = body % 16384 == 0;
		if (hi == 1 && !(banked && body >= 65536))
			return "an Absolute cartridge (header type $0100) needs 64 KiB in 16 KiB banks";
		if (hi == 2 && !(banked && body >= 131072))
			return "an Activision cartridge (header type $0200) needs 128 KiB in 16 KiB banks";
		if (hi == 16 && !(banked && body >= 524288))
			return "a Souper cartridge (header type $1000) needs 512 KiB in 16 KiB banks";
		if (hi == 0 && (declared > 131072 || (lo & 0x1e)) && !banked)
			return "a bank-switched cartridge (header type $0002-$001E, or over 128 KiB) needs 16 KiB banks";
		if (hi == 0 && declared <= 131072 && !(lo & 0x1e) && body > 65536)
			return "a plain cartridge can't be over 64 KiB";
		return "";
	}
	return "";
}

} // namespace

namespace emu
{
bool InitCore()
{
	if (g.inited)
		return true;
	STELLAPS5_STAGE(Emu, "init");
	g.sys_dir = OrbisDir("bios");
	g.save_dir = OrbisDir("saves");
	g.prosystem_dir = OrbisRoot() + "/cores/prosystem"; // empty: no 7800 BIOS (Environment says why)
	OrbisMkdirs(g.prosystem_dir);
	ApplySettings();
	OrbisLog("[emu] cores: Stella (Atari 2600), a5200 (Atari 5200), ProSystem (Atari 7800), Gearlynx (Atari Lynx); "
			 "BIOS folder %s",
		g.sys_dir.c_str());
	g.inited = true;
	return true;
}

void DeinitCore()
{
	if (!g.inited)
		return;
	CloseGame();
	for (int i = 0; i < int(fe::System::Count); i++)
		if (g.core_inited[i])
		{
			fe::CoreFor(fe::System(i)).deinit();
			g.core_inited[i] = false;
		}
	g.inited = false;
}

void ApplySettings()
{
	const fe::Settings& s = fe::Config();
	auto set = [](const char* key, const std::string& value) {
		auto it = g.opts.find(key);
		if (it == g.opts.end() || it->second != value)
		{
			g.opts[key] = value;
			g.opts_changed = true;
		}
	};
	// Atari 2600 (Stella). stella_palette is left out: Stella 7.0's port doesn't pass it on (its setVideoPalette sets
	// the palette it already had); the palette goes to the console itself (ApplyPalette)
	set("stella_console", kTvFormats[s.tv_format % kTvFormatCount].value);
	set("stella_filter", kTvEffects[s.tv_effects % kTvEffectCount].value);
	set("stella_ntsc_aspect", "par");
	set("stella_pal_aspect", "par");
	set("stella_crop_hoverscan", s.crop_hmove ? "enabled" : "disabled");
	set("stella_stereo", kStereo[s.stereo % kStereoCount].value);
	set("stella_phosphor", kPhosphor[s.phosphor % kPhosphorCount].value);
	set("stella_phosphor_blend", std::to_string(std::max(0, std::min(100, s.phosphor_blend))));
	set("stella_paddle_joypad_sensitivity", "3"); // the port's defaults
	set("stella_paddle_analog_sensitivity", "20");
	// Atari 5200 (a5200): the console's BIOS when there is one, else Altirra's (built in)
	set("a5200_bios", FileExists(g.sys_dir + "/5200.rom") ? "official" : "internal");
	set("a5200_mix_frames", kA52Blends[s.a52_blend % kA52BlendCount].value);
	set("a5200_enable_new_pokey", s.a52_hifi_pokey ? "enabled" : "disabled");
	set("a5200_input_hack", kA52Hacks[s.a52_hack % kA52HackCount].value);
	set("a5200_analog_device", "analog_stick");
	set("a5200_pause_is_reset", "disabled");
	// the joystick's range: a5200's per-game one (Missile Command 15 %, Pole Position 40 %...), as its default
	set("a5200_digital_sensitivity", "auto");
	set("a5200_analog_sensitivity", "auto");
	// Atari 7800 (ProSystem)
	set("prosystem_color_depth", "24bit");
	set("prosystem_gamepad_dual_stick_hack", s.a78_dual_stick ? "enabled" : "disabled");
	// Atari Lynx (Gearlynx)
	set("gearlynx_rotation", kLynxRotations[s.lnx_rotation % kLynxRotationCount].value);
	g.palette_frames = 3;
	if (!s.rewind)
		RewindClear();
}

bool LoadGame(const std::string& path, std::string* error)
{
	if (!g.inited)
		return false;
	TraceStart(true);
	EMU_STEP("load game: close the last one");
	CloseGame();
	EMU_STEP("load game: read the file");
	const std::string file = path.substr(path.find_last_of('/') + 1);
	std::string ext = ExtOf(path);
	g.path = path;
	g.dir = path.substr(0, path.find_last_of('/'));
	g.name = file.substr(0, file.size() - ext.size());
	g.rom.clear();

	// the game is read here (from its .zip or .7z, or the file itself) and handed to its core in memory. A game on a
	// share is read whole here too: nothing streams while it runs.
	std::string inner;
	bool too_big = false;
	auto fail = [&](const std::string& why) {
		TraceStart(false);
		if (error)
			*error = why;
		netfs_end_game();
		g.path.clear();
		g.rom.clear();
		return false;
	};
	// a game file bigger than any of its system's (its name's system; for a .bin or .rom whose system isn't known,
	// bigger than any game of the four)
	const auto too_big_fail = [&](const std::string& name, uint64_t size) {
		fe::System s;
		bool ambiguous = false;
		const bool known = fe::SystemForExt(ExtOf(name), &s, &ambiguous) && !ambiguous;
		OrbisLog("[emu] could not start %s: %s is %llu bytes, over %llu%s%s", path.c_str(), name.c_str(),
			(unsigned long long)size, (unsigned long long)fe::MaxRomSizeFor(name), known ? " for the " : "",
			known ? fe::Info(s).name : "");
		return known ? fail(Fmt(fe::T("%s is larger than any %s game."), file.c_str(), fe::T(fe::Info(s).name)))
					 : fail(Fmt(fe::T("%s is too big for a game of these consoles."), file.c_str()));
	};
	if (!g.kept_rom.empty() && g.kept_rom_path == path)
	{
		// a power cycle: the game read a moment ago, not read again (from a share)
		g.rom = std::move(g.kept_rom);
		inner = g.kept_inner;
		g.kept_rom.clear();
		g.kept_rom_path.clear();
		OrbisLog("[emu] power cycle: %s kept in memory, not read again", file.c_str());
	}
	else if (fe::IsArchiveExt(ext))
	{
		EMU_STEP("load game: unpack the archive");
		std::string why;
		uint64_t too_big_size = 0;
		if (!fe::ReadFromArchive(path, &inner, &g.rom, &why, nullptr, nullptr, &too_big_size))
		{
			if (too_big_size)
				return too_big_fail(inner, too_big_size);
			OrbisLog("[emu] could not start %s: %s", path.c_str(), why.empty() ? "no game inside the archive" : why.c_str());
			return fail(why.empty() ? Fmt(fe::T("There is no game inside %s."), file.c_str())
									: Fmt(fe::T("Stella PS5 could not unpack %s."), file.c_str()) + " " + fe::T(why.c_str()));
		}
	}
	else if (!ReadFile(path, g.rom, fe::MaxRomSizeFor(file), &too_big) || g.rom.empty())
	{
		if (too_big)
			return too_big_fail(file, FileSize(path));
		OrbisLog("[emu] could not start %s: can't read it", path.c_str());
		return fail(Fmt(fe::T("Stella PS5 could not read %s."), file.c_str()));
	}
	g.inner = inner;
	EMU_STEP("load game: which system");
	const std::string game_file = inner.empty() ? file : inner.substr(inner.find_last_of('/') + 1);
	g.system = fe::SystemOfRom(path, game_file, g.rom);
	// each system's own limit, the same as for the archives and the naming (fe::MaxRomSize)
	if (g.rom.size() > fe::MaxRomSize(g.system))
	{
		OrbisLog("[emu] could not start %s: %zu bytes, over %llu for the %s", path.c_str(), g.rom.size(),
			(unsigned long long)fe::MaxRomSize(g.system), fe::Info(g.system).name);
		return fail(Fmt(fe::T("%s is larger than any %s game."), file.c_str(), fe::T(fe::Info(g.system).name)));
	}
	// the Lynx needs its boot ROM (Gearlynx shows a "no BIOS" screen without it): said before it starts
	if (g.system == fe::System::Lynx && FileSize(g.sys_dir + "/" + kLynxBios) != kLynxBiosSize)
	{
		OrbisLog("[emu] missing firmware: %s (checked before loading)", kLynxBios);
		return fail(Fmt(fe::T("Atari Lynx games need the Lynx boot ROM: lynxboot.img (512 bytes). Copy it to %s/ and try "
							  "again."),
			g.sys_dir.c_str()));
	}
	{
		const std::string problem = RomProblem(g.system, g.rom);
		if (!problem.empty())
		{
			OrbisLog("[emu] could not start %s: not a valid %s game (%s)", path.c_str(), fe::Info(g.system).name, problem.c_str());
			return fail(Fmt(fe::T("%s is not a valid %s game."), file.c_str(), fe::T(fe::Info(g.system).name)));
		}
	}
	// A 7800 game without its .a78 header gets one, made here: ProSystem sets the cartridge type (and the region,
	// the controllers) from the header, and without one keeps the previous game's (a Souper cartridge's type for the
	// next headerless game: a crash). Its database still sets the known games' own (it goes by the data's MD5, which
	// the header doesn't change). Up to 64 KiB a plain cartridge; over it SuperGame banks (over 128 KiB the large kind)
	if (g.system == fe::System::A7800 && !(g.rom.size() > 128 && memcmp(g.rom.data() + 1, "ATARI7800", 9) == 0))
	{
		std::vector<uint8_t> h(128, 0);
		const uint32_t n = uint32_t(g.rom.size());
		h[0] = 1;
		memcpy(h.data() + 1, "ATARI7800", 9);
		h[49] = uint8_t(n >> 24);
		h[50] = uint8_t(n >> 16);
		h[51] = uint8_t(n >> 8);
		h[52] = uint8_t(n);
		h[54] = n > 65536 ? 0x02 : 0x00;
		h[55] = h[56] = 1; // joysticks
		memcpy(h.data() + 100, "ACTUAL CART DATA STARTS HERE", 28);
		g.rom.insert(g.rom.begin(), h.begin(), h.end());
		OrbisLog("[emu] no .a78 header: one made (%u bytes, %s)", n, n > 65536 ? "SuperGame banks" : "a plain cartridge");
	}
	// Stella reads the game's name (its default title) and extension (a ".f8", ".e0"... extension names the
	// bankswitching) from the path: a game in an archive goes by its name inside
	g.core_path = inner.empty() ? g.path : g.dir + "/" + game_file;
	EMU_STEP("load game: start the core");
	StartCore(g.system);
	EMU_STEP("load game: settings");

	retro_game_info gi = {};
	gi.path = g.core_path.c_str();
	gi.data = g.rom.data();
	gi.size = g.rom.size();

	OrbisLog("[emu] loading %s%s%s (%zu bytes) as an %s game (%s)", path.c_str(), inner.empty() ? "" : " -> ",
		inner.c_str(), g.rom.size(), fe::Info(g.system).name, g.core->name);
	ApplySettings();
	g.opts_changed = true;
	{
		// each core's options once a session, as it gets them (its defaults, and ours)
		static const char* const kPrefix[] = {"stella_", "a5200_", "prosystem_", "gearlynx_"};
		const char* prefix = kPrefix[size_t(g.system) % 4];
		if (g.opts_logged.insert(prefix).second)
		{
			std::string line;
			for (const auto& o : g.opts)
				if (o.first.rfind(prefix, 0) == 0)
					line += (line.empty() ? "" : ", ") + o.first + "=" + o.second;
			OrbisLog("[emu] %s options: %s", g.core->name, line.c_str());
		}
	}
	g.frame.clear();
	g.fw = g.fh = 0;
	g.in.clear();
	g.mute = false;
	g.pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
	EMU_STEP("core: retro_load_game");
	if (!g.core->load_game(&gi))
	{
		OrbisLog("[emu] could not start %s", path.c_str());
		return fail(Fmt(fe::T("%s could not start %s (not a game it knows, or a damaged file)."), g.core->name, file.c_str()));
	}
	EMU_STEP("core: retro_set_controller_port_device");
	for (unsigned port = 0; port < 4; port++)
		g.core->set_controller_port_device(port, RETRO_DEVICE_JOYPAD);
	EMU_STEP("core: retro_get_system_av_info");
	retro_system_av_info av = {};
	g.core->get_system_av_info(&av);
	g.fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
	g.sample_rate = av.timing.sample_rate > 1000 ? av.timing.sample_rate : 44100.0;
	g.core_aspect = av.geometry.aspect_ratio > 0.1f ? av.geometry.aspect_ratio
					: av.geometry.base_height ? double(av.geometry.base_width) / av.geometry.base_height
											  : 4.0 / 3.0;
	g.loaded = true;
	g.no_picture_runs = 0;
	g.no_picture_said = false;
	g.wait_release = true;
	g.prev_p1 = 0;
	g.prev_flips = 0;
	memset(g.press_frames, 0, sizeof(g.press_frames));
	g.a78_left_b = true; // ProSystem's defaults: the left switch on B, the right one on A
	g.a78_right_b = false;
	g.turbo = g.rewinding = false;
	g.frac = 0;
	g.prev_l = g.prev_r = 0;
	g.frames = 0;
	g.fps_frames = 0;
	g.fps_since = Now();
	g.next_stats = Now() + 1.0;
	g.stats_reports = 0;
	g.next_sram_check = Now() + 3.0;
	g.overlay_was_drawn = false;
	EMU_STEP("load game: video");
	RewindClear();
	ps5video::InvalidateFrame();
	EMU_STEP("load game: save folders, the cartridge's save");
	OrbisMkdirs(SaveDir("states"));
	OrbisMkdirs(SaveDir("saves"));
	LoadSram();
	EMU_STEP("load game: the palette");
	g.palette_frames = 3;
	ApplyPalette();
	EMU_STEP("load game: started");
	// realtime from here: the game was read whole above (from a share too); the naming pass of the shelf stops
	// (netfs_realtime) while a game runs
	netfs_set_realtime(true);
	OrbisLog("[emu] running \"%s\" (%s), %.3f fps, %.0f Hz, aspect %.4f, state size %zu", g.name.c_str(),
		SystemName().c_str(), g.fps, g.sample_rate, g.core_aspect, g.core->serialize_size());
	g.warning.clear();
	if (g.system == fe::System::A2600)
	{
		const vcs::Switches sw = vcs::GetSwitches();
		OrbisLog("[emu] Stella: \"%s\", md5 %s, %s, bankswitching %s; controllers %s / %s; difficulty %c / %c, %s",
			vcs::GameName().c_str(), vcs::GameMd5().c_str(), vcs::DisplayFormat().c_str(), vcs::BankSwitching().c_str(),
			vcs::ControllerName(false).c_str(), vcs::ControllerName(true).c_str(), sw.left_a ? 'A' : 'B',
			sw.right_a ? 'A' : 'B', sw.color ? "color" : "B&W");
		// a controller the pad doesn't stand in for yet: said plainly, and the game runs all the same
		std::string cname;
		const vcs::Need need = vcs::NeededController(&cname);
		g.warning = WarningFor(need, cname);
		if (!g.warning.empty())
		{
			OrbisLog("[emu] needs a controller the pad doesn't stand in for yet: %s", cname.c_str());
			Osd(g.warning, 8.0);
		}
	}
	else if (g.system == fe::System::A5200 && !FileExists(g.sys_dir + "/5200.rom"))
	{
		OrbisLog("[emu] 5200.rom not in %s: Altirra's 5200 BIOS (built into a5200) runs", g.sys_dir.c_str());
		Osd(Fmt(fe::T("No 5200.rom in %s: the built-in BIOS runs (a few games need the console's)."), g.sys_dir.c_str()), 6.0);
	}
	return true;
}

void CloseGame()
{
	if (!g.loaded)
		return;
	STELLAPS5_STAGE(Emu, "close game");
	SaveSram();
	g.core->unload_game();
	g.loaded = false;
	g.frame.clear();
	g.rom.clear();
	g.warning.clear();
	g.osd.clear();
	RewindClear();
	netfs_set_realtime(false);
	netfs_end_game();
	OrbisLog("[emu] game closed");
}

bool GameLoaded()
{
	return g.loaded;
}

std::string GameName()
{
	return g.name;
}

std::string SystemName()
{
	return g.loaded || !g.path.empty() ? fe::Info(g.system).name : "";
}

int SystemIndex()
{
	return g.loaded ? int(g.system) : -1;
}

std::string GameDetails()
{
	if (!g.loaded)
		return "";
	if (g.system != fe::System::A2600)
		return g.core->name;
	std::string f = vcs::DisplayFormat(), b = vcs::BankSwitching();
	if (f.size() > 2 && f.compare(f.size() - 2, 2, " *") == 0)
		f.resize(f.size() - 2); // Stella's mark for a format it detected
	if (b.size() > 2 && b.compare(b.size() - 2, 2, " *") == 0)
		b.resize(b.size() - 2);
	return b.empty() ? f : f + ", " + b;
}

namespace
{
void Press(int retro_id, const char* what)
{
	if (retro_id >= 0 && retro_id < 16)
		g.press_frames[retro_id] = kPressFrames;
	OrbisLog("[emu] %s (pause menu)", what);
}

void FlipSwitch(Flip f)
{
	if (!g.loaded)
		return;
	if (g.system == fe::System::A7800)
	{
		bool& b = f == Flip::LeftDifficulty ? g.a78_left_b : g.a78_right_b;
		if (f != Flip::LeftDifficulty && f != Flip::RightDifficulty)
			return;
		b = !b;
		Osd(f == Flip::LeftDifficulty ? (b ? "Left difficulty: B" : "Left difficulty: A")
									  : (b ? "Right difficulty: B" : "Right difficulty: A"));
		OrbisLog("[emu] 7800 %s difficulty %c", f == Flip::LeftDifficulty ? "left" : "right", b ? 'B' : 'A');
		return;
	}
	if (g.system != fe::System::A2600)
		return;
	const vcs::Switches sw = vcs::GetSwitches();
	switch (f)
	{
		case Flip::LeftDifficulty:
			vcs::SetLeftDifficultyA(!sw.left_a);
			Osd(!sw.left_a ? "Left difficulty: A" : "Left difficulty: B");
			OrbisLog("[emu] left difficulty %c", !sw.left_a ? 'A' : 'B');
			break;
		case Flip::RightDifficulty:
			vcs::SetRightDifficultyA(!sw.right_a);
			Osd(!sw.right_a ? "Right difficulty: A" : "Right difficulty: B");
			OrbisLog("[emu] right difficulty %c", !sw.right_a ? 'A' : 'B');
			break;
		case Flip::TvType:
			vcs::SetColor(!sw.color);
			Osd(!sw.color ? "TV type: Color" : "TV type: B&W");
			OrbisLog("[emu] TV type %s", !sw.color ? "color" : "B&W");
			break;
		default: break;
	}
}

// the pause menu's console items: what they do (a RetroPad button pressed for a moment, or a switch flipped)
struct Item
{
	const char* label;
	int retro_id; // -1: flip
	Flip flip;
};
const Item k2600Items[] = {
	{"Game Reset", RETRO_DEVICE_ID_JOYPAD_START, Flip::None},
	{"Game Select", RETRO_DEVICE_ID_JOYPAD_SELECT, Flip::None},
	{"Left difficulty", -1, Flip::LeftDifficulty},
	{"Right difficulty", -1, Flip::RightDifficulty},
	{"TV type", -1, Flip::TvType},
};
const Item k5200Items[] = {
	{"Start", RETRO_DEVICE_ID_JOYPAD_START, Flip::None},
	{"Pause", RETRO_DEVICE_ID_JOYPAD_SELECT, Flip::None},
	{"Keypad on the screen", RETRO_DEVICE_ID_JOYPAD_L, Flip::None},
};
const Item k7800Items[] = {
	{"Pause", RETRO_DEVICE_ID_JOYPAD_START, Flip::None},
	{"Select", RETRO_DEVICE_ID_JOYPAD_SELECT, Flip::None},
	{"Reset", RETRO_DEVICE_ID_JOYPAD_X, Flip::None},
	{"Left difficulty", -1, Flip::LeftDifficulty},
	{"Right difficulty", -1, Flip::RightDifficulty},
};
const Item kLynxItems[] = {
	{"Pause", RETRO_DEVICE_ID_JOYPAD_START, Flip::None},
	{"Option 1", RETRO_DEVICE_ID_JOYPAD_L, Flip::None},
	{"Option 2", RETRO_DEVICE_ID_JOYPAD_R, Flip::None},
};

void ItemsOf(fe::System s, const Item** items, int* n)
{
	switch (s)
	{
		case fe::System::A5200: *items = k5200Items; *n = int(sizeof(k5200Items) / sizeof(k5200Items[0])); break;
		case fe::System::A7800: *items = k7800Items; *n = int(sizeof(k7800Items) / sizeof(k7800Items[0])); break;
		case fe::System::Lynx: *items = kLynxItems; *n = int(sizeof(kLynxItems) / sizeof(kLynxItems[0])); break;
		default: *items = k2600Items; *n = int(sizeof(k2600Items) / sizeof(k2600Items[0])); break;
	}
}

std::string SwitchValue(Flip f)
{
	if (g.system == fe::System::A7800)
		return (f == Flip::LeftDifficulty ? g.a78_left_b : g.a78_right_b) ? "B" : "A";
	const vcs::Switches sw = vcs::GetSwitches();
	switch (f)
	{
		case Flip::LeftDifficulty: return sw.left_a ? "A" : "B";
		case Flip::RightDifficulty: return sw.right_a ? "A" : "B";
		case Flip::TvType: return sw.color ? "Color" : "B&W";
		default: return "";
	}
}
} // namespace

std::vector<ConsoleItem> ConsoleItems()
{
	std::vector<ConsoleItem> out;
	if (!g.loaded)
		return out;
	const Item* items = nullptr;
	int n = 0;
	ItemsOf(g.system, &items, &n);
	for (int i = 0; i < n; i++)
		out.push_back({items[i].label, items[i].flip == Flip::None ? "" : SwitchValue(items[i].flip), i});
	return out;
}

void ConsoleAction(int id)
{
	if (!g.loaded)
		return;
	const Item* items = nullptr;
	int n = 0;
	ItemsOf(g.system, &items, &n);
	if (id < 0 || id >= n)
		return;
	if (items[id].flip != Flip::None)
		FlipSwitch(items[id].flip);
	else
		Press(items[id].retro_id, items[id].label);
}

std::string ControllerWarning()
{
	return g.loaded ? g.warning : "";
}

void Pause() {}

void Resume()
{
	ApplySettings();
	g.wait_release = true;
	ps5video::InvalidateFrame();
	g.new_frame = true;
}

void Osd(const std::string& text, double seconds)
{
	g.osd = fe::T(text.c_str());
	g.osd_until = Now() + seconds;
}

bool SaveState(int slot)
{
	if (!g.loaded)
		return false;
	const size_t size = g.core->serialize_size();
	std::vector<uint8_t> data(size);
	bool ok = size > 0 && g.core->serialize(data.data(), size) && WriteFileAtomic(StatePath(slot), data.data(), size);
	OrbisLog("[emu] save state %d: %s (%zu bytes)", slot, ok ? "ok" : "failed", size);
	if (ok)
		Osd(Fmt(fe::T("State saved to slot %d"), slot));
	else
		Osd("Could not save the state");
	return ok;
}

bool LoadState(int slot)
{
	if (!g.loaded)
		return false;
	std::vector<uint8_t> data;
	if (!ReadFile(StatePath(slot), data) || data.empty())
	{
		Osd(Fmt(fe::T("Slot %d is empty"), slot));
		return false;
	}
	const bool ok = g.core->unserialize(data.data(), data.size());
	OrbisLog("[emu] load state %d: %s", slot, ok ? "ok" : "failed");
	if (ok)
		Osd(Fmt(fe::T("State loaded from slot %d"), slot));
	else
		Osd("Could not load the state");
	if (ok)
		RewindClear();
	return ok;
}

bool StateExists(int slot)
{
	return g.loaded && FileExists(StatePath(slot));
}

void Reset()
{
	if (!g.loaded)
		return;
	g.core->reset();
	RewindClear();
	OrbisLog("[emu] reset");
}

void PowerCycle()
{
	if (!g.loaded)
		return;
	const std::string path = g.path;
	// the game is kept in memory across the power cycle (it may be on a share)
	g.kept_rom = g.rom;
	g.kept_rom_path = path;
	g.kept_inner = g.inner;
	CloseGame();
	std::string error;
	const bool ok = LoadGame(path, &error);
	g.kept_rom.clear();
	g.kept_rom_path.clear();
	if (!ok)
	{
		OrbisLog("[emu] power cycle: %s", error.c_str());
		ProsperoNotify(fe::T("Stella PS5: the game could not be started again. %s"), error.c_str());
	}
}

void RedrawLastFrame()
{
	DrawLast();
}

FrameResult RunFrame()
{
	if (!g.loaded)
		return FrameResult::Stopped;
	fe::Settings& cfg = fe::Config();

	// -- hot keys (player 1)
	ps5input::Poll();
	const uint32_t raw = ps5input::Pad(0).raw_buttons;
	if (g.wait_release)
	{
		g.wait_release = (raw & (SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE)) != 0;
		g.prev_p1 = raw;
		g.prev_flips = raw;
	}
	const uint32_t pressed = raw & ~g.prev_p1;
	g.prev_p1 = raw;
	const uint32_t menu_combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3;
	if ((raw & menu_combo) == menu_combo && (pressed & menu_combo))
		return FrameResult::OpenMenu;

	const bool l2 = (raw & SCE_PAD_BUTTON_L2) != 0;
	const bool r2 = (raw & SCE_PAD_BUTTON_R2) != 0;
	if (l2 && !r2)
	{
		if (pressed & SCE_PAD_BUTTON_UP)
			SaveState(cfg.state_slot);
		else if (pressed & SCE_PAD_BUTTON_DOWN)
			LoadState(cfg.state_slot);
		else if (pressed & (SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT))
		{
			cfg.state_slot = (pressed & SCE_PAD_BUTTON_RIGHT) ? cfg.state_slot % 10 + 1 : (cfg.state_slot + 8) % 10 + 1;
			Osd(Fmt(fe::T(StateExists(cfg.state_slot) ? "State slot %d (used)" : "State slot %d (empty)"), cfg.state_slot));
			OrbisLog("[emu] state slot %d", cfg.state_slot);
			cfg.Save();
		}
	}
	// the switches that stay put (player 1): each press of their button flips one
	if (!g.wait_release)
	{
		const uint32_t newly = raw & ~g.prev_flips;
		g.prev_flips = raw;
		const ButtonLayout& layout = Buttons(Sys());
		for (int i = 0; i < layout.count; i++)
		{
			const unsigned bit = ButtonBit(i);
			if (layout.buttons[i].flip != Flip::None && bit && (newly & bit))
				FlipSwitch(layout.buttons[i].flip);
		}
	}

	const bool want_rewind = cfg.rewind && l2 && r2;
	if (want_rewind != g.rewinding)
	{
		g.rewinding = want_rewind;
		OrbisLog("[emu] rewind %s (%zu snapshots, %zu MB)", want_rewind ? "on" : "off", g.rewind.size(),
			g.rewind_bytes >> 20);
	}
	const bool want_turbo = r2 && !l2;
	if (want_turbo != g.turbo)
	{
		g.turbo = want_turbo;
		OrbisLog("[emu] fast forward %s", want_turbo ? "on" : "off");
	}

	// -- emulate
	if (g_trace_frames > 0)
	{
		EMU_STEP("core: retro_run (one of the first three frames)");
		if (--g_trace_frames == 0)
		{
			OrbisLog("[emu] step: the first frames ran");
			TraceStart(false);
		}
	}
	STELLAPS5_STAGE(Emu, "retro_run");
	if (g.rewinding)
	{
		if (!g.rewind.empty())
		{
			const std::vector<uint8_t>& s = g.rewind.back();
			g.core->unserialize(s.data(), s.size());
			g.rewind_bytes -= s.size();
			g.rewind.pop_back();
		}
		g.mute = true;
		g.core->run();
		g.mute = false;
		g.in.clear();
	}
	else
	{
		const int runs = g.turbo ? (cfg.ff_speed == 0 ? 8 : std::max(2, (cfg.ff_speed + 50) / 100)) : 1;
		for (int i = 0; i < runs; i++)
		{
			g.mute = g.turbo;
			g.core->run();
			// Stella giving no picture for 5 seconds: a program that never ends a frame (its port's loop is bounded,
			// see src/os/libretro/StellaLIBRETRO.cxx): said once, the pause menu still works
			if (g.system == fe::System::A2600 && ++g.no_picture_runs > 300 && !g.no_picture_said)
			{
				g.no_picture_said = true;
				OrbisLog("[emu] no picture from %s for 300 frames: not a 2600 game?", g.name.c_str());
				Osd(fe::T("This file shows no picture: it may not be an Atari 2600 game."), 8.0);
			}
			g.frames++;
			g.fps_frames++;
			for (int& f : g.press_frames)
				if (f > 0)
					f--;
			if (cfg.rewind && ++g.rewind_tick >= kRewindEvery)
			{
				g.rewind_tick = 0;
				RewindPush();
			}
		}
		g.mute = false;
	}
	if (g.palette_frames > 0)
	{
		ApplyPalette(); // after the frame that read the options (Stella sets its own palette when it makes the console)
		g.palette_frames--;
	}
	FlushAudio();

	// -- picture
	const double now = Now();
	if (now - g.fps_since >= 1.0)
	{
		g.fps_shown = g.fps_frames / (now - g.fps_since);
		g.fps_frames = 0;
		g.fps_since = now;
	}
	// 60 Hz games wait for the flip (the display paces them); 50 Hz ones and the Lynx (75 Hz) go by the sound
	const bool by_audio = std::fabs(g.fps - 60.0) > 1.5;
	if (g.new_frame || (!g.osd.empty() && now < g.osd_until + 0.1) || cfg.show_fps)
	{
		g.new_frame = false;
		ps5video::Rect r = DrawLast();
		ShowOverlays(&r);
		ps5video::Present(r.x, r.y, r.w, r.h, !by_audio && !g.turbo);
	}

	// -- pacing on the sound: other rates than 60 Hz, or a ring filling up
	if (!g.turbo && !g.rewinding)
	{
		if (cfg.audio && ps5audio::Available())
		{
			// by audio: the sound is the clock; 60 Hz: vsync is, and this only catches a display that doesn't block
			const int limit = by_audio ? kTargetQueued : kTargetQueued * 2;
			const double give_up = Now() + 0.1;
			while (ps5audio::Queued() > limit && Now() < give_up)
				usleep(1000);
		}
		else if (by_audio)
		{
			static double next = 0;
			if (next < now - 0.1)
				next = now;
			next += 1.0 / g.fps;
			if (next > Now())
				usleep(useconds_t((next - Now()) * 1e6));
		}
	}

	// -- battery save, every 3 s when it changed; the log, now and then
	if (now >= g.next_sram_check)
	{
		g.next_sram_check = now + 3.0;
		SaveSram();
	}
	if (now >= g.next_stats)
	{
		g.stats_reports++;
		g.next_stats = now + (g.stats_reports < 3 ? 5.0 : 60.0);
		const double shader_ms = ps5video::TakeShaderMs();
		OrbisLog("[emu] frame %llu, %.1f fps, audio queued %d (%.0f ms), underruns %llu, shader %.1f ms",
			(unsigned long long)g.frames, g.fps_shown, ps5audio::Queued(), ps5audio::Queued() * 1000.0 / ps5audio::kRate,
			(unsigned long long)ps5audio::Underruns(), shader_ms);
	}
	return FrameResult::Continue;
}
} // namespace emu
