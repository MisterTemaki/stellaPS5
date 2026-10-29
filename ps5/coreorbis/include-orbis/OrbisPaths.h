// Stella PS5: the /data/stella folder layout and the boot log (orbis-shims/orbis_paths.cpp).
//
//   /data/stella/roms         games, in a folder per system: Atari2600 (.a26 .bin .rom), Atari5200 (.a52 .bin),
//                             Atari7800 (.a78 .bin), AtariLynx (.lnx .lyx .o); also in .zip or .7z; sub-folders are fine
//   /data/stella/bios         5200.rom (optional), 7800 BIOS (U).rom (optional), lynxboot.img (needed for Lynx games)
//   /data/stella/states       save states, <game>.state1 .. <game>.state10
//   /data/stella/covers       box art (downloaded, or your own <ROM file name>.png), crc-cache.txt, wanted.txt
//   /data/stella/logs         boot.log (this run) and boot.prev.log (the run before); installer.log, helper.log
//   /data/stella/stella-ps5.ini  the frontend's settings
//
// USB drives are searched too: /mnt/usbN/stella/roms (N = 0..7) and /mnt/extN/stella/roms.
//
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#ifndef ORBIS_ROOT_DEFAULT
#define ORBIS_ROOT_DEFAULT "/data/stella"
#endif

// Root folder (ORBIS_ROOT_DEFAULT; the host tests point it elsewhere with STELLAPS5_ROOT).
const std::string& OrbisRoot();
// <root>/<sub>, created at boot by OrbisPathsInit.
std::string OrbisDir(const char* sub);
// Creates the folder tree. Returns false when the root itself can't be created (no /data access).
bool OrbisPathsInit();
// Folders that hold ROMs and exist right now (internal first, then USB drives: <mnt>/usbN/stella/roms and
// <mnt>/extN/stella/roms).
std::vector<std::string> OrbisRomRoots();
// Where the console mounts USB and extended drives: "/mnt" (the host tests point it elsewhere with STELLAPS5_MNT)
const std::string& OrbisMnt();
// A path on such a drive (under OrbisMnt())
bool OrbisOnDrive(const std::string& path);

bool OrbisIsDir(const std::string& path);
bool OrbisIsFile(const std::string& path);
bool OrbisMkdirs(const std::string& path);

// <root>/logs/<name>.log (the run before kept as <name>.prev.log): boot.log for the emulator, installer.log
// for the installer/helper payload. Every line also goes to stdout (once the log is open). Lines logged before the file is open
// (before the jailbreak shows /data to the app) are kept and written first.
void OrbisLogOpen(const char* name = "boot");
void OrbisLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OrbisLogClose();
// Debug logs on or off: the "Debug logs" setting (debug_logs= in stella-ps5.ini, on unless it says 0).
// OrbisLogOpen reads it, so the app, the installer and the helper all follow it; while off, OrbisLog writes
// nothing (no file, no stdout) and the log files of earlier runs are left as they are.
void OrbisLogSetEnabled(bool on);
// Reads the setting again and applies it (the helper, which keeps running, calls this for each request).
void OrbisLogRefresh();
bool OrbisLogEnabled();
// boot.log onto the disk now (fsync): before a step that might take the process down (a game's start)
void OrbisLogSync();
// The open log's file descriptor, for the crash handler's signal-safe write(); -1 before it is open.
int OrbisLogFd();
