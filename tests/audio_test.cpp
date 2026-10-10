// Builds a mock Long Dark StreamingAssets tree (PCM media, an AKPK package with a bank, SoundbanksInfo)
// and checks that each tld_content row resolves to the right clip and a valid WAV.
#include "../src/launcher/TldAudio.h"
#include "Sheets.gen.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include <cstdint>
#include <sys/stat.h>
static void put32(std::string& s, uint32_t v) { s.append((const char*)&v, 4); }
static void put16(std::string& s, uint16_t v) { s.append((const char*)&v, 2); }
static std::string wemPCM(float seconds, int rate = 8000) {
	std::string d; size_t frames = (size_t)(seconds * rate);
	std::string pcm(frames * 2, '\x01');
	put32(d, 0); d = "RIFF" + std::string(4, 0) + "WAVE";
	d += "fmt "; put32(d, 16); put16(d, 0xFFFE); put16(d, 1); put32(d, rate); put32(d, rate * 2); put16(d, 2); put16(d, 16);
	d += "data"; put32(d, (uint32_t)pcm.size()); d += pcm;
	uint32_t riff = (uint32_t)d.size() - 8; memcpy(&d[4], &riff, 4);
	return d;
}
static void write(const std::string& p, const std::string& d) { std::ofstream(p, std::ios::binary) << d; }
int main() {
	const char* env = getenv("MOCK_ROOT"); std::string root = env ? env : "/tmp/frostspore-mocktld";
	system(("rm -rf " + root + " && mkdir -p " + root + "/tld_Data/StreamingAssets/Audio/GeneratedSoundBanks/Windows " + root + "/cache").c_str());
	std::string A = root + "/tld_Data/StreamingAssets/Audio/GeneratedSoundBanks/Windows/";
	write(A + "100.wem", wemPCM(30));   // named blizzard
	write(A + "101.wem", wemPCM(20));   // named wind calm
	write(A + "102.wem", wemPCM(15));   // named wind moderate
	write(A + "103.wem", wemPCM(200));  // named music
	write(A + "104.wem", wemPCM(2));    // ui click (excluded)
	write(A + "105.wem", wemPCM(9));    // ice crack
	// a bank with an embedded howl (id 200) inside an AKPK package
	std::string howl = wemPCM(4), bank;
	bank += "BKHD"; put32(bank, 8); put32(bank, 140); put32(bank, 1);
	bank += "DIDX"; put32(bank, 12); put32(bank, 200); put32(bank, 0); put32(bank, (uint32_t)howl.size());
	bank += "DATA"; put32(bank, (uint32_t)howl.size()); bank += howl;
	std::string tables; // lang map (4 bytes: count 0), bank table (1 entry), stream table (0), no externals
	std::string lang; put32(lang, 0);
	std::string banks; put32(banks, 1); put32(banks, 7); put32(banks, 1); put32(banks, (uint32_t)bank.size()); put32(banks, 0 /*start block, patched*/); put32(banks, 0);
	std::string streams; put32(streams, 0);
	std::string hdr; put32(hdr, 1); put32(hdr, (uint32_t)lang.size()); put32(hdr, (uint32_t)banks.size()); put32(hdr, (uint32_t)streams.size());
	hdr += lang + banks + streams;
	std::string pck = "AKPK"; put32(pck, (uint32_t)hdr.size()); pck += hdr;
	uint32_t start = (uint32_t)pck.size(); memcpy(&pck[8 + 16 + lang.size() + 4 + 12], &start, 4);
	pck += bank;
	write(A + "Banks.pck", pck);
	write(A + "SoundbanksInfo.xml",
		"<SoundBanksInfo><StreamedFiles>"
		"<File Id=\"100\" Language=\"SFX\"><ShortName>Amb\\Wind_Blizzard_Loop.wav</ShortName></File>"
		"<File Id=\"101\" Language=\"SFX\"><ShortName>Amb\\Wind_Calm_Loop.wav</ShortName></File>"
		"<File Id=\"102\" Language=\"SFX\"><ShortName>Amb\\Wind_Med_Loop.wav</ShortName></File>"
		"<File Id=\"103\" Language=\"SFX\"><ShortName>Music\\MUS_Exploration_01.wav</ShortName></File>"
		"<File Id=\"104\" Language=\"SFX\"><ShortName>UI\\ui_wind_click.wav</ShortName></File>"
		"<File Id=\"105\" Language=\"SFX\"><ShortName>Env\\Ice_Crack_03.wav</ShortName></File>"
		"<File Id=\"200\" Language=\"SFX\"><ShortName>Wildlife\\Wolf_Howl_Far_02.wav</ShortName></File>"
		"</StreamedFiles></SoundBanksInfo>");
	std::wstring wroot(root.begin(), root.end());
	auto res = TldAudio::PrepareCache(wroot, wroot + L"/cache", L"unused.bin", [](const std::string& l) { printf("  log: %s\n", l.c_str()); });
	const char* expect[] = {"\"amb\\wind_calm_loop.wav\"", "\"amb\\wind_med_loop.wav\"", "\"amb\\wind_blizzard_loop.wav\"", "\"wildlife\\wolf_howl_far_02.wav\"", "\"env\\ice_crack_03.wav\"", nullptr, "\"music\\mus_exploration_01.wav\""};
	int fails = 0;
	for (auto& r : res) {
		const char* id = Sheets::kContent[r.contentIndex].id;
		const char* e = expect[r.contentIndex];
		bool ok = e ? r.source.find(e) != std::string::npos : true;
		printf("%-18s %s -> %s\n", id, ok ? "ok " : "BAD", r.source.c_str());
		if (!ok) ++fails;
		if (!r.wavPath.empty()) { std::ifstream f(std::string(r.wavPath.begin(), r.wavPath.end()), std::ios::binary); char h[4] = {}; f.read(h, 4); if (memcmp(h, "RIFF", 4)) { printf("  bad wav\n"); ++fails; } }
	}
	// second run must reuse the cache
	auto res2 = TldAudio::PrepareCache(wroot, wroot + L"/cache", L"unused.bin", [](const std::string& l) { printf("  log2: %s\n", l.c_str()); });
	if (res2[0].wavPath != res[0].wavPath) { printf("cache reuse mismatch\n"); ++fails; }
	printf(fails ? "%d FAILED\n" : "all audio tests passed\n", fails);
	return fails != 0;
}
