// Stella PS5: the payloads (StellaPS5.elf and StellaPS5-helper.elf).
//
// StellaPS5.elf, sent with the PS5 Payload Manager / the ELF loader (port 9021), is what PS5SX2's installer and
// helper are together:
//   1. it installs or updates the dashboard app in /data/homebrew/<TITLE_ID> (eboot.bin = Stella and its frontend,
//      sce_module/libc.prx, sce_sys/param.json, icon0.png, pic0.dds, pic1.dds), built into it (install_data.cpp);
//   2. then it stays running as the helper that lets the app out of its sandbox (ProsperoJailbreak.h), until
//      the console is turned off. A second copy sent while one runs only installs.
// StellaPS5-helper.elf (STELLAPS5_HELPER_ONLY) is step 2 alone: the app carries it and sends it to the ELF
// loader itself when no helper answers (after a reboot, for instance).
//
// Nothing here writes outside /data/stella and the app's folder: /data/homebrew/<TITLE_ID>, or the folder ShadowMount+
// runs it from, written through its mount /system_ex/app/<TITLE_ID> (ProsperoAppPlace.h, from Media Player Classic PS5
// 1.5).
//
// SPDX-License-Identifier: MIT

#include "OrbisPaths.h"
#include "ProsperoJailbreak.h"
#include "ProsperoNotify.h"
#ifndef STELLAPS5_HELPER_ONLY
#include "ProsperoAppPlace.h"
#include "ProsperoInstall.h"
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

#ifndef STELLAPS5_VERSION
#define STELLAPS5_VERSION "dev"
#endif

namespace
{
std::string g_ready_message;

#ifndef STELLAPS5_HELPER_ONLY
#ifndef STELLAPS5_TITLE_ID
#define STELLAPS5_TITLE_ID "PPSA99927"
#endif
// where the payload installs when nothing else runs the app
std::string DefaultAppDir()
{
	const char* env = getenv("STELLAPS5_HOMEBREW");
	return std::string(env && *env ? env : "/data/homebrew") + "/" STELLAPS5_TITLE_ID;
}
#endif

void OnReady()
{
	if (!g_ready_message.empty())
		ProsperoNotify("%s", g_ready_message.c_str());
	ProsperoNotifyFlush();
}
} // namespace

int main()
{
	setvbuf(stdout, nullptr, _IOLBF, 0);
	const bool have_data = OrbisPathsInit();
#ifdef STELLAPS5_HELPER_ONLY
	OrbisLogOpen("helper");
	OrbisLog("[helper] Stella PS5 helper %s (built %s %s), pid %d%s", STELLAPS5_VERSION, __DATE__, __TIME__,
		int(getpid()), have_data ? "" : " (no /data: no log file)");
#else
	OrbisLogOpen("installer");
	OrbisLog("[installer] Stella PS5 %s (built %s %s), pid %d", STELLAPS5_VERSION, __DATE__, __TIME__, int(getpid()));
	if (!have_data)
		OrbisLog("[installer] can't create %s", OrbisRoot().c_str());

	const char* open_hint = "Open it from the Stella PS5 icon on the home screen.";
	char msg[1024];
	const InstallResult installed = InstallApp();
	switch (installed)
	{
		case InstallResult::Installed:
			snprintf(msg, sizeof(msg), "Stella PS5 %s installed. %s", STELLAPS5_VERSION, open_hint);
			break;
		case InstallResult::Updated:
			if ((!AppInstallPlace().mount.empty() || AppInstallPlace().via_link) &&
				AppInstallPlace().source != DefaultAppDir())
				snprintf(msg, sizeof(msg), "Stella PS5 updated to %s (in %s, the copy ShadowMount+ runs). %s",
					STELLAPS5_VERSION, AppInstallPlace().source.c_str(), open_hint);
			else
				snprintf(msg, sizeof(msg), "Stella PS5 updated to %s. %s", STELLAPS5_VERSION, open_hint);
			break;
		case InstallResult::UpToDate:
			snprintf(msg, sizeof(msg), "Stella PS5 %s is ready. %s", STELLAPS5_VERSION, open_hint);
			break;
		case InstallResult::NewerInstalled:
			snprintf(msg, sizeof(msg),
				"Stella PS5: a newer version is installed%s; this payload (%s) left it as it is. Put the newer payload "
				"in your autoload. %s",
				AppInstallPlace().writable ? " (updated from the app)" : "", STELLAPS5_VERSION, open_hint);
			break;
		case InstallResult::CantWrite:
			// ShadowMount+ runs the app from an image or a read-only folder: not written, and no second copy made
			snprintf(msg, sizeof(msg), "%s (This payload is %s; the helper runs.)", AppInstallPlace().reason.c_str(),
				STELLAPS5_VERSION);
			break;
		case InstallResult::Failed:
			snprintf(msg, sizeof(msg), "Stella PS5: could not install the app in %s (see %s/logs/installer.log)",
				AppInstallDir().c_str(), OrbisRoot().c_str());
			break;
		default:
			snprintf(msg, sizeof(msg), "Stella PS5: nothing to install");
			break;
	}
	if (installed != InstallResult::NewerInstalled && installed != InstallResult::CantWrite &&
		!AppInstallPlace().image_pending && SyncAppMeta() > 0)
	{
		const size_t len = strlen(msg);
		snprintf(msg + len, sizeof(msg) - len, " Home screen art updated (restart the PS5 if it doesn't show).");
	}
	OrbisLog("[installer] %s", msg);
	g_ready_message = msg;
#endif

	// the helper: runs until the console is turned off, or returns at once when one already runs
	if (!jailbreak::ServeHelper(OnReady))
	{
#ifndef STELLAPS5_HELPER_ONLY
		ProsperoNotify("%s", g_ready_message.c_str());
#endif
		OrbisLog("[helper] another helper is running: leaving");
	}
	ProsperoNotifyFlush();
	OrbisLogClose();
	return 0;
}
