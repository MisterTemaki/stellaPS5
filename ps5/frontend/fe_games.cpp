// Stella PS5 frontend: the game library (fe_games.h).
// SPDX-License-Identifier: MIT

#include "fe_games.h"
#include "fe_7z.h"
#include "fe_stella.h"

#include "OrbisPaths.h"
#include "ProsperoThread.h"
#include "netfs.h"

#include "third_party/unzip.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <set>
#include <unordered_map>

#define STELLAPS5_INCBIN(sym, path)                                                                                \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const char sym##_begin[];                                                                       \
	extern "C" const char sym##_end[];

STELLAPS5_INCBIN(stellaps5_gamedb, GAMEDB_TSV)

namespace fe
{
namespace
{
const SystemInfo kSystems[size_t(System::Count)] = {
	{"a26", "Atari 2600", "Atari_-_2600", "Atari2600", Family::A2600},
	{"a52", "Atari 5200", "Atari_-_5200", "Atari5200", Family::A5200},
	{"a78", "Atari 7800", "Atari_-_7800", "Atari7800", Family::A7800},
	{"lnx", "Atari Lynx", "Atari_-_Lynx", "AtariLynx", Family::Lynx},
};

struct ExtSys
{
	const char* ext;
	System sys;
};
// What the shelf lists: the cores' libretro extensions (Stella: a26 bin; a5200: a52 bin; ProSystem: a78 bin;
// Gearlynx: lnx lyx o bin) and .rom (Stella's launcher's). .bin and .rom are anyone's (SystemForExt).
const ExtSys kExts[] = {
	{".a26", System::A2600}, {".a52", System::A5200}, {".a78", System::A7800}, {".lnx", System::Lynx},
	{".lyx", System::Lynx}, {".o", System::Lynx}, {".bin", System::A2600}, {".rom", System::A2600},
};

bool AmbiguousExt(const std::string& e)
{
	return e == ".bin" || e == ".rom";
}

uint32_t Crc(const uint8_t* p, size_t n)
{
	return uint32_t(crc32(0L, p, uInt(n)));
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& name)
{
	const size_t dot = name.find_last_of('.');
	const size_t slash = name.find_last_of("/\\");
	if (dot == std::string::npos || dot == 0 || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(name.substr(dot));
}

// The systems searched together for a game of system s (each has its own table).
std::vector<System> Group(System s)
{
	return {s};
}

// "Super Mario World (USA) (Rev 1)" -> "super mario world"; also drops [tags] and trims.
std::string Key(const std::string& name)
{
	std::string out;
	int depth = 0;
	for (char c : name)
	{
		if (c == '(' || c == '[')
			depth++;
		else if ((c == ')' || c == ']') && depth > 0)
			depth--;
		else if (depth == 0)
			out += char(tolower(uint8_t(c)));
	}
	// collapse spaces, drop punctuation that file names often lose
	std::string k;
	bool space = false;
	for (char c : out)
	{
		if (c == '_' || c == ' ' || c == '.')
			space = !k.empty();
		else if (isalnum(uint8_t(c)) || c == '&' || c == '\'' || c == '-' || c == ',' || c == '!')
		{
			if (space)
				k += ' ';
			space = false;
			k += c;
		}
	}
	return k;
}

int RegionRank(const std::string& name)
{
	// prefer clean dumps: no Beta/Proto/Pirate/Virtual Console/etc.
	int penalty = 0;
	static const char* const bad[] = {"(Beta", "(Proto", "(Pirate", "(Virtual Console", "(Sample", "(Demo", "(Alt",
		"(Unl", "(Aftermarket", "(Arcade", "(Kiosk", "(Program", "(Hack", "(Switch Online", "(Collection"};
	for (const char* b : bad)
		if (name.find(b) != std::string::npos)
			penalty += 100;
	if (name.find("(Rev") != std::string::npos)
		penalty += 1;
	if (name.find("USA") != std::string::npos)
		return penalty + 0;
	if (name.find("World") != std::string::npos)
		return penalty + 2;
	if (name.find("Europe") != std::string::npos)
		return penalty + 4;
	if (name.find("Japan") != std::string::npos)
		return penalty + 6;
	return penalty + 8;
}

uint64_t CrcKey(System s, uint32_t crc)
{
	return (uint64_t(s) << 32) | crc;
}

std::string NameKey(System s, const std::string& k)
{
	return std::string(1, char('A' + int(s))) + k;
}

struct Db
{
	size_t entries = 0;
	std::unordered_map<uint64_t, std::string> by_crc;
	std::unordered_map<std::string, std::string> exact; // sys + lower-case name -> name
	std::unordered_map<std::string, std::string> loose; // sys + Key -> best name
	Db()
	{
		const char* p = stellaps5_gamedb_begin;
		const char* end = stellaps5_gamedb_end;
		while (p < end)
		{
			const char* nl = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
			if (!nl)
				nl = end;
			Line(std::string(p, size_t(nl - p)));
			p = nl + 1;
		}
	}
	void Line(const std::string& line)
	{
		if (line.empty() || line[0] == '#')
			return;
		const size_t t1 = line.find('\t');
		const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
		if (t2 == std::string::npos)
			return;
		const std::string id = line.substr(0, t1);
		const std::string crc = line.substr(t1 + 1, t2 - t1 - 1);
		const std::string name = line.substr(t2 + 1);
		System sys = System::Count;
		for (int i = 0; i < int(System::Count); i++)
			if (id == kSystems[i].id)
				sys = System(i);
		if (sys == System::Count || name.empty())
			return;
		entries++;
		if (crc != "-")
			by_crc.emplace(CrcKey(sys, uint32_t(strtoul(crc.c_str(), nullptr, 16))), name);
		exact.emplace(NameKey(sys, Lower(name)), name);
		const std::string k = NameKey(sys, Key(name));
		auto it = loose.find(k);
		if (it == loose.end() || RegionRank(name) < RegionRank(it->second))
			loose[k] = name;
	}
};

const Db& D()
{
	static const Db db;
	return db;
}

// ---- CRC cache: path \t size \t mtime \t whole \t body ----
// One per process: the scan and the pass that names network ROMs (NetNaming) share it, each under the lock.
struct CrcCache
{
	std::mutex lock;
	std::unordered_map<std::string, std::string> lines; // path -> "size\tmtime\twhole\tbody"
	std::unordered_map<std::string, bool> seen; // the paths this scan looked up
	bool dirty = false;
	bool loaded = false;
	std::string file;
	// A new scan: reads the file the first time, forgets which paths the last scan saw.
	void Begin()
	{
		std::lock_guard<std::mutex> g(lock);
		seen.clear();
		if (loaded)
			return;
		loaded = true;
		file = OrbisDir("covers") + "/crc-cache.txt";
		FILE* f = fopen(file.c_str(), "r");
		if (!f)
			return;
		char line[2048];
		while (fgets(line, sizeof(line), f))
		{
			line[strcspn(line, "\r\n")] = 0;
			char* tab = strchr(line, '\t');
			if (tab)
			{
				*tab = 0;
				lines[line] = tab + 1;
			}
		}
		fclose(f);
	}
	// prune: the scan's end drops the entries of files that are gone; the naming pass (mid-scan, maybe) keeps all.
	void Save(bool prune)
	{
		std::lock_guard<std::mutex> g(lock);
		// entries of files that are gone are dropped (a USB drive that isn't plugged in, or a share that is not
		// connected, keeps its own)
		for (auto it = lines.begin(); prune && it != lines.end();)
		{
			const std::string& key = it->first;
			// an archive's system is kept as "sys:<path>": the same rule for its path
			const char* file = key.c_str() + (key.rfind("sys:", 0) == 0 ? 4 : 0);
			const bool network = netfs_is_network_path(file);
			if (!seen.count(key) && !OrbisOnDrive(file) && !network)
			{
				it = lines.erase(it);
				dirty = true;
			}
			else
				++it;
		}
		if (!dirty)
			return;
		const std::string tmp = file + ".part";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f)
			return;
		for (const auto& kv : lines)
			fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
		bool ok = ferror(f) == 0;
		ok = fclose(f) == 0 && ok;
		if (!ok || rename(tmp.c_str(), file.c_str()) != 0)
		{
			remove(tmp.c_str());
			return;
		}
		dirty = false;
	}
	void Put(const std::string& path, const std::string& val)
	{
		std::lock_guard<std::mutex> g(lock);
		lines[path] = val;
		dirty = true;
	}
};

CrcCache& SharedCrcCache()
{
	static CrcCache* c = new CrcCache; // never freed: the naming pass may still run at exit
	return *c;
}

// Whether a file that could not be used (no ROM in a zip, a CRC that could not be taken) may be remembered as such.
// A local file: yes. A file on a share: only when it reads whole, without an error, from a share that is still
// connected. A read error (the share dropped, the watchdog cut a slow request, Settings > Network shares connected
// the shares again) is not the file's fault, and a remembered failure would hide the game, or keep it unnamed, until
// the file changes; it is tried again at the next visit of the shelf instead.
bool FailureIsTheFiles(const std::string& path, const struct stat& st, const std::atomic<bool>* cancel = nullptr)
{
	if (!netfs_is_network_path(path.c_str()))
		return true;
	if (st.st_size > 64LL * 1024 * 1024)
		return false; // too big to read through each time it fails
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	std::vector<char> buf(1 << 20);
	long long total = 0;
	bool ok = true;
	for (;;)
	{
		if (cancel && cancel->load())
		{
			ok = false;
			break;
		}
		const size_t got = fread(buf.data(), 1, buf.size(), f);
		total += (long long)got;
		if (got < buf.size())
			break;
	}
	ok = ok && ferror(f) == 0 && total == (long long)st.st_size;
	fclose(f);
	if (!ok)
		return false;
	for (const std::string& m : netfs_mount_points())
		if (path.size() > m.size() && path.compare(0, m.size(), m) == 0 && path[m.size()] == '/')
			return true;
	return false;
}

// read: false for a ROM on a share, where only a CRC already in the cache is used (reading every ROM over the
// network is what makes a scan slow). known (if given): the cache holds an answer for this file as it is now,
// "could not be read" included.
bool CachedCrc(CrcCache& cache, const std::string& path, uint32_t* whole, uint32_t* body, bool read = true, bool* known = nullptr,
	std::string* md5 = nullptr)
{
	if (known)
		*known = false;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[path] = true;
	}
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return false;
	char key[64];
	snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	{
		std::lock_guard<std::mutex> g(cache.lock);
		auto it = cache.lines.find(path);
		if (it != cache.lines.end() && it->second.compare(0, strlen(key), key) == 0)
		{
			if (known)
				*known = true;
			if (it->second.compare(strlen(key), std::string::npos, "fail") == 0)
				return false; // couldn't be read last time, and hasn't changed since
			unsigned a = 0, b = 0;
			char m[40] = "";
			const int got = sscanf(it->second.c_str() + strlen(key), "%x\t%x\t%32[0-9a-f]", &a, &b, m);
			if (got >= 2)
			{
				*whole = a;
				*body = b;
				if (md5)
					*md5 = got == 3 && strlen(m) == 32 ? m : "";
				return true;
			}
		}
	}
	if (!read)
		return false;
	char val[160];
	std::string sum;
	if (!RomCrc32(path, whole, body, nullptr, nullptr, nullptr, &sum))
	{
		// unreadable (or an empty zip): remembered, so the next scans don't read it again until it changes
		if (FailureIsTheFiles(path, st))
		{
			snprintf(val, sizeof(val), "%sfail", key);
			cache.Put(path, val);
		}
		return false;
	}
	snprintf(val, sizeof(val), "%s%08X\t%08X\t%s", key, *whole, *body, sum.c_str());
	cache.Put(path, val);
	if (md5)
		*md5 = sum;
	return true;
}

// The offset of the ROM data after a header we know, or 0: a 7800 game's .a78 header (128 bytes, "ATARI7800" from
// byte 1), a Lynx game's .lnx header (64 bytes, "LYNX"). No-Intro lists both with and without it.
size_t HeaderSize(const std::vector<uint8_t>& d, const std::string&)
{
	if (d.size() > 128 + 1024 && memcmp(d.data() + 1, "ATARI7800", 9) == 0)
		return 128;
	if (d.size() > 64 + 1024 && memcmp(d.data(), "LYNX", 4) == 0)
		return 64;
	return 0;
}

// An archive's system (the ROM inside it), from the cache while the archive hasn't changed: opening every zip at
// every start is slow on a USB drive (and on a share) with a big library. "sys:<path>" -> "size\tmtime\t<system>".
// read: false for an archive on a share (*known then says whether the cache had it; NetNaming opens it later).
bool CachedArchiveSystem(CrcCache& cache, const std::string& path, const struct stat& st, System* out, bool read = true,
	bool* known = nullptr, const std::atomic<bool>* cancel = nullptr)
{
	const std::string key = "sys:" + path;
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	if (known)
		*known = true;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
		{
			const std::string v = it->second.substr(strlen(stamp));
			if (v == "none")
				return false;
			const int n = atoi(v.c_str());
			if (n >= 0 && n < int(System::Count))
			{
				*out = System(n);
				return true;
			}
		}
	}
	if (!read)
	{
		if (known)
			*known = false;
		return false;
	}
	bool certain = false;
	const bool ok = ArchiveSystem(path, out, cancel, &certain);
	// "none" hides the archive from the shelf until it changes: only when it is really the archive's (a 7z's directory
	// says so, or FailureIsTheFiles)
	if (ok || certain || FailureIsTheFiles(path, st, cancel))
		cache.Put(key, std::string(stamp) + (ok ? std::to_string(int(*out)) : std::string("none")));
	return ok;
}

// The system an archive on a share most likely holds before NetNaming has opened it: its folders' names, else the 2600.
System GuessSystemFromFolder(const std::string& path)
{
	System s = System::A2600;
	SystemFromFolders(path, &s);
	return s;
}

void Walk(const std::string& dir, int depth, bool usb, bool net, std::vector<GameInfo>& out, CrcCache& cache)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
		{
			if (depth < 4)
				Walk(path, depth + 1, usb, net, out, cache);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue; // a FIFO or a device would block the CRC read (and the shelf) forever
		const std::string name = e->d_name;
		const std::string ext = ExtOf(name);
		if (ext.empty())
			continue;
		GameInfo g;
		if (IsArchiveExt(ext))
		{
			// a zip (or a 7z) on a share is not opened by the scan: the cache knows its system, or its folder's name says it
			// until NetNaming has looked inside
			bool known = true;
			if (!CachedArchiveSystem(cache, path, st, &g.system, !net, &known))
			{
				if (known)
					continue;
				g.system = GuessSystemFromFolder(path);
				g.crc_pending = true;
			}
			// the game inside may be a .bin (the archive's system then reads A2600): its folder, or its CRC, decides
			else if (g.system == System::A2600)
			{
				g.system_known = SystemFromFolders(path, &g.system);
				g.ambiguous = !g.system_known;
			}
		}
		else
		{
			bool ambiguous = false;
			if (!SystemForExt(ext, &g.system, &ambiguous))
				continue;
			// a .bin: the folder it is in says whose, if any does (else ScanGames asks the game tables)
			if (ambiguous && SystemFromFolders(path, &g.system))
				g.system_known = true;
			g.ambiguous = ambiguous && !g.system_known;
		}
		g.path = path;
		g.file_base = name.substr(0, name.size() - ext.size());
		g.ext = ext;
		g.on_usb = usb;
		g.on_network = net;
		out.push_back(std::move(g));
	}
	closedir(d);
}

template <typename F>
std::string Search(System s, System* found_in, F&& find)
{
	for (System g : Group(s))
	{
		std::string n = find(g);
		if (!n.empty())
		{
			if (found_in)
				*found_in = g;
			return n;
		}
	}
	return "";
}
} // namespace

const SystemInfo& Info(System s)
{
	return kSystems[size_t(s) < size_t(System::Count) ? size_t(s) : 0];
}

const char* FamilyName(Family f)
{
	if (f == Family::All)
		return "All games";
	for (const SystemInfo& s : kSystems)
		if (s.family == f)
			return s.name;
	return "?";
}

bool SystemForExt(const std::string& ext, System* out, bool* ambiguous)
{
	const std::string e = Lower(ext);
	if (ambiguous)
		*ambiguous = AmbiguousExt(e);
	for (const ExtSys& x : kExts)
		if (e == x.ext)
		{
			*out = x.sys;
			return true;
		}
	return false;
}

bool SystemFromFolders(const std::string& path, System* out)
{
	std::string dir = path.substr(0, path.find_last_of('/'));
	for (int depth = 0; depth < 3 && !dir.empty(); depth++)
	{
		std::string name = dir.substr(dir.find_last_of('/') + 1), key;
		for (char c : name)
			if (isalnum(uint8_t(c)))
				key += char(tolower(uint8_t(c)));
		if (key.find("5200") != std::string::npos)
			*out = System::A5200;
		else if (key.find("7800") != std::string::npos)
			*out = System::A7800;
		else if (key.find("lynx") != std::string::npos)
			*out = System::Lynx;
		else if (key.find("2600") != std::string::npos || key == "vcs")
			*out = System::A2600;
		else
		{
			dir = dir.substr(0, dir.find_last_of('/'));
			continue;
		}
		return true;
	}
	return false;
}

namespace
{
// Calls fn(name) for each file of the zip until it returns true; the zip is then positioned on that file.
template <typename F>
bool FindInZip(unzFile z, F&& fn)
{
	for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z))
	{
		unz_file_info info;
		char name[512];
		// minizip ends the name with '\0' only when it fits: keep the last byte for it, skip names that don't fit
		name[sizeof(name) - 1] = '\0';
		if (unzGetCurrentFileInfo(z, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK ||
			info.size_filename >= sizeof(name) - 1)
			continue;
		if (fn(std::string(name)))
			return true;
	}
	return false;
}
} // namespace

bool IsArchiveExt(const std::string& ext)
{
	const std::string e = Lower(ext);
	return e == ".zip" || e == ".7z";
}

namespace
{

// A file of an archive that is a game we list: an Atari 2600 game (.a26, .bin, .rom).
bool IsArchivedRom(const std::string& name, System* sys = nullptr)
{
	System s;
	if (!SystemForExt(ExtOf(name), &s))
		return false;
	if (sys)
		*sys = s;
	return true;
}

// A .7z's first game, in the archive's order (a zip's rule): its index, or -1.
int First7zRom(const sevenzip::Archive& a)
{
	const std::vector<sevenzip::Entry>& entries = a.Entries();
	for (size_t i = 0; i < entries.size(); i++)
		if (!entries[i].is_dir && IsArchivedRom(entries[i].name))
			return int(i);
	return -1;
}

// What went wrong with a .7z, for the player (English; T() translates it).
const char* SevenZipWhy(sevenzip::Status st)
{
	switch (st)
	{
		case sevenzip::Status::Unsupported:
			return "It is packed with a method Stella PS5 can't unpack (LZMA, LZMA2 and PPMd work), or its names are encrypted.";
		case sevenzip::Status::Encrypted: return "The archive is encrypted.";
		case sevenzip::Status::TooBig:
			return "The game, or the solid block it is packed in, is too big (2 MiB for a game, 64 MiB for a block).";
		case sevenzip::Status::ReadError: return "The file could not be read.";
		case sevenzip::Status::NoMemory: return "There is not enough memory to unpack it.";
		default: return "The archive is damaged, or it is not a 7z archive.";
	}
}

// A .7z that can't give a game for a reason of its own, read without an error: no game in its directory, encrypted
// names, a method the decoder doesn't have, too big. Such an answer may be remembered without reading the whole file
// again (FailureIsTheFiles); a read error, a cancel or a damaged-looking archive (which a cut read can make) may not.
bool CertainFailure(sevenzip::Status st)
{
	return st == sevenzip::Status::Encrypted || st == sevenzip::Status::Unsupported || st == sevenzip::Status::TooBig;
}

} // namespace

bool ArchiveSystem(const std::string& path, System* out, const std::atomic<bool>* cancel, bool* certain)
{
	if (certain)
		*certain = false;
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		const sevenzip::Status st = a.Open(path, cancel);
		if (st != sevenzip::Status::Ok)
		{
			if (certain)
				*certain = CertainFailure(st);
			return false;
		}
		const int i = First7zRom(a);
		if (i < 0 && certain)
			*certain = true; // its directory was read whole: no game in it
		return i >= 0 && IsArchivedRom(a.Entries()[size_t(i)].name, out);
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	const bool ok = FindInZip(z, [&](const std::string& name) { return IsArchivedRom(name, out); });
	unzClose(z);
	return ok;
}

bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, std::string* why,
	const std::atomic<bool>* cancel, bool* certain)
{
	if (certain)
		*certain = false;
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		sevenzip::Status st = a.Open(path, cancel);
		int i = -1;
		if (st == sevenzip::Status::Ok)
		{
			i = First7zRom(a);
			if (i < 0)
			{
				if (certain)
					*certain = true;
				return false; // no game inside: the caller says so
			}
			*inner = a.Entries()[size_t(i)].name;
			st = a.Extract(size_t(i), data, MaxRomSizeFor(*inner));
			if (st == sevenzip::Status::Ok && data->empty())
				st = sevenzip::Status::Broken;
		}
		if (st != sevenzip::Status::Ok)
		{
			OrbisLog("[games] %s%s%s: %s", path.c_str(), i >= 0 ? " -> " : "", i >= 0 ? inner->c_str() : "",
				sevenzip::StatusName(st));
			if (why)
				*why = SevenZipWhy(st);
			if (certain)
				*certain = CertainFailure(st);
			data->clear();
			return false;
		}
		return true;
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
	{
		if (why)
			*why = "The file could not be read."; // a share that dropped, or not a zip at all
		return false;
	}
	bool ok = FindInZip(z, [&](const std::string& name) {
		if (!IsArchivedRom(name))
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= MaxRomSizeFor(*inner) && unzOpenCurrentFile(z) == UNZ_OK;
		if (ok)
		{
			data->resize(info.uncompressed_size);
			ok = unzReadCurrentFile(z, data->data(), unsigned(data->size())) == int(data->size());
			unzCloseCurrentFile(z);
		}
		if (!ok)
		{
			data->clear();
			if (why)
				*why = "The file could not be read."; // the game is there, its data didn't come (or is damaged)
		}
	}
	unzClose(z);
	return ok;
}

namespace gamedb
{
size_t Count()
{
	return D().entries;
}

std::string ByCrc(System s, uint32_t crc, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().by_crc.find(CrcKey(g, crc));
		return it == D().by_crc.end() ? std::string() : it->second;
	});
}

std::string Exact(System s, const std::string& name, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().exact.find(NameKey(g, Lower(name)));
		return it == D().exact.end() ? std::string() : it->second;
	});
}

std::string Loose(System s, const std::string& file_base, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().loose.find(NameKey(g, Key(file_base)));
		return it == D().loose.end() ? std::string() : it->second;
	});
}

std::string Title(const std::string& nointro)
{
	std::string t = nointro.substr(0, nointro.find(" ("));
	// "Legend of Zelda, The - A Link to the Past" -> "The Legend of Zelda - A Link to the Past"
	static const char* const arts[] = {", The", ", A", ", An"};
	for (const char* a : arts)
	{
		const size_t pos = t.find(a);
		if (pos != std::string::npos)
		{
			const size_t end = pos + strlen(a);
			if (end == t.size() || t.compare(end, 3, " - ") == 0 || t[end] == ':')
			{
				const std::string art = std::string(a + 2);
				t = art + " " + t.substr(0, pos) + t.substr(end);
				break;
			}
		}
	}
	return t;
}

std::string Region(const std::string& nointro)
{
	const size_t a = nointro.find(" (");
	if (a == std::string::npos)
		return "";
	const size_t b = nointro.find(')', a);
	return b == std::string::npos ? "" : nointro.substr(a + 2, b - a - 2);
}
} // namespace gamedb

bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext, const std::atomic<bool>* cancel,
	bool* certain, std::string* md5)
{
	if (certain)
		*certain = false;
	auto stopped = [cancel] { return cancel && cancel->load(); };
	std::string ext = ExtOf(path);
	std::vector<uint8_t> data;
	System sys;
	if (IsArchiveExt(ext))
	{
		// the game is unpacked (2600 games are a few KiB): its MD5, Stella's database key, isn't in the archive
		std::string inner;
		if (!ReadFromArchive(path, &inner, &data, nullptr, cancel, certain))
			return false;
		ext = ExtOf(inner);
	}
	else if (!SystemForExt(ext, &sys))
		return false;
	else
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size <= 0 || uint64_t(size) > MaxRomSizeFor(path))
		{
			fclose(f);
			return false;
		}
		data.resize(size_t(size));
		// in pieces, so a cancel (NetNaming::Stop) does not wait for a whole ROM to come over the network
		size_t n = 0;
		while (n < data.size() && !stopped())
		{
			const size_t got = fread(data.data() + n, 1, std::min<size_t>(1 << 20, data.size() - n), f);
			if (got == 0)
				break;
			n += got;
		}
		fclose(f);
		if (n != data.size())
			return false;
	}
	if (inner_ext)
		*inner_ext = ext;
	*whole = Crc(data.data(), data.size());
	const size_t skip = HeaderSize(data, ext);
	*body = skip > 0 && skip < data.size() ? Crc(data.data() + skip, data.size() - skip) : *whole;
	if (md5)
		*md5 = vcs::Md5(data.data(), data.size());
	return true;
}

uint64_t MaxRomSize(System s)
{
	switch (s)
	{
		case System::A2600: return 512u * 1024;
		case System::A5200: return 128u * 1024;
		default: return 2u * 1024 * 1024 + 1024;
	}
}

uint64_t MaxRomSizeFor(const std::string& name)
{
	System s;
	bool ambiguous = false;
	if (!SystemForExt(ExtOf(name), &s, &ambiguous) || ambiguous)
		return kMaxRomAny;
	return MaxRomSize(s);
}

System SystemOfRom(const std::string& path, const std::string& name, const std::vector<uint8_t>& data)
{
	System sys = System::A2600;
	bool ambiguous = false;
	if (!SystemForExt(ExtOf(name), &sys, &ambiguous))
		return System::A2600;
	if (!ambiguous || SystemFromFolders(path, &sys))
		return sys;
	const uint32_t whole = Crc(data.data(), data.size());
	const size_t skip = HeaderSize(data, ExtOf(name));
	const uint32_t body = skip ? Crc(data.data() + skip, data.size() - skip) : whole;
	for (int k = 0; k < int(System::Count); k++)
	{
		System found = System(k);
		if (!gamedb::ByCrc(System(k), whole, &found).empty() || !gamedb::ByCrc(System(k), body, &found).empty())
			return found;
	}
	return System::A2600;
}

void PrepareFolders()
{
	const std::string roms = OrbisDir("roms"), covers = OrbisDir("covers");
	for (const SystemInfo& s : kSystems)
	{
		OrbisMkdirs(roms + "/" + s.folder);
		OrbisMkdirs(covers + "/" + s.folder);
	}
}

// ---- the naming pass for ROMs on shares ----

struct NetNaming::Impl
{
	std::mutex lock;
	std::deque<std::string> todo;
	std::set<std::string> tried; // every path queued: a ROM is read once per run (one whose share went away is let go)
	std::atomic<bool> stop{false};
	std::atomic<int> resolved{0};
	std::atomic<bool> busy{false};
	ps5::BigThread thread;

	void Run()
	{
		CrcCache& cache = SharedCrcCache();
		int named = 0, since_save = 0;
		for (;;)
		{
			std::string path;
			{
				std::unique_lock<std::mutex> g(lock);
				if (stop.load() || todo.empty() || netfs_realtime())
				{
					busy = false;
					break;
				}
				path = todo.front();
				todo.pop_front();
			}
			struct stat st = {};
			if (stat(path.c_str(), &st) != 0)
			{
				// the share went away: the next scan lists it again once it is back, and Add queues it then
				std::lock_guard<std::mutex> g(lock);
				tried.erase(path);
				continue;
			}
			char key[64];
			snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
			if (IsArchiveExt(ExtOf(path)))
			{
				System sys;
				// the system of the game inside, for the next scan
				if (!CachedArchiveSystem(cache, path, st, &sys, true, nullptr, &stop) && !stop.load())
				{
					// an archive known to hold no game (nothing we list, encrypted names): no CRC to take, and the
					// whole file is not read again to prove it
					bool none = false;
					{
						std::lock_guard<std::mutex> g(cache.lock);
						auto it = cache.lines.find("sys:" + path);
						none = it != cache.lines.end() && it->second == std::string(key) + "none";
					}
					if (none)
					{
						cache.Put(path, std::string(key) + "fail");
						named++;
						resolved++;
						continue;
					}
				}
			}
			uint32_t whole = 0, body = 0;
			bool certain = false;
			std::string sum;
			const bool ok = RomCrc32(path, &whole, &body, nullptr, &stop, &certain, &sum);
			if (stop.load())
			{
				// cancelled half way: nothing recorded, and it is the first to be read when the pass starts again
				std::lock_guard<std::mutex> g(lock);
				todo.push_front(path);
				break;
			}
			char val[160];
			if (ok)
				snprintf(val, sizeof(val), "%s%08X\t%08X\t%s", key, whole, body, sum.c_str());
			else
				snprintf(val, sizeof(val), "%sfail", key);
			// a read error is not remembered as "fail" (FailureIsTheFiles): the next visit of the shelf tries it again
			// (not this one: it stays in `tried`, or every scan the pass's results bring would read it once more)
			if (ok || certain || FailureIsTheFiles(path, st, &stop))
				cache.Put(path, val);
			else if (stop.load())
			{
				std::lock_guard<std::mutex> g(lock);
				todo.push_front(path); // cancelled while it was being checked: the same as above
				break;
			}
			named++;
			resolved++;
			if (++since_save >= 20)
			{
				cache.Save(false);
				since_save = 0;
			}
		}
		cache.Save(false);
		busy = false;
		OrbisLog("[games] network CRC pass: %d ROM(s) read%s", named, stop.load() ? " (stopped)" : "");
	}
};

NetNaming::NetNaming() : m_i(new Impl) {}

NetNaming::~NetNaming()
{
	Stop();
}

void NetNaming::Add(const std::vector<GameInfo>& games)
{
	size_t added = 0, queued = 0;
	{
		std::lock_guard<std::mutex> g(m_i->lock);
		for (const GameInfo& game : games)
			if (game.crc_pending && m_i->tried.insert(game.path).second)
			{
				m_i->todo.push_back(game.path);
				added++;
			}
		queued = m_i->todo.size();
	}
	if (added)
		OrbisLog("[games] naming %zu network ROM(s) by CRC in the background", added);
	// nothing new may still leave work: what a Stop() cut short, or what was queued when a game started (netfs_realtime)
	if (!queued)
		return;
	if (m_i->busy.load())
		return; // the running pass takes the new ones too
	m_i->thread.join();
	m_i->stop = false;
	m_i->busy = true;
	m_i->thread = ps5::BigThread([this] { m_i->Run(); }, 256 * 1024);
}

void NetNaming::Stop()
{
	m_i->stop = true;
	m_i->thread.join();
	m_i->busy = false;
}

int NetNaming::Resolved() const
{
	return m_i->resolved.load();
}

bool NetNaming::Busy() const
{
	return m_i->busy.load();
}

std::vector<std::string> RomRoots()
{
	std::vector<std::string> roots = OrbisRomRoots();
	for (const std::string& r : netfs_mount_points())
		roots.push_back(r);
	return roots;
}

std::vector<GameInfo> ScanGames()
{
	PrepareFolders();
	std::vector<GameInfo> games;
	CrcCache& cache = SharedCrcCache();
	cache.Begin();
	for (const std::string& root : RomRoots())
		Walk(root, 0, OrbisOnDrive(root), netfs_is_network_path(root.c_str()), games, cache);

	for (GameInfo& g : games)
	{
		System found = g.system;
		g.nointro = gamedb::Exact(g.system, g.file_base, &found);
		// the ROM's CRC (No-Intro's names) and MD5 (Stella's database): read once, then from the cache
		uint32_t whole = 0, body = 0;
		bool known = false;
		const bool have = CachedCrc(cache, g.path, &whole, &body, !g.on_network, &known, &g.md5);
		// a game on a share is named from its file name for now: NetNaming reads its CRC and MD5
		if (g.on_network && !known)
			g.crc_pending = true;
		if (have && g.nointro.empty())
		{
			// a .bin no folder speaks for: the system whose table knows its CRC (the 2600's first)
			for (int k = 0; k < int(System::Count) && g.nointro.empty(); k++)
			{
				const System s = g.ambiguous ? System(k) : g.system;
				g.nointro = gamedb::ByCrc(s, whole, &found);
				if (g.nointro.empty() && body != whole)
					g.nointro = gamedb::ByCrc(s, body, &found);
				if (!g.ambiguous)
					break;
			}
			g.name_by_crc = !g.nointro.empty();
			if (!g.nointro.empty())
				g.system = found;
		}
		vcs::Props props;
		if (g.system == System::A2600 && !g.md5.empty() && vcs::LookupProps(g.md5, &props))
			g.stella = props.name;
		if (g.nointro.empty())
			g.nointro = gamedb::Loose(g.system, g.file_base, &found);
		if (!g.nointro.empty())
			g.system = found;
		// the title: Stella's name for it, else No-Intro's, else the file's
		g.title = !g.stella.empty() ? gamedb::Title(g.stella) : !g.nointro.empty() ? gamedb::Title(g.nointro) : g.file_base;
		g.region = gamedb::Region(g.nointro);
	}
	cache.Save(true);

	std::sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) {
		const int c = strcasecmp(a.title.c_str(), b.title.c_str());
		if (c != 0)
			return c < 0;
		if (a.system != b.system)
			return a.system < b.system;
		return a.path < b.path;
	});
	OrbisLog("[games] %zu ROM(s); table of %zu names", games.size(), gamedb::Count());
	// each game once per scan is a lot of boot.log for a big library: the first 200
	constexpr size_t kLogged = 200;
	for (size_t i = 0; i < games.size() && i < kLogged; i++)
	{
		const GameInfo& g = games[i];
		OrbisLog("[games]   %s [%s] -> \"%s\"%s, Stella: \"%s\"", g.path.c_str(), Info(g.system).id, g.nointro.c_str(),
			g.name_by_crc ? " (by CRC)" : "", g.stella.c_str());
	}
	if (games.size() > kLogged)
		OrbisLog("[games]   ... and %zu more", games.size() - kLogged);
	return games;
}
} // namespace fe
