// Stella PS5 frontend: settings file.
// SPDX-License-Identifier: MIT

#include "fe_settings.h"

#include "fe_emu.h"
#include "fe_games.h"

#include "OrbisPaths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace fe
{
namespace
{
std::string IniPath()
{
	return OrbisRoot() + "/stella-ps5.ini";
}

int Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

// "a26_btn_fire": a console button's key in the file (its system's id, then the button's key)
std::string ButtonKey(int sys, int i)
{
	return std::string(Info(System(sys)).id) + "_" + emu::Buttons(sys).buttons[i].key;
}
constexpr int kShaderCount = 12; // ps5crt::Shader::Count
} // namespace

Settings::Settings()
{
	for (int sys = 0; sys < kSystemCount; sys++)
		DefaultButtons(sys);
}

void Settings::DefaultButtons(int sys)
{
	if (sys < 0 || sys >= kSystemCount)
		return;
	const emu::ButtonLayout& l = emu::Buttons(sys);
	for (int i = 0; i < kMaxButtons; i++)
		buttons[sys][i] = i < l.count ? l.buttons[i].default_ps5 : emu::kPs5ButtonCount - 1;
}

Settings& Config()
{
	static Settings s;
	return s;
}

void Settings::Load()
{
	// off by default; the host tests turn the default on (orbis_paths.cpp reads the same variable)
	if (const char* d = getenv("STELLAPS5_LOGS_DEFAULT"))
		debug_logs = atoi(d) != 0;
	FILE* f = fopen(IniPath().c_str(), "r");
	if (!f)
		return;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		char* eq = strchr(line, '=');
		if (!eq || line[0] == '#' || line[0] == ';')
			continue;
		*eq = 0;
		const std::string key = line;
		const char* val = eq + 1;
		const int n = atoi(val);
		if (key == "scale")
			scale = Clamp(n, 0, 2);
		else if (key == "shader")
			shader = Clamp(n, 0, kShaderCount - 1);
		else if (key == "aspect")
			aspect = Clamp(n, 0, 2);
		else if (key == "a52_blend")
			a52_blend = Clamp(n, 0, emu::kA52BlendCount - 1);
		else if (key == "a52_hack")
			a52_hack = Clamp(n, 0, emu::kA52HackCount - 1);
		else if (key == "a52_hifi_pokey")
			a52_hifi_pokey = n != 0;
		else if (key == "a78_dual_stick")
			a78_dual_stick = n != 0;
		else if (key == "lnx_rotation")
			lnx_rotation = Clamp(n, 0, emu::kLynxRotationCount - 1);
		else if (key == "controls_system")
			controls_system = Clamp(n, 0, kSystemCount - 1);
		else if (key == "palette")
			palette = Clamp(n, 0, 2);
		else if (key == "tv_effects")
			tv_effects = Clamp(n, 0, 4);
		else if (key == "phosphor")
			phosphor = Clamp(n, 0, 2);
		else if (key == "phosphor_blend")
			phosphor_blend = Clamp(n, 0, 100) / 5 * 5;
		else if (key == "crop_hmove")
			crop_hmove = n != 0;
		else if (key == "smooth")
			smooth = n != 0;
		else if (key == "scanlines")
			scanlines = n != 0;
		else if (key == "show_fps")
			show_fps = n != 0;
		else if (key == "audio")
			audio = n != 0;
		else if (key == "volume")
			volume = Clamp(n, 0, 100);
		else if (key == "stereo")
			stereo = Clamp(n, 0, 2);
		else if (key == "ff_speed")
			ff_speed = n == 0 ? 0 : Clamp(n, 150, 1000);
		else if (key == "rewind")
			rewind = n != 0;
		else if (key == "tv_format")
			tv_format = Clamp(n, 0, 6);
		else if (key.find("_btn_") != std::string::npos)
		{
			for (int sys = 0; sys < kSystemCount; sys++)
				for (int i = 0; i < emu::Buttons(sys).count; i++)
					if (key == ButtonKey(sys, i))
						buttons[sys][i] = Clamp(n, 0, emu::kPs5ButtonCount - 1);
		}
		else if (key == "state_slot")
			state_slot = Clamp(n, 1, 10);
		else if (key == "covers_download")
			covers_download = n != 0;
		else if (key == "debug_logs")
			debug_logs = n != 0;
		else if (key == "language")
			language = Clamp(n, 0, 4);
		else if (key == "shelf_family")
			shelf_family = Clamp(n, 0, 64);
		else if (key == "last_tab")
			last_tab = Clamp(n, 0, 2);
		else if (key == "last_rom")
			last_rom = val;
	}
	fclose(f);
	OrbisLog("[settings] loaded %s", IniPath().c_str());
}

void Settings::Save() const
{
	const std::string tmp = IniPath() + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f)
	{
		OrbisLog("[settings] can't write %s", tmp.c_str());
		return;
	}
	fprintf(f, "# Stella PS5\n");
	fprintf(f, "scale=%d\n", scale);
	fprintf(f, "shader=%d\n", shader);
	fprintf(f, "aspect=%d\n", aspect);
	fprintf(f, "palette=%d\n", palette);
	fprintf(f, "tv_effects=%d\n", tv_effects);
	fprintf(f, "phosphor=%d\n", phosphor);
	fprintf(f, "phosphor_blend=%d\n", phosphor_blend);
	fprintf(f, "crop_hmove=%d\n", crop_hmove ? 1 : 0);
	fprintf(f, "smooth=%d\n", smooth ? 1 : 0);
	fprintf(f, "scanlines=%d\n", scanlines ? 1 : 0);
	fprintf(f, "show_fps=%d\n", show_fps ? 1 : 0);
	fprintf(f, "audio=%d\n", audio ? 1 : 0);
	fprintf(f, "volume=%d\n", volume);
	fprintf(f, "stereo=%d\n", stereo);
	fprintf(f, "ff_speed=%d\n", ff_speed);
	fprintf(f, "rewind=%d\n", rewind ? 1 : 0);
	fprintf(f, "tv_format=%d\n", tv_format);
	fprintf(f, "a52_blend=%d\n", a52_blend);
	fprintf(f, "a52_hack=%d\n", a52_hack);
	fprintf(f, "a52_hifi_pokey=%d\n", a52_hifi_pokey ? 1 : 0);
	fprintf(f, "a78_dual_stick=%d\n", a78_dual_stick ? 1 : 0);
	fprintf(f, "lnx_rotation=%d\n", lnx_rotation);
	fprintf(f, "controls_system=%d\n", controls_system);
	for (int sys = 0; sys < kSystemCount; sys++)
		for (int i = 0; i < emu::Buttons(sys).count; i++)
			fprintf(f, "%s=%d\n", ButtonKey(sys, i).c_str(), buttons[sys][i]);
	fprintf(f, "state_slot=%d\n", state_slot);
	fprintf(f, "covers_download=%d\n", covers_download ? 1 : 0);
	fprintf(f, "debug_logs=%d\n", debug_logs ? 1 : 0);
	fprintf(f, "language=%d\n", language);
	fprintf(f, "shelf_family=%d\n", shelf_family);
	fprintf(f, "last_tab=%d\n", last_tab);
	fprintf(f, "last_rom=%s\n", last_rom.c_str());
	bool ok = ferror(f) == 0;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp.c_str(), IniPath().c_str()) != 0)
	{
		remove(tmp.c_str());
		OrbisLog("[settings] can't save %s (disk full?)", IniPath().c_str());
	}
}
} // namespace fe
