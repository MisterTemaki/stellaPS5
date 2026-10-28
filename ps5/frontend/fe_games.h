// Stella PS5 frontend: the game library -- every ROM under the ROM folders, with its title and official name.
//
// Atari 2600 games are .a26, .bin or .rom files (also inside a .zip or .7z). Each game gets two names:
//   - its title, from Stella's own property database (src/emucore/DefProps.hxx, by the ROM's MD5: fe_stella.h):
//     "Pitfall! (1982) (Activision)" -> "Pitfall!"; a game Stella's database doesn't know takes the No-Intro name's
//     title, else its file name;
//   - its No-Intro name, the way libretro names it (libretro-thumbnails names the covers after it):
//       1. its file name, when it is already a No-Intro name ("Pitfall! - Pitfall Harry's Jungle Adventure (USA).a26");
//       2. otherwise the CRC32 of the ROM, looked up in the table built into the app (data/nointro.tsv: No-Intro's
//          Atari 2600 DAT; tools/make_gamedb.py makes it);
//       3. otherwise the file name, loosely: "pitfall.bin" -> the best entry with that title (USA first, then World,
//          Europe, Japan).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fe
{
enum class System : uint8_t
{
	A2600, // Atari 2600 (VCS): Stella
	A5200, // Atari 5200: a5200
	A7800, // Atari 7800: ProSystem
	Lynx, // Atari Lynx: Gearlynx
	Count
};

// The shelf's tabs: every game, then one tab per system that has games.
enum class Family : uint8_t
{
	All,
	A2600,
	A5200,
	A7800,
	Lynx,
	Count
};
const char* FamilyName(Family f); // "All games", "Atari 2600"...

struct SystemInfo
{
	const char* id; // "a26": the table's and the cover cache's tag
	const char* name; // "Atari 2600"
	const char* thumbs; // the libretro-thumbnails repository
	const char* folder; // "Atari2600": its folder in roms/ and in covers/
	Family family;
};
const SystemInfo& Info(System s);

// Creates roms/<folder> and covers/<folder> for every system (Atari2600, Atari5200, Atari7800, AtariLynx).
void PrepareFolders();

// ".a26" -> System::A2600, ".a52" -> A5200, ".a78" -> A7800, ".lnx" / ".lyx" / ".o" -> Lynx; ".bin" and ".rom" are any
// system's (*ambiguous set; A2600 given): the folder they are in decides (roms/Atari5200/...), else the game tables
// (ScanGames, by CRC), else the 2600. False for anything that isn't a game we list.
bool SystemForExt(const std::string& ext, System* out, bool* ambiguous = nullptr);
// A .bin's system from the folders it is in ("Atari5200", "5200", "7800", "Lynx"... up to three levels up); false if
// none says.
bool SystemFromFolders(const std::string& path, System* out);

struct GameInfo
{
	std::string path; // full path
	std::string file_base; // file name without extension
	std::string ext; // ".a26", ".bin", ".zip", ".7z"... lower case
	System system = System::A2600;
	std::string nointro; // official name, "" until known (or unknown)
	std::string stella; // the name Stella's database gives it (by its MD5), "" until known (or unknown)
	std::string md5; // the ROM's MD5 (Stella's database key), "" until read
	std::string title; // what the shelf shows
	std::string region; // "USA", "Europe"... from the name's tags
	bool on_usb = false;
	bool on_network = false; // on a share (/smb/..., /nfs/...): named without reading the ROM (netfs)
	bool name_by_crc = false; // the No-Intro name came from the CRC (not the file name)
	bool crc_pending = false; // on a share, named from its file name only so far: NetNaming reads its CRC
	bool ambiguous = false; // a .bin or .rom no folder speaks for: the game tables (by CRC) tell its system, else the 2600
	bool system_known = false; // its folder named its system
};

// The system of a game being started: its file name's extension (name: the name inside its archive, or the file's);
// for a .bin or .rom, the folders of path, else the game tables (data: the ROM, by its CRC), else the 2600.
System SystemOfRom(const std::string& path, const std::string& name, const std::vector<uint8_t>& data);

// The table built into the app.
namespace gamedb
{
size_t Count();
// "" when unknown. *found_in: the system whose table knew it.
std::string ByCrc(System s, uint32_t crc, System* found_in);
// Exact No-Intro name ("Super Mario Bros. (World)") -> itself, or "".
std::string Exact(System s, const std::string& name, System* found_in);
// Loose: title without tags, any case ("super mario bros") -> best regional entry, or "".
std::string Loose(System s, const std::string& file_base, System* found_in);
// "Super Mario Bros. (World)" -> "Super Mario Bros."; "Legend of Zelda, The (...)" -> "The Legend of Zelda"
std::string Title(const std::string& nointro);
std::string Region(const std::string& nointro);
} // namespace gamedb

// The ROM's CRC32s: of the whole file, and without its header (2600 ROMs have none: the same), and its MD5 (when md5
// is given: 32 hex digits). For a .zip or .7z, of the ROM inside (unpacked: 2600 games are small); *inner_ext gets
// that ROM's extension. False if it can't be read. cancel (when given): stops the read when it turns true (false is
// returned). certain (when given): false returned for a reason of the file's own, read without an error (a .7z with
// no game, encrypted, too big...): it may be remembered.
bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext = nullptr,
	const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr, std::string* md5 = nullptr);

// The folders to look for games in: OrbisRomRoots() plus the network shares that are connected (in memory, no I/O).
std::vector<std::string> RomRoots();

// Names the games on shares by their ROM's CRC and MD5, in the background (Snes9x PS5's design, by thatboialex). A scan does
// not read network ROMs (it would make a big library crawl); this does, one at a time, only for the games with
// crc_pending (a zip or 7z: its directory, which also gives its system), and remembers each CRC in crc-cache.txt, so
// the next ScanGames names them. Add() starts or extends the pass; it stops by itself when a game starts
// (netfs_realtime) and when Stop() or the destructor is called (leaving the shelf).
class NetNaming
{
public:
	NetNaming();
	~NetNaming();
	void Add(const std::vector<GameInfo>& games);
	void Stop();
	int Resolved() const; // ROMs read so far (named or not)
	bool Busy() const;

private:
	struct Impl;
	std::unique_ptr<Impl> m_i;
};

// Archives of games: .zip and .7z (fe_7z.h). The first Atari 2600 game inside (.a26, .bin, .rom), in the archive's
// order, is the game; an archive with none in it is not listed.
bool IsArchiveExt(const std::string& ext);
// The system of a .zip or .7z: the extension of the first game inside. False if none.
// cancel: stops the read (a .7z's); certain: as for RomCrc32.
bool ArchiveSystem(const std::string& path, System* out, const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr);
// The first game inside a .zip or .7z (its name in the archive) and its bytes (512 KiB at most: Stella's biggest). False if none can be
// read; *why (when given) then says why in a sentence for the player (English: T() it), for an archive that can't be
// read, or a .7z that is damaged, encrypted, packed with a method the app can't unpack, or too big. Left alone when
// there is simply no game inside.
bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, std::string* why = nullptr,
	const std::atomic<bool>* cancel = nullptr, bool* certain = nullptr);

// Every ROM under the ROM folders (sub-folders included, 4 levels), sorted by title. Names that need a
// CRC or an MD5 are resolved here too, with a cache (/data/stella/covers/crc-cache.txt) so each ROM is read once.
std::vector<GameInfo> ScanGames();
} // namespace fe
