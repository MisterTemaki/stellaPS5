// Stella PS5 frontend: Stella's libretro port, and what the frontend asks of it beyond the libretro API (fe_stella.h).
//
// Stella's libretro port (src/os/libretro/libretro.cxx) is compiled here, #included whole and unchanged: the
// retro_* functions the frontend calls (fe_emu.cpp) are its own. Being in the same translation unit, the calls below
// reach its StellaLIBRETRO object (`stella`, static there) and the controller types it detected (`input_type`). Built
// with Stella's flags (C++20, no RTTI, its defines and include folders: ps5/Makefile, BRIDGE_SRC).
//
// SPDX-License-Identifier: MIT

#include "libretro.cxx"

#include "fe_stella.h"

#include "MD5.hxx"
#include "Props.hxx"
#include "PropsSet.hxx"
#include "Switches.hxx"

namespace vcs
{
namespace
{
bool Ready()
{
	return stella.osystem().hasConsole();
}
} // namespace

bool LookupProps(const std::string& md5, Props* out)
{
	// the built-in database only (useDefaults): PropertiesSet's repository is a no-op one here, and nothing is
	// inserted, so this reads constant data only (the shelf's scan calls it from its own thread)
	PropertiesSet set;
	Properties p;
	if (md5.size() != 32 || !set.getMD5(md5, p, true))
		return false;
	if (out)
	{
		out->name = p.get(PropType::Cart_Name);
		out->manufacturer = p.get(PropType::Cart_Manufacturer);
		out->left = p.get(PropType::Controller_Left);
		out->right = p.get(PropType::Controller_Right);
		out->format = p.get(PropType::Display_Format);
	}
	return true;
}

std::string Md5(const uint8_t* data, size_t size)
{
	return MD5::hash(data, size);
}

bool Running()
{
	return Ready();
}

std::string ControllerName(bool right)
{
	if (!Ready())
		return "";
	const Console& c = stella.osystem().console();
	return Controller::getName((right ? c.rightController() : c.leftController()).type());
}

Need NeededController(std::string* name)
{
	if (name)
		name->clear();
	if (!Ready())
		return Need::None;
	Need need = Need::None;
	for (const Controller::Type t : {input_type[0], input_type[1]})
	{
		Need n = Need::None;
		switch (t)
		{
			using enum Controller::Type;
			case Joystick:
			case BoosterGrip:
			case Genesis:
			case Joy2BPlus:
			case SaveKey:
			case AtariVox:
			case Unknown:
			case LastType: n = Need::None; break;
			case Paddles:
			case PaddlesIAxis:
			case PaddlesIAxDr: n = Need::Paddles; break;
			case Driving: n = Need::Driving; break;
			case Keyboard:
			case KidVid: n = Need::Keypad; break;
			case TrakBall: n = Need::TrakBall; break;
			case AtariMouse:
			case AmigaMouse: n = Need::Mouse; break;
			case Lightgun: n = Need::LightGun; break;
			default: n = Need::Other; break; // MindLink, CompuMate, QuadTari
		}
		if (n != Need::None && need == Need::None)
		{
			need = n;
			if (name)
				*name = Controller::getName(t);
		}
	}
	return need;
}

Switches GetSwitches()
{
	Switches s;
	if (!Ready())
		return s;
	const ::Switches& sw = stella.osystem().console().switches();
	s.left_a = sw.leftDifficultyA();
	s.right_a = sw.rightDifficultyA();
	s.color = sw.tvColor();
	return s;
}

void SetLeftDifficultyA(bool a)
{
	if (Ready())
		stella.setInputEvent(a ? Event::ConsoleLeftDiffA : Event::ConsoleLeftDiffB, 1);
}

void SetRightDifficultyA(bool a)
{
	if (Ready())
		stella.setInputEvent(a ? Event::ConsoleRightDiffA : Event::ConsoleRightDiffB, 1);
}

void SetColor(bool color)
{
	if (Ready())
		stella.setInputEvent(color ? Event::ConsoleColor : Event::ConsoleBlackWhite, 1);
}

void SetPalette(const char* name)
{
	if (!Ready() || !name)
		return;
	stella.osystem().settings().setValue("palette", name);
	stella.osystem().frameBuffer().tiaSurface().paletteHandler().setPalette(name);
}

std::string DisplayFormat()
{
	return Ready() ? stella.osystem().console().getFormatString() : "";
}

double PictureAspect(bool crop_hmove)
{
	if (!Ready())
		return 4.0 / 3.0;
	const TIA& tia = stella.osystem().console().tia();
	const double w = tia.width() * 2.0 - (crop_hmove ? 16.0 : 0.0);
	return tia.height() ? w / tia.height() : 4.0 / 3.0;
}

std::string BankSwitching()
{
	return Ready() ? stella.osystem().console().cartridge().detectedType() : "";
}

std::string GameName()
{
	return Ready() ? stella.osystem().console().properties().get(PropType::Cart_Name) : "";
}

std::string GameMd5()
{
	return Ready() ? stella.osystem().console().properties().get(PropType::Cart_MD5) : "";
}
} // namespace vcs
