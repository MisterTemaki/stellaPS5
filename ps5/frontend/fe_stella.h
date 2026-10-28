// Stella PS5 frontend: what the frontend asks of Stella beyond the libretro API (fe_stella.cxx).
//
// fe_stella.cxx compiles Stella's own libretro port (src/os/libretro/libretro.cxx) by #including it, so these calls
// reach the console that port runs (its StellaLIBRETRO object) without a change to Stella's files: the controllers
// it plugged in, the console switches, the palette. They also look ROMs up in Stella's property database
// (src/emucore/DefProps.hxx, through its PropertiesSet). Everything here is plain C++17: the frontend never sees
// Stella's headers (C++20).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace vcs
{
// ---- Stella's property database, by the ROM's MD5 (no game needs to run) ----------------------------------------
struct Props
{
	std::string name;         // Cart.Name: "Pitfall! (1982) (Activision)"
	std::string manufacturer; // Cart.Manufacturer
	std::string left, right;  // Controller.Left / .Right: "PADDLES", "DRIVING"... ("" = Stella detects it)
	std::string format;       // Display.Format: "PAL", "NTSC50"... ("" = Stella detects it)
};
bool LookupProps(const std::string& md5, Props* out);
// Stella's MD5 of a ROM (MD5::hash): 32 lower-case hex digits, the database's key.
std::string Md5(const uint8_t* data, size_t size);

// ---- the game running (after retro_load_game, until retro_unload_game) ---------------------------------------
bool Running();
// The controllers Stella plugged into the left / right port, from its property database or its own detection of the
// game's code (Controller::getName: "Joystick", "Paddles", "Driving", "Keyboard", "Trak-Ball"...).
std::string ControllerName(bool right);

// A controller the game needs that the PS5 pad doesn't stand in for yet (Stella PS5 1.0 has the joystick only; the
// joystick's relatives -- the Booster Grip, the Genesis pad, Joy 2B+ -- and the SaveKey / AtariVox are fine).
enum class Need
{
	None,
	Paddles,
	Driving,
	Keypad, // Keyboard (the keypad controllers), Kid Vid
	TrakBall,
	Mouse, // Atari / Amiga mouse
	LightGun,
	Other, // MindLink, CompuMate...: *name says which (Stella's name)
};
Need NeededController(std::string* name);

// The console's switches. The difficulty switches and Color / B&W are switches that stay where they are put: these
// set them (Stella's own events, as its libretro port sends them for L / L2, R / R2, L3 / R3). Game Select and Game
// Reset are buttons: the frontend holds them down through the RetroPad's Select / Start.
struct Switches
{
	bool left_a = false;  // left difficulty: A (pro) or B (amateur)
	bool right_a = false; // right difficulty
	bool color = true;    // TV type switch: Color or B&W
};
Switches GetSwitches();
void SetLeftDifficultyA(bool a);
void SetRightDifficultyA(bool a);
void SetColor(bool color);

// The TIA palette: "standard", "z26" or "custom" (Stella's palettes; its libretro option "stella_palette" doesn't
// reach the palette in Stella 7.0's port, so the frontend sets it here, after every start).
void SetPalette(const char* name);

// What Stella made of the game: its display format ("NTSC", "PAL", "SECAM", "NTSC50", "PAL60", "SECAM60"; with " *"
// when detected), the bankswitching scheme ("4K", "F8", "E0"...), the name it gives it, its MD5.
std::string DisplayFormat();
std::string BankSwitching();
// The picture's display aspect as Stella's port gives it without the NTSC filter (its double-width pixels: the TIA's
// width doubled, less the HMOVE bars' 8 pixels when they are hidden, over its height). With the filter on, Stella 7.0's
// libretro port reports an aspect for a line-doubled picture (half as wide) while it hands over single lines: the
// frontend keeps this one, so the TV effects don't squeeze the picture.
double PictureAspect(bool crop_hmove);
std::string GameName();
std::string GameMd5();
} // namespace vcs
