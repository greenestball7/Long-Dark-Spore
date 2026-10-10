#include "Audio.h"
#include "Log.h"
#include <windows.h>
#include <mmsystem.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

namespace Audio {
namespace {

constexpr int kRate = 44100;
constexpr int kFrames = 2048;  // per buffer (~46 ms)
constexpr int kBuffers = 4;
constexpr int kMaxClips = 16;

struct Clip { std::vector<float> data; int channels = 2; int rate = kRate; bool ok = false; };
struct Voice { int clip = -1; double pos = 0; float vol = 0, target = 0; bool loop = false; bool active = false; };

Clip gClips[kMaxClips];
Voice gAmbience[2];  // current + fading out
int gAmbienceCurrent = 0;
Voice gMusic;
Voice gShots[6];
float gMaster = 0, gMasterTarget = 0;

CRITICAL_SECTION gLock;
HWAVEOUT gOut = nullptr;
WAVEHDR gHdr[kBuffers];
short gPcm[kBuffers][kFrames * 2];
HANDLE gThread = nullptr, gEvent = nullptr;
volatile bool gRunning = false;

bool ReadWav(const std::wstring& path, Clip& c) {
	FILE* f = _wfopen(path.c_str(), L"rb");
	if (!f) return false;
	fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
	std::vector<uint8_t> b(n > 0 ? n : 0);
	size_t got = fread(b.data(), 1, b.size(), f);
	fclose(f);
	if (got < 44 || memcmp(b.data(), "RIFF", 4) || memcmp(b.data() + 8, "WAVE", 4)) return false;
	int ch = 0, rate = 0, bits = 0;
	for (size_t p = 12; p + 8 <= b.size();) {
		uint32_t sz; memcpy(&sz, &b[p + 4], 4);
		if (!memcmp(&b[p], "fmt ", 4)) { ch = *(uint16_t*)&b[p + 10]; rate = *(uint32_t*)&b[p + 12]; bits = *(uint16_t*)&b[p + 22]; }
		else if (!memcmp(&b[p], "data", 4)) {
			if (bits != 16 || ch < 1 || ch > 8 || rate <= 0) return false;
			size_t samples = (std::min<size_t>(sz, b.size() - p - 8)) / 2;
			const short* s = (const short*)&b[p + 8];
			size_t frames = samples / ch;
			c.data.resize(frames * 2);
			for (size_t i = 0; i < frames; ++i) {  // down/up-mix to stereo floats
				float l = s[i * ch] / 32768.f, r = ch > 1 ? s[i * ch + 1] / 32768.f : l;
				c.data[i * 2] = l; c.data[i * 2 + 1] = r;
			}
			c.channels = 2; c.rate = rate; c.ok = frames > 0;
			return c.ok;
		}
		p += 8 + sz + (sz & 1);
	}
	return false;
}

// Adds one voice into the float mix; returns false when a one-shot finished.
bool MixVoice(Voice& v, float* mix, int frames) {
	if (!v.active || v.clip < 0 || !gClips[v.clip].ok) return false;
	const Clip& c = gClips[v.clip];
	const size_t len = c.data.size() / 2;
	const double step = (double)c.rate / kRate;
	const float fade = 1.0f / (kRate * 1.5f);  // 1.5 s fades
	for (int i = 0; i < frames; ++i) {
		if (v.vol < v.target) v.vol = std::min(v.target, v.vol + fade); else if (v.vol > v.target) v.vol = std::max(v.target, v.vol - fade);
		size_t i0 = (size_t)v.pos;
		if (i0 >= len) {
			if (!v.loop) { v.active = false; return false; }
			v.pos -= len; i0 = (size_t)v.pos;
		}
		size_t i1 = i0 + 1 < len ? i0 + 1 : (v.loop ? 0 : i0);
		float t = (float)(v.pos - i0);
		mix[i * 2] += (c.data[i0 * 2] * (1 - t) + c.data[i1 * 2] * t) * v.vol;
		mix[i * 2 + 1] += (c.data[i0 * 2 + 1] * (1 - t) + c.data[i1 * 2 + 1] * t) * v.vol;
		v.pos += step;
	}
	if (v.target == 0 && v.vol == 0) v.active = false;
	return true;
}

void Fill(short* out) {
	static float mix[kFrames * 2];
	memset(mix, 0, sizeof mix);
	EnterCriticalSection(&gLock);
	for (auto& v : gAmbience) MixVoice(v, mix, kFrames);
	MixVoice(gMusic, mix, kFrames);
	for (auto& v : gShots) MixVoice(v, mix, kFrames);
	const float step = 1.0f / (kRate * 2.0f) * kFrames;
	if (gMaster < gMasterTarget) gMaster = std::min(gMasterTarget, gMaster + step); else gMaster = std::max(gMasterTarget, gMaster - step);
	const float m = gMaster;
	LeaveCriticalSection(&gLock);
	for (int i = 0; i < kFrames * 2; ++i) {
		float s = mix[i] * m;
		s = s > 1 ? 1 : (s < -1 ? -1 : s);
		out[i] = (short)(s * 32767);
	}
}

DWORD WINAPI Thread(void*) {
	for (int i = 0; i < kBuffers; ++i) { Fill(gPcm[i]); waveOutWrite(gOut, &gHdr[i], sizeof(WAVEHDR)); }
	while (gRunning) {
		WaitForSingleObject(gEvent, 100);
		for (int i = 0; i < kBuffers && gRunning; ++i) {
			if (gHdr[i].dwFlags & WHDR_DONE) {
				gHdr[i].dwFlags &= ~WHDR_DONE;
				Fill(gPcm[i]);
				waveOutWrite(gOut, &gHdr[i], sizeof(WAVEHDR));
			}
		}
	}
	return 0;
}

}  // namespace

bool Start() {
	InitializeCriticalSection(&gLock);
	gEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
	WAVEFORMATEX fmt = {};
	fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = kRate; fmt.wBitsPerSample = 16;
	fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = kRate * 4;
	if (waveOutOpen(&gOut, WAVE_MAPPER, &fmt, (DWORD_PTR)gEvent, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) { Log::Line("audio: no output device"); return false; }
	for (int i = 0; i < kBuffers; ++i) {
		memset(&gHdr[i], 0, sizeof(WAVEHDR));
		gHdr[i].lpData = (LPSTR)gPcm[i]; gHdr[i].dwBufferLength = sizeof(gPcm[i]);
		waveOutPrepareHeader(gOut, &gHdr[i], sizeof(WAVEHDR));
	}
	gRunning = true;
	gThread = CreateThread(nullptr, 0, Thread, nullptr, 0, nullptr);
	return true;
}

void Stop() {
	if (!gRunning) return;
	gRunning = false;
	SetEvent(gEvent);
	WaitForSingleObject(gThread, 2000);
	waveOutReset(gOut);
	for (auto& h : gHdr) waveOutUnprepareHeader(gOut, &h, sizeof(WAVEHDR));
	waveOutClose(gOut);
}

bool Load(int i, const std::wstring& path) {
	if (i < 0 || i >= kMaxClips) return false;
	Clip c;
	if (!ReadWav(path, c)) return false;
	EnterCriticalSection(&gLock);
	gClips[i] = std::move(c);
	LeaveCriticalSection(&gLock);
	return true;
}

bool Has(int i) { return i >= 0 && i < kMaxClips && gClips[i].ok; }

void SetAmbience(int i, float volume) {
	EnterCriticalSection(&gLock);
	Voice& cur = gAmbience[gAmbienceCurrent];
	if (cur.active && cur.clip == i) cur.target = volume;
	else {
		cur.target = 0;  // fade the old bed out
		gAmbienceCurrent ^= 1;
		Voice& nv = gAmbience[gAmbienceCurrent];
		nv = Voice();
		if (Has(i)) { nv.clip = i; nv.loop = true; nv.active = true; nv.vol = 0; nv.target = volume; }
	}
	LeaveCriticalSection(&gLock);
}

void PlayOneShot(int i, float volume) {
	if (!Has(i)) return;
	EnterCriticalSection(&gLock);
	for (auto& v : gShots) if (!v.active) { v = Voice(); v.clip = i; v.active = true; v.vol = v.target = volume; break; }
	LeaveCriticalSection(&gLock);
}

void PlayMusic(int i, float volume) {
	if (!Has(i)) return;
	EnterCriticalSection(&gLock);
	if (!gMusic.active) { gMusic = Voice(); gMusic.clip = i; gMusic.active = true; gMusic.vol = 0; gMusic.target = volume; }
	LeaveCriticalSection(&gLock);
}

bool MusicPlaying() { return gMusic.active; }

void SetMaster(float v) { gMasterTarget = v; }

}  // namespace Audio
