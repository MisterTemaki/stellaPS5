// Stella PS5 frontend: the menus.
//
// Everything is drawn on the CPU into the 1920x1080 surface (fe_text: Roboto + PromptFont) and shown with
// ps5video::Present, which waits for vsync, so the menus run at 60 Hz.
//
// SPDX-License-Identifier: MIT

#include "fe_menu.h"

#include "fe_emu.h"
#include "fe_i18n.h"
#include "fe_settings.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoAppPlace.h"
#include "ProsperoCrt.h"
#include "ProsperoInput.h"
#include "ProsperoJailbreak.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include "ProsperoUpdate.h"
#include "ProsperoVideo.h"

#include "netfs.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <vector>

namespace fe
{
namespace
{
using ps5video::Rgb;

// printf into a std::string (the translated formats)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}
constexpr int W = ps5video::kWidth;
constexpr int H = ps5video::kHeight;

const uint32_t kBg = Rgb(14, 14, 28);
const uint32_t kPanel = Rgb(26, 24, 48);
const uint32_t kAccent = Rgb(150, 125, 255);
const uint32_t kSel = Rgb(78, 62, 170);
const uint32_t kText = Rgb(235, 235, 245);
const uint32_t kDim = Rgb(150, 150, 175);

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- input with key repeat ---------------------------------------------------------------------------
struct Nav
{
	bool up = false, down = false, left = false, right = false;
	bool ok = false, back = false, options = false, menu = false;
};

class NavReader
{
public:
	NavReader()
	{
		ps5input::Poll();
		m_prev = ps5input::Pad(0).buttons; // buttons still held from before don't count
	}
	Nav Read()
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~m_prev;
		m_prev = cur;
		const double now = Now();
		Nav n;
		n.up = Repeat(cur, down, SCE_PAD_BUTTON_UP, now, 0);
		n.down = Repeat(cur, down, SCE_PAD_BUTTON_DOWN, now, 1);
		n.left = Repeat(cur, down, SCE_PAD_BUTTON_LEFT, now, 2);
		n.right = Repeat(cur, down, SCE_PAD_BUTTON_RIGHT, now, 3);
		n.ok = down & SCE_PAD_BUTTON_CROSS;
		n.back = down & SCE_PAD_BUTTON_CIRCLE;
		n.options = down & SCE_PAD_BUTTON_OPTIONS;
		const uint32_t combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3;
		n.menu = (cur & combo) == combo && (down & combo);
		return n;
	}

private:
	bool Repeat(uint32_t cur, uint32_t down, uint32_t bit, double now, int i)
	{
		if (down & bit)
		{
			m_next[i] = now + 0.40;
			return true;
		}
		if ((cur & bit) && now >= m_next[i])
		{
			m_next[i] = now + 0.07;
			return true;
		}
		return false;
	}
	uint32_t m_prev = 0;
	double m_next[4] = {};
};

void Present()
{
	ps5video::Present(0, 0, 0, 0, true);
}

void Header(const char* subtitle)
{
	ps5video::FillRect(0, 0, W, H, kBg);
	ps5video::FillRect(0, 0, W, 150, kPanel);
	ps5video::FillRect(0, 150, W, 4, kAccent);
	DrawText(80, 34, "Stella PS5", 6, kAccent);
	const char* ver = "Stella " STELLAPS5_CORE_VERSION " - PS5 " STELLAPS5_VERSION;
	DrawText(W - 80 - TextWidth(ver, 2), 40, ver, 2, kDim);
	if (subtitle && *subtitle)
		DrawText(84, 104, FitText(T(subtitle), 3, W - 168).c_str(), 3, kDim);
}

void Footer(const std::string& help)
{
	ps5video::FillRect(0, H - 80, W, 80, kPanel);
	DrawText(80, H - 60, help.c_str(), 3, kDim);
}

// A box of label/value rows (pause menu, settings); a row with header=true is a section title.
struct Row
{
	std::string label;
	std::string value; // "" = an action
	bool enabled = true;
	bool header = false;
};

// The box, centred in the screen less `room_below` (the settings' help lines go there); returns its bottom edge
int DrawOptionBox(const std::string& title, const std::vector<Row>& rows, int sel, bool over_game, int room_below = 0)
{
	const int scale = 3;
	const int row_h = 46;
	const int visible = std::min(int(rows.size()), 16);
	const int bw = 1180;
	const int bh = 120 + visible * row_h + 30;
	const int bx = (W - bw) / 2;
	const int by = std::max(20, (H - bh - room_below) / 2);
	int top = 0;
	if (int(rows.size()) > visible)
		top = std::max(0, std::min(int(rows.size()) - visible, sel - visible / 2));
	if (!over_game)
		ps5video::FillRect(bx - 4, by - 4, bw + 8, bh + 8, kAccent);
	ps5video::FillRect(bx, by, bw, bh, kPanel);
	ps5video::FillRect(bx, by + 86, bw, 3, kAccent);
	DrawText(bx + 40, by + 24, FitText(T(title.c_str()), 5, bw - 80).c_str(), 5, kAccent);
	for (int i = 0; i < visible; i++)
	{
		const Row& row = rows[size_t(top + i)];
		const int y = by + 110 + i * row_h;
		if (row.header)
		{
			DrawText(bx + 40, y + 6, FitText(T(row.label.c_str()), 2, bw - 80).c_str(), 2, kAccent);
			ps5video::FillRect(bx + 40, y + 34, bw - 80, 1, kSel);
			continue;
		}
		if (top + i == sel)
			ps5video::FillRect(bx + 16, y - 6, bw - 32, row_h, kSel);
		const uint32_t col = row.enabled ? kText : kDim;
		// the label gets what the value leaves (a translation may be longer than the English)
		const std::string tv = row.value.empty() ? std::string() : std::string(T(row.value.c_str()));
		const std::string v = tv.empty() ? tv : (top + i == sel ? "< " + tv + " >" : tv);
		const int label_w = bw - 80 - (v.empty() ? 0 : TextWidth(v.c_str(), scale) + 30);
		DrawText(bx + 40, y, FitText(T(row.label.c_str()), scale, label_w).c_str(), scale, col);
		if (!v.empty())
			DrawText(bx + bw - 40 - TextWidth(v.c_str(), scale), y, v.c_str(), scale, top + i == sel ? kText : kAccent);
	}
	if (int(rows.size()) > visible)
	{
		const int track = visible * row_h;
		const int thumb = std::max(30, track * visible / int(rows.size()));
		const int ty = by + 104 + (track - thumb) * top / std::max(1, int(rows.size()) - visible);
		ps5video::FillRect(bx + bw - 12, ty, 6, thumb, kAccent);
	}
	return by + bh + 4;
}

int Step(int sel, int dir, const std::vector<Row>& rows)
{
	const int n = int(rows.size());
	for (int i = 0; i < n; i++)
	{
		sel = (sel + dir + n) % n;
		if (!rows[size_t(sel)].header)
			return sel;
	}
	return sel;
}

const char* OnOff(bool b)
{
	return b ? "On" : "Off";
}

// ---- settings ----------------------------------------------------------------------------------------
enum SettingRow
{
	H_VIDEO,
	S_SHADER,
	S_SCALE,
	S_ASPECT,
	S_SMOOTH,
	S_SCANLINES,
	S_FPS,
	H_AUDIO,
	S_AUDIO,
	S_VOLUME,
	H_EMU,
	S_FF,
	S_REWIND,
	H_A2600,
	S_PALETTE,
	S_TVFX,
	S_PHOSPHOR,
	S_PHOSBLEND,
	S_CROP,
	S_STEREO,
	S_TVFORMAT,
	H_A5200,
	S_A52_BLEND,
	S_A52_HACK,
	S_A52_POKEY,
	H_A7800,
	S_A78_DUAL,
	H_LYNX,
	S_LNX_ROTATION,
	H_CONTROLS,
	S_CONTROLS_SYS, // whose buttons the rows below are
	S_BTN_FIRST,
	S_BTN_LAST = S_BTN_FIRST + kMaxButtons - 1,
	S_BTN_DEFAULT,
	H_LIBRARY,
	S_COVERS,
	S_NETWORK, // Network shares: a screen of its own (fe_netscreen.cpp); not over a game (its files may be on a share)
	H_SYSTEM,
	S_LANGUAGE,
	S_DEBUGLOGS,
	S_UPDATES,
	S_COUNT,
	S_BACK = S_COUNT, // the last row
};

// What a setting row says at the bottom of the screen while it is selected ("" = nothing).
const char* SettingHelp(int s)
{
	switch (s)
	{
		case S_PALETTE: return "The TIA's colours: Stella's standard palette, z26's, or Stella's adjustable custom one.";
		case S_TVFX: return "Blargg's NTSC filter, as Stella has it: the picture as a TV showed it through each kind of cable.";
		case S_PHOSPHOR:
		case S_PHOSBLEND:
			return "Blends each frame with the last one, as a TV's phosphor did: games that flicker look solid. Stella's database "
				   "turns it on for the games that need it.";
		case S_CROP: return "Hides the left 8 pixels, where many games leave black lines (the HMOVE bars).";
		case S_STEREO: return "A few games (and homebrews) play their two sound channels apart. From the next game.";
		case S_TVFORMAT:
			return "NTSC games run at 60 Hz, PAL and SECAM ones at 50 Hz; Stella's database and its detection know which. "
				   "Changing it restarts the game.";
		case S_A52_BLEND: return "Blends successive frames, as a TV did: games that flicker look solid.";
		case S_A52_HACK:
			return "Dual stick: the right stick is the second joystick (Robotron: 2084, Space Dungeon); the keypad's 1-9 then "
				   "only on the screen keypad. Swap ports: for Wizard of Wor.";
		case S_A52_POKEY: return "a5200's high-fidelity sound chip. Turn it off for the games with digitised speech. From the next game.";
		case S_A78_DUAL: return "The right stick is the second joystick, for the twin-stick games (Robotron: 2084).";
		case S_LNX_ROTATION: return "Games made to hold the Lynx sideways are turned by Gearlynx's database; this turns any game.";
		case S_CONTROLS_SYS: return "The buttons below are this system's; each system has its own.";
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				const emu::ButtonLayout& l = emu::Buttons(Config().controls_system);
				const int i = s - S_BTN_FIRST;
				return i < l.count ? l.buttons[i].help : "";
			}
			return "";
	}
}

const int kFfSteps[] = {150, 200, 300, 400, 500, 1000, 0};
constexpr int kFfCount = int(sizeof(kFfSteps) / sizeof(kFfSteps[0]));

// The Network shares row: how many shares are set up, and how many of them are connected.
std::string NetworkSummary()
{
	const std::vector<NetfsShareInfo> shares = netfs_shares();
	if (shares.empty())
		return "None";
	int ready = 0;
	for (const NetfsShareInfo& i : shares)
		ready += i.state == NetfsState::Ready;
	return Fmt(T("%d of %d connected"), ready, int(shares.size()));
}

// ---- updates (ProsperoUpdate.h): the helper checks, downloads and installs; this screen shows it ----------------
// Where the console runs the app from (Media Player Classic PS5 1.5's ShadowMount+ fix, ProsperoAppPlace.h): an
// image or a read-only folder can't take an update, and that is said before anything is downloaded.
std::string EnvOr(const char* name, const char* fallback)
{
	const char* v = getenv(name);
	return v && *v ? v : fallback;
}

const AppPlace& Place(bool refresh = false)
{
	static AppPlace place;
	static bool found = false;
	if (!found || refresh)
	{
		place = FindAppPlace(STELLAPS5_TITLE_ID, EnvOr("STELLAPS5_HOMEBREW", "/data/homebrew") + "/" STELLAPS5_TITLE_ID);
		found = true;
		OrbisLog("[update] %s", place.log.c_str());
	}
	return place;
}

std::string PendingPath()
{
	return OrbisRoot() + "/update-pending.txt";
}

// An update installed: noted, so that the next start can tell whether it runs the new version.
void NoteInstalled(const std::string& version)
{
	if (FILE* f = fopen(PendingPath().c_str(), "w"))
	{
		fprintf(f, "%s\n%s\n0\n", version.c_str(), Place().source.c_str());
		fclose(f);
	}
}

// What opens the new version: the eboot the console runs -- /app0 is the app's own view of its folder, when it sees
// it --, else through the mount ShadowMount+ made, else the folder the update went to.
std::string EbootPath()
{
	if (OrbisIsFile("/app0/eboot.bin"))
		return "/app0/eboot.bin";
	const AppPlace& p = Place();
	return (p.mount.empty() ? p.dir : p.mount) + "/eboot.bin";
}

struct UpdateView
{
	update::Status st;
	bool helper = false;
	double polled = -1;
	bool noted = false; // the installed version is noted (update-pending.txt)
};
UpdateView g_upd;

void PollUpdate(bool force)
{
	if (!force && Now() - g_upd.polled < 0.5)
		return;
	g_upd.polled = Now();
	std::string reply;
	g_upd.helper = jailbreak::UpdateRequest("status", reply) && update::Decode(reply, g_upd.st);
	if (g_upd.helper && g_upd.st.phase == update::Phase::Done && !g_upd.noted)
	{
		g_upd.noted = true;
		NoteInstalled(g_upd.st.latest); // the next start checks it runs
		OrbisLog("[update] %s installed (in %s)", g_upd.st.latest.c_str(), Place().source.c_str());
	}
}

std::string UpdateValue()
{
	char buf[96];
	if (!g_upd.helper)
		return "Needs the helper";
	const update::Status& st = g_upd.st;
	switch (st.phase)
	{
		case update::Phase::Checking: return "Checking...";
		case update::Phase::UpToDate: return "Up to date";
		case update::Phase::Available:
			if (!Place().writable)
			{
				snprintf(buf, sizeof(buf), T("%s: can't be installed here"), st.latest.c_str());
				return buf;
			}
			snprintf(buf, sizeof(buf), T("Install %s (%.1f MB)"), st.latest.c_str(), st.size / 1048576.0);
			return buf;
		case update::Phase::Downloading:
			snprintf(buf, sizeof(buf), T("Downloading %d%%"), st.total ? int(st.done * 100 / st.total) : 0);
			return buf;
		case update::Phase::Installing:
			snprintf(buf, sizeof(buf), T("Installing %d/%d"), int(st.done), int(st.total));
			return buf;
		case update::Phase::Done: return "Installed: restart";
		case update::Phase::Failed: return "Failed: try again";
		default: return "Check now";
	}
}

// The copy the console runs can't take the update (an image, a read-only folder, one that can't be identified for
// sure): said before anything is downloaded, with what to do instead.
void CantInstall(const update::Status& st)
{
	const AppPlace& place = Place();
	OrbisLog("[update] %s is available, but it can't be installed here: %s", st.latest.c_str(), place.reason.c_str());
	if (place.unsure)
		MessageBox("Updates", Fmt(T("Stella PS5 %s is available (you have %s).\n\nThe app can't tell for sure which folder "
									"this copy runs from (%s), so it doesn't write an update anywhere. Update it the way "
									"you put it there, with the new version's folder (from the release's zip)."),
								  st.latest.c_str(), STELLAPS5_VERSION, place.source.c_str()));
	else if (place.image)
		MessageBox("Updates", Fmt(T("Stella PS5 %s is available (you have %s).\n\nThis copy runs from an image that "
									"ShadowMount+ mounted (%s): the app can't write an update into it. Update the image the "
									"way you made it, with the new version's folder (from the release's zip)."),
								  st.latest.c_str(), STELLAPS5_VERSION, place.source.c_str()));
	else
		MessageBox("Updates", Fmt(T("Stella PS5 %s is available (you have %s).\n\nThis copy runs from %s, where the app "
									"can't write: it can't be updated from here. Update that folder the way you put it "
									"there (with the new version's folder from the release's zip, or its payload)."),
								  st.latest.c_str(), STELLAPS5_VERSION, place.source.c_str()));
}

void UpdateAction()
{
	std::string reply;
	switch (g_upd.st.phase)
	{
		case update::Phase::Available:
			if (!Place(true).writable)
			{
				CantInstall(g_upd.st);
				break;
			}
			jailbreak::UpdateRequest("install", reply);
			break;
		case update::Phase::Checking:
		case update::Phase::Downloading:
		case update::Phase::Installing: break;
		case update::Phase::Done:
		{
			// the new build, opened in this one's place
			if (!g_upd.noted)
			{
				g_upd.noted = true;
				NoteInstalled(g_upd.st.latest);
			}
			const std::string path = EbootPath();
			OrbisLog("[update] restarting %s", path.c_str());
			Config().Save();
			OrbisLogClose();
			const int rc = sceSystemServiceLoadExec(path.c_str(), nullptr);
			if (rc == 0)
				for (int i = 0; i < 100; i++)
					usleep(100 * 1000);
			OrbisLogOpen("boot-after-update");
			OrbisLog("[update] sceSystemServiceLoadExec(%s) -> %x: close the app and open it again", path.c_str(), unsigned(rc));
			MessageBox("Updates", "Close Stella PS5 and open it again to use the new version.");
			break;
		}
		default:
			Place(true); // looked up again: ShadowMount+ may have mounted the app since
			jailbreak::UpdateRequest("check", reply);
			break;
	}
	PollUpdate(true);
}

Row SettingRowFor(int s)
{
	const Settings& c = Config();
	char buf[48];
	switch (s)
	{
		case H_VIDEO: return {"VIDEO", "", true, true};
		case S_SHADER: return {"Shader", ps5crt::Name(ps5crt::Shader(c.shader))};
		case S_SCALE: return {"Screen size", ps5video::ScaleName(ps5video::Scale(c.scale))};
		case S_ASPECT: return {"Aspect ratio", emu::kAspects[c.aspect % emu::kAspectCount].name};
		case S_SMOOTH: return {"Smooth picture", c.shader ? "(shader)" : OnOff(c.smooth), c.shader == 0};
		case S_SCANLINES: return {"Scanlines (CRT effect)", c.shader ? "(shader)" : OnOff(c.scanlines), c.shader == 0};
		case S_FPS: return {"Show FPS", OnOff(c.show_fps)};
		case H_AUDIO: return {"AUDIO", "", true, true};
		case S_AUDIO: return {"Sound", OnOff(c.audio)};
		case S_VOLUME: snprintf(buf, sizeof(buf), "%d%%", c.volume); return {"Volume", buf};
		case H_EMU: return {"EMULATION", "", true, true};
		case S_FF:
			if (c.ff_speed == 0)
				return {"Fast forward speed (hold R2)", "Unlimited"};
			snprintf(buf, sizeof(buf), "%d%%", c.ff_speed);
			return {"Fast forward speed (hold R2)", buf};
		case S_REWIND: return {"Rewind (hold L2 + R2)", OnOff(c.rewind)};
		case H_A2600: return {"ATARI 2600 (STELLA)", "", true, true};
		case S_PALETTE: return {"Palette", emu::kPalettes[c.palette % emu::kPaletteCount].name};
		case S_TVFX: return {"TV effects (NTSC filter)", emu::kTvEffects[c.tv_effects % emu::kTvEffectCount].name};
		case S_PHOSPHOR: return {"Phosphor (flicker blend)", emu::kPhosphor[c.phosphor % emu::kPhosphorCount].name};
		case S_PHOSBLEND: snprintf(buf, sizeof(buf), "%d%%", c.phosphor_blend); return {"Phosphor blend", buf, c.phosphor != 1};
		case S_CROP: return {"Hide the HMOVE bars (left edge)", OnOff(c.crop_hmove)};
		case S_STEREO: return {"Stereo", emu::kStereo[c.stereo % emu::kStereoCount].name};
		case S_TVFORMAT: return {"TV format", emu::kTvFormats[c.tv_format % emu::kTvFormatCount].name};
		case H_A5200: return {"ATARI 5200 (A5200)", "", true, true};
		case S_A52_BLEND: return {"Frame blending", emu::kA52Blends[c.a52_blend % emu::kA52BlendCount].name};
		case S_A52_HACK: return {"Controller hack", emu::kA52Hacks[c.a52_hack % emu::kA52HackCount].name};
		case S_A52_POKEY: return {"High-fidelity POKEY sound", OnOff(c.a52_hifi_pokey)};
		case H_A7800: return {"ATARI 7800 (PROSYSTEM)", "", true, true};
		case S_A78_DUAL: return {"Dual stick (right stick = player 2)", OnOff(c.a78_dual_stick)};
		case H_LYNX: return {"ATARI LYNX (GEARLYNX)", "", true, true};
		case S_LNX_ROTATION: return {"Screen rotation", emu::kLynxRotations[c.lnx_rotation % emu::kLynxRotationCount].name};
		case H_CONTROLS: return {"CONTROLS (console button  ->  PS5 button)", "", true, true};
		case S_CONTROLS_SYS: return {"Buttons of", emu::SystemLabel(c.controls_system)};
		case S_BTN_DEFAULT: return {"Default button layout", ""};
		case H_LIBRARY: return {"LIBRARY", "", true, true};
		case S_COVERS: return {"Download covers", OnOff(c.covers_download)};
		case S_NETWORK: return {"Network shares", NetworkSummary()};
		case H_SYSTEM: return {"SYSTEM", "", true, true};
		case S_LANGUAGE: return {"Language", LanguageName(c.language)};
		case S_DEBUGLOGS: return {"Debug logs", OnOff(c.debug_logs)};
		case S_UPDATES: return {Fmt(T("Updates (this is %s)"), STELLAPS5_VERSION), UpdateValue()};
		case S_BACK: return {"Back", ""};
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				const int sys = c.controls_system;
				const int i = s - S_BTN_FIRST;
				const int b = c.buttons[sys][i];
				return {T(emu::Buttons(sys).buttons[i].name),
					emu::kPs5Buttons[b >= 0 && b < emu::kPs5ButtonCount ? b : emu::kPs5ButtonCount - 1].name};
			}
			return {"", ""};
	}
}

int Cycle(int v, int dir, int n)
{
	return ((v + dir) % n + n) % n;
}

void ChangeSetting(int s, int dir)
{
	Settings& c = Config();
	switch (s)
	{
		case S_SHADER: c.shader = Cycle(c.shader, dir, int(ps5crt::Shader::Count)); break;
		case S_SCALE: c.scale = Cycle(c.scale, dir, int(ps5video::Scale::Count)); break;
		case S_ASPECT: c.aspect = Cycle(c.aspect, dir, emu::kAspectCount); break;
		case S_PALETTE: c.palette = Cycle(c.palette, dir, emu::kPaletteCount); break;
		case S_TVFX: c.tv_effects = Cycle(c.tv_effects, dir, emu::kTvEffectCount); break;
		case S_PHOSPHOR: c.phosphor = Cycle(c.phosphor, dir, emu::kPhosphorCount); break;
		case S_PHOSBLEND: c.phosphor_blend = Cycle(c.phosphor_blend / 5, dir, 21) * 5; break;
		case S_CROP: c.crop_hmove = !c.crop_hmove; break;
		case S_SMOOTH: c.smooth = !c.smooth; break;
		case S_SCANLINES: c.scanlines = !c.scanlines; break;
		case S_FPS: c.show_fps = !c.show_fps; break;
		case S_AUDIO: c.audio = !c.audio; break;
		case S_VOLUME: c.volume = std::max(0, std::min(100, c.volume + dir * 10)); break;
		case S_STEREO: c.stereo = Cycle(c.stereo, dir, emu::kStereoCount); break;
		case S_FF:
		{
			int i = 0;
			while (i < kFfCount - 1 && kFfSteps[i] != c.ff_speed)
				i++;
			c.ff_speed = kFfSteps[Cycle(i, dir, kFfCount)];
			break;
		}
		case S_REWIND: c.rewind = !c.rewind; break;
		case S_TVFORMAT: c.tv_format = Cycle(c.tv_format, dir, emu::kTvFormatCount); break;
		case S_A52_BLEND: c.a52_blend = Cycle(c.a52_blend, dir, emu::kA52BlendCount); break;
		case S_A52_HACK: c.a52_hack = Cycle(c.a52_hack, dir, emu::kA52HackCount); break;
		case S_A52_POKEY: c.a52_hifi_pokey = !c.a52_hifi_pokey; break;
		case S_A78_DUAL: c.a78_dual_stick = !c.a78_dual_stick; break;
		case S_LNX_ROTATION: c.lnx_rotation = Cycle(c.lnx_rotation, dir, emu::kLynxRotationCount); break;
		case S_CONTROLS_SYS: c.controls_system = Cycle(c.controls_system, dir, kSystemCount); break;
		case S_COVERS: c.covers_download = !c.covers_download; break;
		case S_LANGUAGE: c.language = Cycle(c.language, dir, int(Language::Count)); break;
		case S_DEBUGLOGS:
			c.debug_logs = !c.debug_logs;
			OrbisLogSetEnabled(c.debug_logs); // at once: the line saying so is the last (or first) one written
			break;
		case S_BTN_DEFAULT: c.DefaultButtons(c.controls_system); break;
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				int& b = c.buttons[c.controls_system][s - S_BTN_FIRST];
				b = Cycle(b, dir, emu::kPs5ButtonCount);
			}
			break;
	}
	emu::ApplySettings();
}

// The rows shown, as SettingRow ids: every one, but the CONTROLS rows past the chosen system's buttons
std::vector<int> SettingIds()
{
	std::vector<int> ids;
	const int buttons = emu::Buttons(Config().controls_system).count;
	for (int s = 0; s < S_COUNT; s++)
		if (s < S_BTN_FIRST || s > S_BTN_LAST || s - S_BTN_FIRST < buttons)
			ids.push_back(s);
	ids.push_back(S_BACK);
	return ids;
}

std::vector<Row> SettingRows(const std::vector<int>& ids)
{
	std::vector<Row> rows;
	for (int s : ids)
	{
		rows.push_back(SettingRowFor(s));
		if (s == S_NETWORK && emu::GameLoaded())
			rows.back().enabled = false; // changing the shares reconnects them: not while a game may be reading from one
	}
	return rows;
}

void Background(bool over_game)
{
	if (over_game)
	{
		// the paused game, darkened, under the box
		ps5video::FillRect(0, 0, W, H, Rgb(0, 0, 0));
		emu::RedrawLastFrame();
		ps5video::DarkenRect(0, 0, W, H);
		ps5video::DarkenRect(0, 0, W, H);
	}
	else
		Header("Settings");
}

std::vector<std::string> Wrap(const std::string& text, int scale, int max_px);
constexpr int kHelpLineH = 30;
// A help text in at most two lines from y (the second one cut short with "..." if it must)
void DrawHelpLines(const std::string& text, int y, uint32_t color)
{
	std::vector<std::string> lines = Wrap(text, 2, W - 160);
	if (lines.size() > 2)
	{
		std::string rest = lines[1];
		for (size_t i = 2; i < lines.size(); i++)
			rest += " " + lines[i];
		lines.resize(2);
		lines[1] = FitText(rest, 2, W - 160);
	}
	for (size_t i = 0; i < lines.size(); i++)
		DrawText(80, y + int(i) * kHelpLineH, lines[i].c_str(), 2, color);
}

void SettingsScreen(bool over_game)
{
	NavReader nav;
	// the CONTROLS rows show the running game's system's buttons
	if (emu::SystemIndex() >= 0)
		Config().controls_system = emu::SystemIndex();
	int sel = 1; // Shader
	for (;;)
	{
		PollUpdate(false);
		const std::vector<int> ids = SettingIds();
		const std::vector<Row> rows = SettingRows(ids);
		sel = std::min(sel, int(ids.size()) - 1);
		const int id = ids[size_t(sel)];
		Background(over_game);
		// the help line(s) of the row go under the box (up to two lines, wrapped), above the footer (80 pixels): the
		// box leaves them the room
		const int bottom = DrawOptionBox("Settings", rows, sel, over_game, 80 + 2 * kHelpLineH + 16);
		if (!over_game)
			Footer(std::string(icon::Cross) + " " + T("Change") + "     " + icon::DpadLeftRight + " " + T("Adjust") + "     " + icon::Circle + " " + T("Back"));
		const char* help = nullptr;
		uint32_t help_color = kDim;
		if (id == S_UPDATES && g_upd.st.phase == update::Phase::Failed && !g_upd.st.error.empty())
			help = g_upd.st.error.c_str(), help_color = kAccent;
		else if (id == S_UPDATES && !g_upd.helper)
			help = "Updates are downloaded by the helper: send the payload, then open Settings again.";
		else if (id == S_UPDATES && g_upd.st.phase == update::Phase::Available && !Place().writable)
			help = "This copy can't be updated from the app (Cross says why).", help_color = kAccent;
		else if (id < S_COUNT && *SettingHelp(id))
			help = SettingHelp(id);
		if (help)
			DrawHelpLines(T(help), bottom + 12, help_color);
		Present();
		const Nav n = nav.Read();
		if (n.up || n.down)
		{
			sel = Step(sel, n.up ? -1 : 1, rows);
			if (ids[size_t(sel)] == S_NETWORK && !rows[size_t(sel)].enabled)
				sel = Step(sel, n.up ? -1 : 1, rows); // over a game the Network shares row is passed over
		}
		const int now = ids[size_t(sel)];
		if (now == S_UPDATES)
		{
			if (n.ok)
			{
				UpdateAction();
				nav = NavReader();
			}
		}
		else if (now == S_NETWORK)
		{
			if (n.ok && rows[size_t(sel)].enabled)
			{
				NetworkMenu();
				nav = NavReader();
			}
		}
		else if (now < S_COUNT && (n.left || n.right || n.ok))
			ChangeSetting(now, n.left ? -1 : 1);
		if (n.back || n.options || n.menu || (n.ok && now == S_BACK))
		{
			Config().Save();
			return;
		}
	}
}

// Word wrap into lines that fit max_px at `scale`.
std::vector<std::string> WrapLine(const std::string& text, int scale, int max_px);
// Paragraphs ("\n") wrapped one by one; an empty line between two
std::vector<std::string> Wrap(const std::string& text, int scale, int max_px)
{
	std::vector<std::string> lines;
	size_t a = 0;
	for (;;)
	{
		const size_t nl = text.find('\n', a);
		const std::string para = text.substr(a, nl == std::string::npos ? std::string::npos : nl - a);
		const std::vector<std::string> l = WrapLine(para, scale, max_px);
		if (l.empty())
			lines.push_back("");
		lines.insert(lines.end(), l.begin(), l.end());
		if (nl == std::string::npos)
			break;
		a = nl + 1;
	}
	return lines;
}

std::vector<std::string> WrapLine(const std::string& text, int scale, int max_px)
{
	std::vector<std::string> lines;
	std::string cur;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t sp = text.find(' ', pos);
		if (sp == std::string::npos)
			sp = text.size();
		const std::string word = text.substr(pos, sp - pos);
		const std::string tryline = cur.empty() ? word : cur + " " + word;
		if (!cur.empty() && TextWidth(tryline.c_str(), scale) > max_px)
		{
			lines.push_back(cur);
			cur = word;
		}
		else
			cur = tryline;
		pos = sp + 1;
	}
	if (!cur.empty())
		lines.push_back(cur);
	return lines;
}
} // namespace

void CheckAfterUpdate()
{
	FILE* f = fopen(PendingPath().c_str(), "r");
	if (!f)
		return;
	char want[64] = "", dir[512] = "";
	int shown = 0;
	const bool read = fgets(want, sizeof(want), f) && fgets(dir, sizeof(dir), f);
	if (read && fscanf(f, "%d", &shown) != 1)
		shown = 0;
	fclose(f);
	want[strcspn(want, "\r\n")] = 0;
	dir[strcspn(dir, "\r\n")] = 0;
	if (!read || !*want)
	{
		remove(PendingPath().c_str());
		return;
	}
	const bool ok = update::CompareVersions(STELLAPS5_VERSION, want) >= 0;
	shown++;
	OrbisLog("[update] after the update to %s (in %s): this is %s%s", want, dir, STELLAPS5_VERSION,
		ok ? ": the new version runs" : ": still the old version");
	if (ok || shown >= 3)
		remove(PendingPath().c_str()); // seen running (or said three times: not again)
	else if (FILE* w = fopen(PendingPath().c_str(), "w"))
	{
		fprintf(w, "%s\n%s\n%d\n", want, dir, shown);
		fclose(w);
	}
	if (ok)
		ProsperoNotify(T("Stella PS5 updated to %s."), STELLAPS5_VERSION);
	else
		MessageBox("Updates", Fmt(T("Stella PS5 %s was installed in %s, but this is still %s: the console started the old "
									"files.\n\nClose the app completely (on the home screen: Options on its card, Close "
									"Application) and open it again. If it still says %s, ShadowMount+ still shows the old "
									"files: run ShadowMount+ again from your Payload Manager (it registers the app again), or "
									"restart the PS5.\n\nThe version in the home screen's app information only changes when "
									"ShadowMount+ registers the app again."),
								  want, dir, STELLAPS5_VERSION, STELLAPS5_VERSION));
}

void SettingsMenu()
{
	SettingsScreen(false);
}

PauseAction PauseMenu()
{
	Settings& cfg = Config();
	NavReader nav;
	// Resume, the states, the console's own switches and buttons (its system's: emu::ConsoleItems), then the rest
	enum Item
	{
		I_RESUME,
		I_SAVE,
		I_LOAD,
		I_SLOT,
		I_CONSOLE, // the first console item; the rows below follow them
	};
	int sel = I_RESUME;
	std::string toast;
	double toast_until = 0;

	for (;;)
	{
		const std::vector<emu::ConsoleItem> items = emu::ConsoleItems();
		const int n_items = int(items.size());
		const int I_SETTINGS = I_CONSOLE + n_items, I_RESET = I_SETTINGS + 1, I_POWER = I_RESET + 1, I_LIST = I_POWER + 1,
				  I_QUIT = I_LIST + 1, I_COUNT = I_QUIT + 1;
		char buf[128];
		std::vector<Row> rows;
		rows.resize(size_t(I_COUNT));
		rows[I_RESUME] = {"Resume", ""};
		snprintf(buf, sizeof(buf), T("Save state (slot %d)"), cfg.state_slot);
		rows[I_SAVE] = {buf, ""};
		snprintf(buf, sizeof(buf), T("Load state (slot %d)"), cfg.state_slot);
		rows[I_LOAD] = {buf, "", emu::StateExists(cfg.state_slot)};
		snprintf(buf, sizeof(buf), T(emu::StateExists(cfg.state_slot) ? "%d (used)" : "%d (empty)"), cfg.state_slot);
		rows[I_SLOT] = {"State slot", buf};
		for (int k = 0; k < n_items; k++)
			rows[size_t(I_CONSOLE + k)] = {items[size_t(k)].label, items[size_t(k)].value};
		rows[size_t(I_SETTINGS)] = {"Settings", ""};
		rows[size_t(I_RESET)] = {"Reset", ""};
		rows[size_t(I_POWER)] = {"Power cycle", ""};
		rows[size_t(I_LIST)] = {"Back to the game list", ""};
		rows[size_t(I_QUIT)] = {"Quit Stella PS5", ""};
		if (sel >= I_COUNT)
			sel = I_COUNT - 1;

		Background(true);
		std::string sys = emu::SystemName();
		const std::string details = emu::GameDetails();
		if (!details.empty())
			sys += "  -  " + details;
		DrawOptionBox(emu::GameName(), rows, sel, true);
		const int visible = std::min(I_COUNT, 16);
		if (!sys.empty())
			DrawText(W / 2 + 560 - TextWidth(sys.c_str(), 2) - 40, std::max(20, (H - (120 + visible * 46 + 30)) / 2) + 40,
				sys.c_str(), 2, kDim);
		const std::string warning = emu::ControllerWarning();
		if (!toast.empty() && Now() < toast_until)
		{
			const char* tt = T(toast.c_str());
			const int tw = TextWidth(tt, 3);
			ps5video::FillRect((W - tw) / 2 - 30, H - 110, tw + 60, 70, kSel);
			DrawText((W - tw) / 2, H - 90, tt, 3, kText);
		}
		else if (!warning.empty())
		{
			const std::string w = FitText(warning, 2, W - 160);
			DrawText((W - TextWidth(w.c_str(), 2)) / 2, H - 70, w.c_str(), 2, kAccent);
		}
		Present();

		const Nav n = nav.Read();
		if (n.up)
			sel = Step(sel, -1, rows);
		if (n.down)
			sel = Step(sel, 1, rows);
		if (n.back || n.options || n.menu)
			return PauseAction::Resume;

		if (sel == I_SLOT && (n.left || n.right || n.ok))
		{
			cfg.state_slot = n.left ? (cfg.state_slot + 8) % 10 + 1 : cfg.state_slot % 10 + 1;
			cfg.Save();
		}
		else if (sel >= I_CONSOLE && sel < I_SETTINGS && (n.ok || ((n.left || n.right) && !items[size_t(sel - I_CONSOLE)].value.empty())))
		{
			const emu::ConsoleItem& it = items[size_t(sel - I_CONSOLE)];
			emu::ConsoleAction(it.id);
			if (it.value.empty())
				return PauseAction::Resume; // a button (Game Reset...): pressed in the game, which goes on
		}
		else if (n.ok)
		{
			if (sel == I_RESUME)
				return PauseAction::Resume;
			if (sel == I_SAVE)
			{
				toast = emu::SaveState(cfg.state_slot) ? "State saved." : "Could not save the state.";
				toast_until = Now() + 2.0;
				cfg.Save();
			}
			else if (sel == I_LOAD)
			{
				if (emu::LoadState(cfg.state_slot))
					return PauseAction::Resume;
				toast = "This slot is empty.";
				toast_until = Now() + 2.0;
			}
			else if (sel == I_SETTINGS)
			{
				SettingsScreen(true);
				nav = NavReader();
			}
			else if (sel == I_RESET)
			{
				emu::Reset();
				return PauseAction::Resume;
			}
			else if (sel == I_POWER)
			{
				emu::PowerCycle();
				return PauseAction::Resume;
			}
			else if (sel == I_LIST)
				return PauseAction::BackToList;
			else if (sel == I_QUIT)
				return PauseAction::Quit;
		}
	}
}

void MessageBox(const std::string& title, const std::string& text)
{
	NavReader nav;
	std::string one = text;
	std::replace(one.begin(), one.end(), '\n', ' ');
	OrbisLog("[ui] message: %s", one.c_str());
	const int bw = 1400;
	const std::vector<std::string> lines = Wrap(T(text.c_str()), 3, bw - 80);
	const int bh = 220 + int(lines.size()) * 44;
	for (;;)
	{
		Header("");
		const int bx = (W - bw) / 2, by = (H - bh) / 2;
		ps5video::FillRect(bx - 4, by - 4, bw + 8, bh + 8, kAccent);
		ps5video::FillRect(bx, by, bw, bh, kPanel);
		DrawText(bx + 40, by + 30, FitText(T(title.c_str()), 5, bw - 80).c_str(), 5, kAccent);
		int y = by + 120;
		for (const std::string& l : lines)
		{
			DrawText(bx + 40, y, l.c_str(), 3, kText);
			y += 44;
		}
		DrawText(bx + 40, by + bh - 70, (std::string(icon::Cross) + " " + T("OK")).c_str(), 3, kDim);
		Present();
		const Nav n = nav.Read();
		if (n.ok || n.back || n.options)
			return;
	}
}
} // namespace fe
