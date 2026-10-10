// See TldAudio.h. Paths are UTF-8 internally; on Windows they are opened as wide paths.
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>  // before ww2ogg's headers, which say "using namespace std"
#endif
#include "TldAudio.h"
#include "Sheets.gen.h"
#include "../../third_party/ww2ogg/ww_path.h"
#include "../../third_party/ww2ogg/wwriff.h"
#include "../../third_party/ww2ogg/errors.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <regex>
#include <set>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

extern "C" int stb_vorbis_decode_memory(const unsigned char* mem, int len, int* channels, int* sample_rate, short** output);

namespace TldAudio {
namespace {

// ---- small file helpers ----------------------------------------------------------------
std::string Narrow(const std::wstring& w) {
#ifdef _WIN32
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(n ? n - 1 : 0, '\0');
	if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
	return s;
#else
	return std::string(w.begin(), w.end());
#endif
}
std::wstring Wide(const std::string& s) {
#ifdef _WIN32
	return ww_path(s);
#else
	return std::wstring(s.begin(), s.end());
#endif
}
std::string Lower(std::string s) { for (auto& c : s) c = (char)tolower((unsigned char)c); return s; }
std::string Join(const std::string& a, const std::string& b) { return a.empty() ? b : (a.back() == '/' || a.back() == '\\' ? a + b : a + "/" + b); }
std::string BaseName(const std::string& p) { size_t i = p.find_last_of("/\\"); return i == std::string::npos ? p : p.substr(i + 1); }

struct Entry { std::string path; bool dir; uint64_t size; };
std::vector<Entry> ListDir(const std::string& dir) {
	std::vector<Entry> out;
#ifdef _WIN32
	WIN32_FIND_DATAW fd;
	HANDLE h = FindFirstFileW((Wide(dir) + L"\\*").c_str(), &fd);
	if (h == INVALID_HANDLE_VALUE) return out;
	do {
		std::wstring n = fd.cFileName;
		if (n == L"." || n == L"..") continue;
		out.push_back({Join(dir, Narrow(n)), (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0,
			((uint64_t)fd.nFileSizeHigh << 32) | fd.nFileSizeLow});
	} while (FindNextFileW(h, &fd));
	FindClose(h);
#else
	DIR* d = opendir(dir.c_str());
	if (!d) return out;
	while (dirent* e = readdir(d)) {
		std::string n = e->d_name;
		if (n == "." || n == "..") continue;
		struct stat st; std::string p = Join(dir, n);
		if (stat(p.c_str(), &st) == 0) out.push_back({p, S_ISDIR(st.st_mode), (uint64_t)st.st_size});
	}
	closedir(d);
#endif
	return out;
}
void Walk(const std::string& dir, int depth, std::vector<Entry>& files) {
	if (depth < 0) return;
	for (auto& e : ListDir(dir)) {
		if (e.dir) Walk(e.path, depth - 1, files); else files.push_back(e);
	}
}
bool ReadRange(const std::string& path, uint64_t off, uint64_t size, std::vector<uint8_t>& out) {
	std::ifstream f(ww_path(path).c_str(), std::ios::binary);
	if (!f) return false;
	f.seekg((std::streamoff)off);
	out.resize((size_t)size);
	f.read((char*)out.data(), (std::streamsize)size);
	return (uint64_t)f.gcount() == size;
}
bool ReadAll(const std::string& path, std::vector<uint8_t>& out) {
	std::ifstream f(ww_path(path).c_str(), std::ios::binary | std::ios::ate);
	if (!f) return false;
	auto n = (uint64_t)f.tellg();
	f.close();
	return ReadRange(path, 0, n, out);
}
uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

// ---- media index ---------------------------------------------------------------------------
struct Media {
	uint64_t id = 0;
	std::string name;     // lowercase short name from SoundbanksInfo, or the file name
	std::string file;     // file holding the bytes
	uint64_t offset = 0, size = 0;
	uint16_t codec = 0;
	uint32_t channels = 0, sampleRate = 0;
	float seconds = 0;
	std::string where;    // human-readable origin
};

// Fills codec/length from the RIFF header at the start of a .wem
void ProbeRiff(Media& m) {
	std::vector<uint8_t> head;
	if (!ReadRange(m.file, m.offset, std::min<uint64_t>(m.size, 4096), head) || head.size() < 12) return;
	if (memcmp(head.data(), "RIFF", 4) || memcmp(head.data() + 8, "WAVE", 4)) return;
	uint32_t avg = 0, dataSize = 0, sampleCount = 0;
	for (size_t p = 12; p + 8 <= head.size();) {
		uint32_t sz = U32(&head[p + 4]);
		if (!memcmp(&head[p], "fmt ", 4) && p + 24 <= head.size()) {
			m.codec = U16(&head[p + 8]);
			m.channels = U16(&head[p + 10]);
			m.sampleRate = U32(&head[p + 12]);
			avg = U32(&head[p + 16]);
			// Wwise Vorbis keeps the sample count in the extended fmt / vorb data
			if (m.codec == 0xFFFF && sz >= 0x18 + 4 && p + 8 + 0x18 + 4 <= head.size()) sampleCount = U32(&head[p + 8 + 0x18]);
		}
		else if (!memcmp(&head[p], "vorb", 4) && p + 12 <= head.size()) sampleCount = U32(&head[p + 8]);
		else if (!memcmp(&head[p], "data", 4)) { dataSize = sz; break; }
		p += 8 + sz + (sz & 1);
	}
	if (sampleCount && m.sampleRate) m.seconds = (float)sampleCount / m.sampleRate;
	else if (avg) m.seconds = (float)(dataSize ? dataSize : m.size) / avg;
}

// Embedded media in a soundbank: DIDX lists (id, offset, size) inside DATA.
void IndexBank(const std::string& file, uint64_t base, uint64_t size, std::vector<Media>& out, const std::string& where) {
	std::vector<uint8_t> b;
	if (!ReadRange(file, base, size, b)) return;
	const uint8_t* didx = nullptr; uint32_t didxSize = 0; uint64_t dataOff = 0;
	for (size_t p = 0; p + 8 <= b.size();) {
		uint32_t sz = U32(&b[p + 4]);
		if (!memcmp(&b[p], "DIDX", 4)) { didx = &b[p + 8]; didxSize = sz; }
		else if (!memcmp(&b[p], "DATA", 4)) dataOff = p + 8;
		if (p + 8 + (uint64_t)sz > b.size()) break;
		p += 8 + sz;
	}
	if (!didx || !dataOff) return;
	for (uint32_t i = 0; i + 12 <= didxSize; i += 12) {
		Media m;
		m.id = U32(didx + i);
		m.file = file;
		m.offset = base + dataOff + U32(didx + i + 4);
		m.size = U32(didx + i + 8);
		m.where = where + " (embedded " + std::to_string(m.id) + ")";
		out.push_back(m);
	}
}

// Wwise file package (.pck, "AKPK"): sound banks and streamed media in one file.
void IndexPackage(const std::string& file, std::vector<Media>& out) {
	std::vector<uint8_t> h;
	if (!ReadRange(file, 0, 8, h) || memcmp(h.data(), "AKPK", 4)) return;
	uint32_t headerSize = U32(&h[4]);
	if (headerSize > (64u << 20)) return;
	std::vector<uint8_t> hd;
	if (!ReadRange(file, 8, headerSize, hd) || hd.size() < 16) return;
	uint32_t langSize = U32(&hd[4]), banksSize = U32(&hd[8]), streamsSize = U32(&hd[12]);
	// Header words: version, language map size, bank table size, stream table size, and in newer
	// packages an externals table size. Pick the layout whose sizes add up to the header size.
	uint32_t externalsSize = 0, words = 4;
	if (hd.size() >= 20 && 20ull + langSize + banksSize + streamsSize + U32(&hd[16]) == headerSize) { externalsSize = U32(&hd[16]); words = 5; }
	size_t p = words * 4 + langSize;
	auto table = [&](size_t start, uint32_t bytes, bool bank, bool id64) {
		if (bytes < 4 || start + bytes > hd.size()) return;
		uint32_t n = U32(&hd[start]);
		size_t es = id64 ? 24 : 20;
		if (bytes < 4 + (uint64_t)n * es) es = 20, id64 = false;
		for (uint32_t i = 0; i < n && start + 4 + (i + 1) * es <= hd.size(); ++i) {
			const uint8_t* e = &hd[start + 4 + i * es];
			uint64_t id = id64 ? (U32(e) | ((uint64_t)U32(e + 4) << 32)) : U32(e);
			const uint8_t* r = e + (id64 ? 8 : 4);
			uint32_t block = U32(r), fsize = U32(r + 4), startBlock = U32(r + 8);
			uint64_t off = (uint64_t)startBlock * (block ? block : 1);
			std::string where = BaseName(file) + "#" + std::to_string(id);
			if (bank) IndexBank(file, off, fsize, out, where);
			else {
				Media m; m.id = id; m.file = file; m.offset = off; m.size = fsize; m.where = where;
				out.push_back(m);
			}
		}
	};
	table(p, banksSize, true, false); p += banksSize;
	table(p, streamsSize, false, false); p += streamsSize;
	if (externalsSize) table(p, externalsSize, false, true);
}

// SoundbanksInfo.xml / .json: media id -> short name
void ReadNames(const std::string& file, std::map<uint64_t, std::string>& names) {
	std::vector<uint8_t> raw;
	if (!ReadAll(file, raw)) return;
	std::string s(raw.begin(), raw.end());
	std::smatch m;
	if (Lower(file).find(".json") != std::string::npos) {
		static const std::regex re("\\{[^{}]*?\"Id\"\\s*:\\s*\"?(\\d+)\"?[^{}]*?\"(?:ShortName|Path)\"\\s*:\\s*\"([^\"]+)\"[^{}]*\\}");
		for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it)
			names.emplace(std::stoull((*it)[1]), Lower((*it)[2]));
	}
	else {
		static const std::regex re("<File\\s+Id=\"(\\d+)\"[^>]*>[\\s\\S]*?<ShortName>([^<]+)</ShortName>");
		for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it)
			names.emplace(std::stoull((*it)[1]), Lower((*it)[2]));
	}
}

// ---- conversion ----------------------------------------------------------------------------
bool WriteWav(const std::string& path, const short* pcm, size_t frames, int channels, int rate) {
	std::ofstream f(ww_path(path).c_str(), std::ios::binary);
	if (!f) return false;
	uint32_t data = (uint32_t)(frames * channels * 2);
	auto w32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
	auto w16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
	f.write("RIFF", 4); w32(36 + data); f.write("WAVEfmt ", 8); w32(16); w16(1); w16((uint16_t)channels);
	w32((uint32_t)rate); w32((uint32_t)(rate * channels * 2)); w16((uint16_t)(channels * 2)); w16(16);
	f.write("data", 4); w32(data); f.write((const char*)pcm, data);
	return (bool)f;
}

bool Convert(const Media& m, const std::string& cacheDir, const std::string& codebooks, float maxSeconds,
             const std::string& outWav, std::string& err) {
	std::vector<uint8_t> bytes;
	if (!ReadRange(m.file, m.offset, m.size, bytes)) { err = "read failed"; return false; }
	if (m.codec == 0xFFFE || m.codec == 0x0001) {  // plain PCM16 inside RIFF
		for (size_t p = 12; p + 8 <= bytes.size();) {
			uint32_t sz = U32(&bytes[p + 4]);
			if (!memcmp(&bytes[p], "data", 4)) {
				size_t frames = std::min<size_t>(sz, bytes.size() - p - 8) / (2 * std::max(1u, m.channels));
				frames = std::min<size_t>(frames, (size_t)(maxSeconds * m.sampleRate));
				return WriteWav(outWav, (const short*)&bytes[p + 8], frames, m.channels, m.sampleRate) || (err = "write failed", false);
			}
			p += 8 + sz + (sz & 1);
		}
		err = "no data chunk"; return false;
	}
	if (m.codec != 0xFFFF) { char b[64]; snprintf(b, sizeof b, "codec 0x%04X not supported", m.codec); err = b; return false; }
	std::string tmpWem = Join(cacheDir, "tmp.wem"), tmpOgg = Join(cacheDir, "tmp.ogg");
	{ std::ofstream f(ww_path(tmpWem).c_str(), std::ios::binary); f.write((const char*)bytes.data(), bytes.size()); }
	try {
		Wwise_RIFF_Vorbis ww(tmpWem, codebooks, false, false, kNoForcePacketFormat);
		std::ofstream of(ww_path(tmpOgg).c_str(), std::ios::binary);
		ww.generate_ogg(of);
	}
	catch (const Parse_error& pe) { std::ostringstream o; o << pe; err = "ww2ogg: " + o.str(); return false; }
	catch (const File_open_error& fe) { std::ostringstream o; o << fe; err = "ww2ogg: " + o.str(); return false; }
	catch (...) { err = "ww2ogg failed"; return false; }
	std::vector<uint8_t> ogg;
	if (!ReadAll(tmpOgg, ogg)) { err = "no ogg"; return false; }
	int ch = 0, rate = 0; short* pcm = nullptr;
	int frames = stb_vorbis_decode_memory(ogg.data(), (int)ogg.size(), &ch, &rate, &pcm);
	if (frames <= 0 || !pcm) { err = "vorbis decode failed"; return false; }
	size_t keep = std::min<size_t>((size_t)frames, (size_t)(maxSeconds * rate));
	bool ok = WriteWav(outWav, pcm, keep, ch, rate);
	free(pcm);
	if (!ok) err = "write failed";
	return ok;
}

bool Fits(const Media& m, const char* rule) {
	std::string r = rule;
	if (r == "short") return m.seconds >= 1 && m.seconds <= 8;
	if (r == "medium_long") return m.seconds > 8 && m.seconds <= 60;
	if (r == "long") return m.seconds > 20 && m.seconds <= 120;
	if (r == "longest") return m.seconds > 30;
	return false;
}
uint32_t Hash(const char* s) { uint32_t h = 2166136261u; while (*s) { h ^= (uint8_t)*s++; h *= 16777619u; } return h; }

}  // namespace

std::vector<Result> PrepareCache(const std::wstring& tldFolderW, const std::wstring& cacheDirW,
                                 const std::wstring& codebooksW,
                                 const std::function<void(const std::string&)>& log) {
	const std::string tld = Narrow(tldFolderW), cache = Narrow(cacheDirW), codebooks = Narrow(codebooksW);
	const int N = Sheets::Count(Sheets::kContent);
	std::vector<Result> results(N);
	for (int i = 0; i < N; ++i) results[i].contentIndex = i;

	// Locate the audio under <TLD>/<something>_Data/StreamingAssets
	std::vector<Entry> files;
	for (auto& e : ListDir(tld)) {
		if (!e.dir) continue;
		std::string sa = Join(e.path, "StreamingAssets");
		if (!ListDir(sa).empty()) Walk(sa, 6, files);
	}
	log("found " + std::to_string(files.size()) + " files under StreamingAssets in " + tld);

	// Stamp: reuse the cache when the same files are there
	uint64_t stamp = 1469598103934665603ull;
	for (auto& f : files) {
		std::string l = Lower(f.path);
		if (l.find(".wem") == std::string::npos && l.find(".bnk") == std::string::npos && l.find(".pck") == std::string::npos) continue;
		for (char c : l) stamp = (stamp ^ (uint8_t)c) * 1099511628211ull;
		stamp = (stamp ^ f.size) * 1099511628211ull;
	}
	const std::string manifest = Join(cache, "manifest.txt");
	{
		std::ifstream mf(ww_path(manifest).c_str());
		std::string line;
		if (mf && std::getline(mf, line) && line == "stamp=" + std::to_string(stamp)) {
			while (std::getline(mf, line)) {
				size_t eq = line.find('='), bar = line.find('|');
				if (eq == std::string::npos) continue;
				std::string id = line.substr(0, eq), wav = line.substr(eq + 1, bar == std::string::npos ? std::string::npos : bar - eq - 1);
				for (int i = 0; i < N; ++i) if (id == Sheets::kContent[i].id && !wav.empty()) {
					results[i].wavPath = Wide(Join(cache, wav));
					results[i].source = bar == std::string::npos ? "" : line.substr(bar + 1);
				}
			}
			log("cache is current, reusing it");
			return results;
		}
	}

	std::map<uint64_t, std::string> names;
	std::vector<Media> media;
	for (auto& f : files) {
		std::string l = Lower(BaseName(f.path));
		if (l.rfind("soundbanksinfo", 0) == 0 && (l.find(".xml") != std::string::npos || l.find(".json") != std::string::npos)) ReadNames(f.path, names);
		else if (l.size() > 4 && l.substr(l.size() - 4) == ".pck") IndexPackage(f.path, media);
		else if (l.size() > 4 && l.substr(l.size() - 4) == ".bnk") IndexBank(f.path, 0, f.size, media, BaseName(f.path));
		else if (l.size() > 4 && l.substr(l.size() - 4) == ".wem") {
			Media m; m.file = f.path; m.size = f.size; m.where = BaseName(f.path);
			std::string stem = l.substr(0, l.size() - 4);
			if (!stem.empty() && std::all_of(stem.begin(), stem.end(), ::isdigit)) m.id = std::stoull(stem);
			else m.name = stem;
			media.push_back(m);
		}
	}
	log("indexed " + std::to_string(media.size()) + " media clips, " + std::to_string(names.size()) + " named in SoundbanksInfo");
	for (auto& m : media) {
		if (m.name.empty()) { auto it = names.find(m.id); if (it != names.end()) m.name = it->second; }
		ProbeRiff(m);
	}
	// Keep one entry per id (streamed copies beat prefetch stubs: larger wins)
	std::map<uint64_t, Media> byId; std::vector<Media> unnamedNoId;
	for (auto& m : media) {
		if (!m.codec) continue;
		if (!m.id) { unnamedNoId.push_back(m); continue; }
		auto it = byId.find(m.id);
		if (it == byId.end() || it->second.size < m.size) byId[m.id] = m;
	}
	std::vector<Media> pool;
	for (auto& kv : byId) pool.push_back(kv.second);
	pool.insert(pool.end(), unnamedNoId.begin(), unnamedNoId.end());
	std::sort(pool.begin(), pool.end(), [](const Media& a, const Media& b) { return a.seconds > b.seconds; });

	std::ofstream mf(ww_path(manifest).c_str());
	mf << "stamp=" << stamp << "\n";
	std::set<std::string> used;
	for (int i = 0; i < N; ++i) {
		const auto& c = Sheets::kContent[i];
		const float maxSec = c.role == Sheets::kRoleMusic ? 240.f : (c.role == Sheets::kRoleAmbience ? 90.f : 12.f);
		std::vector<const Media*> cands;
		// 1) named matches, in keyword priority order
		for (int k = 0; c.keywords[k] && cands.empty(); ++k) {
			for (auto& m : pool) {
				if (m.name.empty() || m.name.find(c.keywords[k]) == std::string::npos || used.count(m.where)) continue;
				bool bad = false;
				for (int x = 0; c.exclude[x]; ++x) if (m.name.find(c.exclude[x]) != std::string::npos) bad = true;
				if (c.role != Sheets::kRoleOneshot && m.seconds < 3) bad = true;
				if (!bad) cands.push_back(&m);
			}
		}
		bool named = !cands.empty();
		// 2) fallback by length (tld_content.json fallbacks)
		if (!named) {
			std::vector<const Media*> fit;
			for (auto& m : pool) {
				if (!Fits(m, c.fallback) || used.count(m.where)) continue;
				bool bad = false;  // a named clip still has to pass the row's exclude list
				for (int x = 0; c.exclude[x]; ++x) if (!m.name.empty() && m.name.find(c.exclude[x]) != std::string::npos) bad = true;
				if (!bad) fit.push_back(&m);
			}
			if (std::string(c.fallback) == "longest" && fit.size() > 5) fit.resize(5);
			if (!fit.empty()) cands.push_back(fit[Hash(c.id) % fit.size()]);
			for (auto* m : fit) if (m != cands.front()) cands.push_back(m);
		}
		bool done = false;
		for (size_t t = 0; t < cands.size() && t < 6 && !done; ++t) {
			const Media& m = *cands[t];
			std::string wav = std::string(c.id) + ".wav", err;
			if (Convert(m, cache, codebooks, maxSec, Join(cache, wav), err)) {
				used.insert(m.where);
				results[i].wavPath = Wide(Join(cache, wav));
				results[i].source = m.where + (m.name.empty() ? "" : " \"" + m.name + "\"") + (named ? " [name match]" : " [length match]");
				mf << c.id << "=" << wav << "|" << results[i].source << "\n";
				log(std::string(c.id) + " <- " + results[i].source + ", " + std::to_string((int)m.seconds) + " s");
				done = true;
			}
			else log(std::string(c.id) + ": skipped " + m.where + " (" + err + ")");
		}
		if (!done) log(std::string(c.id) + ": no usable clip found");
	}
	return results;
}

}  // namespace TldAudio
