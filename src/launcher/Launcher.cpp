// FrostSpore launcher: Melty runs this with the player's Spore and The Long Dark folders.
//   FrostSpore.exe --spore "<Spore folder>" --tld "<The Long Dark folder>"
// 1. Reads The Long Dark's audio from the player's install into a WAV cache (TldAudio).
// 2. Starts Spore Galactic Adventures' SporeApp.exe suspended and injects the ModAPI loader
//    (ModAPI.DLLInjector.dll, from the Spore ModAPI Launcher Kit) with SporeModAPI.dll and FrostSpore.dll.
// Everything is logged to %LOCALAPPDATA%\FrostSpore\logs\launcher.log.
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <string>
#include <vector>
#include "TldAudio.h"
#include "Sheets.gen.h"

namespace {

std::wstring gLogPath;

std::string Utf8(const std::wstring& w) {
	int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
	std::string s(n ? n - 1 : 0, '\0');
	if (n > 1) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
	return s;
}

void Log(const std::string& line) {
	if (FILE* f = _wfopen(gLogPath.c_str(), L"ab")) {
		SYSTEMTIME t; GetLocalTime(&t);
		fprintf(f, "%02d:%02d:%02d %s\r\n", t.wHour, t.wMinute, t.wSecond, line.c_str());
		fclose(f);
	}
}

void Fail(const std::wstring& msg) {
	Log("ERROR: " + Utf8(msg));
	MessageBoxW(nullptr, (msg + L"\n\nDetails: " + gLogPath).c_str(), L"Spore: The Long Dark", MB_ICONERROR | MB_OK);
}

bool Exists(const std::wstring& p) { return GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES; }
std::wstring Dir(const std::wstring& p) { size_t i = p.find_last_of(L"\\/"); return i == std::wstring::npos ? L"." : p.substr(0, i); }
std::wstring Trim(std::wstring s) { while (!s.empty() && (s.back() == L'\\' || s.back() == L'/' || s.back() == L'"')) s.pop_back(); return s; }

void MakeDirs(const std::wstring& p) { SHCreateDirectoryExW(nullptr, p.c_str(), nullptr); }

LONGLONG FileSize(const std::wstring& p) {
	WIN32_FILE_ATTRIBUTE_DATA d;
	if (!GetFileAttributesExW(p.c_str(), GetFileExInfoStandard, &d)) return -1;
	return ((LONGLONG)d.nFileSizeHigh << 32) | d.nFileSizeLow;
}

// Same executable table the official ModAPI Launcher Kit uses (ModAPI.Common/GameVersion.cs).
struct ExeKind { LONGLONG size; const wchar_t* name; bool disk; bool needsModAPIFix; bool steam; };
const ExeKind kExeKinds[] = {
	{24909584, L"Disc + Patch 5.1 (July 2009)", true, false, false},
	{24898224, L"Origin (March 2017)", false, true, false},
	{24906040, L"EA App (October 2024)", false, true, false},
	{24885248, L"GOG/Steam (March 2017)", false, false, true},
	{24895536, L"GOG (October 2024)", false, false, false},
	{25066744, L"Steam (October 2024)", false, false, true},
};

// Inject a DLL by full path with LoadLibraryW in the suspended process; returns its base address there.
HMODULE InjectLoadLibrary(HANDLE proc, const std::wstring& dll) {
	size_t bytes = (dll.size() + 1) * sizeof(wchar_t);
	void* remote = VirtualAllocEx(proc, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) return nullptr;
	WriteProcessMemory(proc, remote, dll.c_str(), bytes, nullptr);
	auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
	HANDLE th = CreateRemoteThread(proc, nullptr, 0, loadLib, remote, 0, nullptr);
	if (!th) { VirtualFreeEx(proc, remote, 0, MEM_RELEASE); return nullptr; }
	WaitForSingleObject(th, 15000);
	DWORD code = 0;
	GetExitCodeThread(th, &code);  // 32-bit launcher + 32-bit Spore: the module handle fits
	CloseHandle(th);
	VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
	return (HMODULE)(uintptr_t)code;
}

// Calls ModAPI.DLLInjector's SetInjectionData in Spore with the DLL list (format from the Launcher Kit's Injector.cs).
bool SetInjectionData(HANDLE proc, HMODULE remoteInjector, const std::wstring& injectorPath, bool disk, const std::vector<std::wstring>& dlls) {
	HMODULE local = LoadLibraryExW(injectorPath.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
	if (!local) return false;
	FARPROC fn = GetProcAddress(local, "SetInjectionData");
	uintptr_t offset = fn ? (uintptr_t)fn - (uintptr_t)local : 0;
	FreeLibrary(local);
	if (!offset) return false;
	std::vector<uint8_t> data;
	data.push_back(disk ? 1 : 0);
	auto put32 = [&](uint32_t v) { for (int i = 0; i < 4; ++i) data.push_back((uint8_t)(v >> (8 * i))); };
	put32((uint32_t)dlls.size());
	for (auto& d : dlls) {
		put32((uint32_t)d.size());
		const uint8_t* p = (const uint8_t*)d.c_str();
		data.insert(data.end(), p, p + d.size() * sizeof(wchar_t));
	}
	void* remote = VirtualAllocEx(proc, nullptr, data.size(), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (!remote) return false;
	WriteProcessMemory(proc, remote, data.data(), data.size(), nullptr);
	HANDLE th = CreateRemoteThread(proc, nullptr, 0, (LPTHREAD_START_ROUTINE)((uintptr_t)remoteInjector + offset), remote, 0, nullptr);
	if (!th) return false;
	WaitForSingleObject(th, 15000);
	CloseHandle(th);
	// The injector copies the strings, so the block can go.
	VirtualFreeEx(proc, remote, 0, MEM_RELEASE);
	return true;
}

}  // namespace

int WINAPI wWinMain(HINSTANCE, HINSTANCE, LPWSTR, int) {
	wchar_t* la = nullptr;
	SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &la);
	const std::wstring appData = std::wstring(la ? la : L".") + L"\\FrostSpore";
	CoTaskMemFree(la);
	MakeDirs(appData + L"\\logs");
	gLogPath = appData + L"\\logs\\launcher.log";
	DeleteFileW(gLogPath.c_str());

	wchar_t self[MAX_PATH * 2];
	GetModuleFileNameW(nullptr, self, MAX_PATH * 2);
	const std::wstring modDir = Dir(self);

	std::wstring spore, tld;
	int argc = 0;
	LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
	for (int i = 1; i + 1 < argc; ++i) {
		if (!wcscmp(argv[i], L"--spore")) spore = Trim(argv[++i]);
		else if (!wcscmp(argv[i], L"--tld")) tld = Trim(argv[++i]);
	}
	LocalFree(argv);
	Log("FrostSpore launcher, mod folder " + Utf8(modDir));
	Log("Spore folder: " + Utf8(spore));
	Log("The Long Dark folder: " + Utf8(tld));
	if (spore.empty() || tld.empty()) { Fail(L"Start this from Melty: it passes the Spore and The Long Dark folders."); return 1; }

	// 1. The Long Dark's own sound, from the player's copy
	const std::wstring cache = appData + L"\\tld-cache";
	MakeDirs(cache);
	auto clips = TldAudio::PrepareCache(tld, cache, modDir + L"\\packed_codebooks_aoTuV_603.bin", [](const std::string& l) { Log("tld: " + l); });
	int found = 0;
	for (auto& c : clips) if (!c.wavPath.empty()) ++found;
	Log("tld: " + std::to_string(found) + " of " + std::to_string(clips.size()) + " clips ready");

	// 2. Spore Galactic Adventures executable
	const std::wstring sporebin = spore + L"\\SporebinEP1";
	const std::wstring exe = sporebin + L"\\SporeApp.exe";
	LONGLONG size = FileSize(exe);
	const ExeKind* kind = nullptr;
	for (auto& k : kExeKinds) if (k.size == size) kind = &k;
	if (!kind) { Fail(L"This Spore version isn't one Spore ModAPI supports (SporebinEP1\\SporeApp.exe, " + std::to_wstring(size) + L" bytes). Spore with Galactic Adventures from Steam, GOG, EA app or disc + patch 5.1 is needed."); return 1; }
	Log("Spore version: " + Utf8(kind->name));
	if (kind->disk) { Fail(L"The disc release of Spore isn't supported by this mashup yet; the Steam, GOG and EA app releases are."); return 1; }
	std::wstring runExe = exe;
	if (kind->needsModAPIFix) {
		runExe = sporebin + L"\\SporeApp_ModAPIFix.exe";
		if (!Exists(runExe)) { Fail(L"The EA app/Origin release of Spore needs the ModAPI compatibility fix (SporeApp_ModAPIFix.exe) that the official Spore ModAPI Launcher Kit installs."); return 1; }
	}
	if (kind->steam) {
		// As the Launcher Kit does: Steam's DRM wants Galactic Adventures' app id to start SporeApp.exe directly.
		const std::wstring appid = sporebin + L"\\steam_appid.txt";
		if (!Exists(appid)) {
			if (FILE* f = _wfopen(appid.c_str(), L"wb")) { fputs("24720", f); fclose(f); Log("wrote steam_appid.txt"); }
		}
		SetEnvironmentVariableW(L"SteamAppId", L"24720");
	}

	// 3. Tell FrostSpore.dll where things are
	SetEnvironmentVariableW(L"FROSTSPORE_CACHE", cache.c_str());
	SetEnvironmentVariableW(L"FROSTSPORE_LOG", (appData + L"\\logs\\mod.log").c_str());

	const std::wstring injector = modDir + L"\\ModAPI.DLLInjector.dll";
	std::vector<std::wstring> dlls = {modDir + L"\\SporeModAPI.dll", modDir + L"\\FrostSpore.dll"};
	for (auto& d : dlls) if (!Exists(d)) { Fail(L"Missing file: " + d); return 1; }
	if (!Exists(injector)) { Fail(L"Missing file: " + injector); return 1; }

	// 4. Start Spore suspended, load the injector, hand it the DLL list, resume
	std::wstring cmd = L"\"" + runExe + L"\"";
	STARTUPINFOW si = {sizeof si};
	PROCESS_INFORMATION pi = {};
	if (!CreateProcessW(nullptr, &cmd[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr, sporebin.c_str(), &si, &pi)) {
		Fail(L"Couldn't start Spore (error " + std::to_wstring(GetLastError()) + L")."); return 1;
	}
	HMODULE remote = InjectLoadLibrary(pi.hProcess, injector);
	if (!remote || !SetInjectionData(pi.hProcess, remote, injector, false, dlls)) {
		TerminateProcess(pi.hProcess, 0);
		Fail(L"Couldn't load the Spore mod loader into Spore."); return 1;
	}
	ResumeThread(pi.hThread);
	Log("Spore started (pid " + std::to_string(pi.dwProcessId) + ") with " + std::to_string(dlls.size()) + " mod DLLs");
	// Stay alive until Spore exits so Melty sees the play session end.
	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD code = 0; GetExitCodeProcess(pi.hProcess, &code);
	Log("Spore exited with code " + std::to_string(code));
	CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
	return 0;
}
