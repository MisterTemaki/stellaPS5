// Stella PS5 frontend: the cores behind a small API.
//
// Each system has its libretro core in the eboot (fe_cores.h): Stella for the 2600 (its own libretro port,
// compiled inside fe_stella.cxx), a5200, ProSystem for the 7800, Gearlynx for the Lynx. The frontend is the libretro
// "frontend" of the one the running game needs -- it answers its environment calls (options, folders, log), gets
// each frame and its sound through callbacks, and reports the pads. One retro_run() is one frame, called from the
// frontend's thread, as Snes9x PS5 runs Snes9x. The 2600's switches that stay put (the difficulty switches, Color /
// B&W) and its palette go to Stella's console itself (fe_stella.h).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace emu
{
// The choices the settings menu offers.
struct Choice
{
	const char* name;
	const char* value; // the core option's value ("" where the frontend handles it)
};
extern const Choice kAspects[]; // each system's own (its core's pixel aspect), 4:3, 16:9 (stretched)
extern const int kAspectCount;
// Atari 2600 (Stella)
extern const Choice kPalettes[]; // the TIA palettes: Stella's standard, z26's, Stella's custom (fe_stella.h)
extern const int kPaletteCount;
extern const Choice kTvEffects[]; // stella_filter: off, composite, S-Video, RGB, badly adjusted (Blargg's NTSC filter)
extern const int kTvEffectCount;
extern const Choice kTvFormats[]; // stella_console: auto (Stella's properties / detection), NTSC, PAL, SECAM...
extern const int kTvFormatCount;
extern const Choice kPhosphor[]; // stella_phosphor: by game (Stella's properties), off, on
extern const int kPhosphorCount;
extern const Choice kStereo[]; // stella_stereo: by game, off, on
extern const int kStereoCount;
// Atari 5200 (a5200)
extern const Choice kA52Blends[]; // a5200_mix_frames
extern const int kA52BlendCount;
extern const Choice kA52Hacks[]; // a5200_input_hack
extern const int kA52HackCount;
// Atari Lynx (Gearlynx)
extern const Choice kLynxRotations[]; // gearlynx_rotation
extern const int kLynxRotationCount;

// The PS5 buttons a console button can go to.
struct PadButton
{
	const char* name; // "Square"
	unsigned bit; // SCE_PAD_BUTTON_*; 0 = not assigned
};
// Cross, Circle, Square, Triangle, L1, R1, OPTIONS, touchpad, R3 (alone: L3 + R3 is the pause menu), (none)
extern const PadButton kPs5Buttons[];
extern const int kPs5ButtonCount;
// What a console button does: holds a RetroPad button while its PS5 button is held, or flips a switch at each press
// (the frontend's: the 2600's difficulty and TV type switches, the 7800's difficulty switches).
enum class Flip
{
	None,
	LeftDifficulty,
	RightDifficulty,
	TvType,
};
struct ConsoleButton
{
	const char* name; // "Fire", "Game Reset"...
	const char* key; // its key in stella-ps5.ini, after the system's id: "a26_btn_fire"
	int retro_id; // the RetroPad button the core reads for it; -1: a switch the frontend flips
	int default_ps5; // index in kPs5Buttons
	Flip flip;
	const char* help; // a line under the settings ("" = none)
};
struct ButtonLayout
{
	const ConsoleButton* buttons;
	int count; // at most fe::kMaxButtons
};
// system: fe::System as a number (0 Atari 2600, 1 5200, 2 7800, 3 Lynx)
const ButtonLayout& Buttons(int system);
// "Atari 2600"... (the CONTROLS rows' system)
const char* SystemLabel(int system);

bool InitCore();
void DeinitCore();

// Starts the game. On failure, *error says why.
bool LoadGame(const std::string& path, std::string* error);
void CloseGame(); // ends the game
bool GameLoaded();
std::string GameName(); // the file name without its extension
std::string SystemName(); // "Atari 2600", "Atari Lynx"... while a game runs
int SystemIndex(); // the running game's system as a number (fe::System), -1 without one
// What Stella made of the game, for the pause menu: "NTSC, F8" (its display format and its bankswitching)
std::string GameDetails();

// The console's own buttons and switches, for the pause menu: a row with a value is a switch (Cross, Left / Right
// flip it, the menu stays), one without is a button (pressed for a moment in the game, which then goes on).
struct ConsoleItem
{
	std::string label; // "Left difficulty" (English: the menu translates it)
	std::string value; // "A", "Color"... ("" = a button)
	int id;
};
std::vector<ConsoleItem> ConsoleItems();
void ConsoleAction(int id);
// A controller the game needs that the pad doesn't stand in for yet ("" = none): the message shown when it starts.
std::string ControllerWarning();

// Settings from fe::Config() -> the core's options and the frontend's picture/sound settings.
void ApplySettings();

enum class FrameResult
{
	Continue,
	OpenMenu, // L3 + R3 pressed together
	Stopped, // no game
};
// One frame: the pads and hot keys, retro_run(), the picture (and the messages) on the screen, the sound to
// the audio ring, paced by the display (60 Hz games) or by the sound (50 Hz games). Hot keys: L2 + Up / Down =
// save / load the state slot, L2 + Left / Right = change the slot, R2 held = fast forward, L2 + R2 held =
// rewind.
FrameResult RunFrame();

void Pause(); // nothing runs between frames; kept for the menus' symmetry
void Resume();

bool SaveState(int slot); // 1..10, /data/stella/states/<game>.state<slot>
bool LoadState(int slot);
bool StateExists(int slot);
void Reset(); // the console's reset (Stella's: the whole system, as switching it off and on keeps the cartridge)
void PowerCycle(); // the game loaded again, as switching the console off and on
void Osd(const std::string& text, double seconds = 2.5); // a message over the game for a few seconds

// Redraws the last frame into the surface (the pause menu draws over it).
void RedrawLastFrame();
} // namespace emu
