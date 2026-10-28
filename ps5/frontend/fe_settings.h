// Stella PS5 frontend: settings kept in /data/stella/stella-ps5.ini (key=value lines).
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
// The systems, as fe::System numbers them (fe_games.h): 0 Atari 2600, 1 Atari 5200, 2 Atari 7800, 3 Atari Lynx.
constexpr int kSystemCount = 4;
// The most console buttons a system has (emu::Buttons).
constexpr int kMaxButtons = 10;

struct Settings
{
	// video
	int scale = 0; // ps5video::Scale: fit, integer, stretch
	int shader = 1; // ps5crt::Shader: 0 off, 1 "CRT Easymode style" (the default for every game), ...
	int aspect = 0; // emu::kAspects: each system's own, 4:3, 16:9
	bool smooth = false; // bilinear when the scale isn't whole
	bool scanlines = false;
	bool show_fps = false;
	// audio
	bool audio = true;
	int volume = 100; // 0..100
	// emulation
	int ff_speed = 300; // % while R2 is held; 0 = as fast as possible
	bool rewind = true; // L2 + R2 rewinds
	int state_slot = 1; // 1..10
	// Atari 2600 (Stella)
	int palette = 0; // emu::kPalettes: standard (Stella's), z26, custom
	int tv_effects = 0; // emu::kTvEffects: off, composite, S-Video, RGB, badly adjusted (Blargg's NTSC filter)
	int phosphor = 0; // emu::kPhosphor: by game (Stella's database turns it on for the games that flicker), off, on
	int phosphor_blend = 60; // 0..100 %, Stella's default 60
	bool crop_hmove = false; // hide the left 8 pixels (the HMOVE bars some games leave there)
	int stereo = 0; // emu::kStereo: by game, off, on
	int tv_format = 0; // emu::kTvFormats: auto (Stella's), NTSC, PAL, SECAM, NTSC50, PAL60, SECAM60
	// Atari 5200 (a5200)
	int a52_blend = 0; // emu::kA52Blends: off, simple, ghosting 65..95 %
	int a52_hack = 0; // emu::kA52Hacks: off, dual stick (the right stick is player 2's), swap ports
	bool a52_hifi_pokey = true; // a5200's high-fidelity POKEY (off for the games with digitised speech)
	// Atari 7800 (ProSystem)
	bool a78_dual_stick = false; // the right stick is player 2's joystick (Robotron: 2084...)
	// Atari Lynx (Gearlynx)
	int lnx_rotation = 0; // emu::kLynxRotations: auto (Gearlynx's database), off, left, right, 180
	// controls: per system, the PS5 button (index in emu::kPs5Buttons) of each console button, in emu::Buttons' order
	int buttons[kSystemCount][kMaxButtons] = {};
	int controls_system = 0; // the system the CONTROLS rows show (the running game's when there is one)
	void DefaultButtons(int system);
	Settings();
	// library
	bool covers_download = true; // fetch box art from libretro-thumbnails
	bool debug_logs = false;     // boot.log and the others in /data/stella/logs (OrbisLogSetEnabled); off by default
	int language = 0;            // fe::Language: 0 English (the default), 1 Spanish, 2 Portuguese (Brazil), 3 French, 4 German
	int shelf_family = 0; // fe::Family the shelf shows
	int last_tab = 0; // the shelf's storage tab (fe_tabs.h): 0 Internal, 1 USB, 2 Network
	std::string last_rom; // the shelf puts the selection on this game

	void Load();
	void Save() const;
};

Settings& Config();
} // namespace fe
