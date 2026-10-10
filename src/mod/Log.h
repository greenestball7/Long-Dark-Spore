// Appends lines to the file named by FROSTSPORE_LOG (set by the launcher).
#pragma once
#include <windows.h>
#include <cstdarg>
#include <cstdio>

namespace Log {
inline void Line(const char* fmt, ...) {
	static wchar_t path[MAX_PATH * 2] = {};
	if (!path[0] && !GetEnvironmentVariableW(L"FROSTSPORE_LOG", path, MAX_PATH * 2)) return;
	FILE* f = _wfopen(path, L"ab");
	if (!f) return;
	SYSTEMTIME t; GetLocalTime(&t);
	fprintf(f, "%02d:%02d:%02d ", t.wHour, t.wMinute, t.wSecond);
	va_list a; va_start(a, fmt); vfprintf(f, fmt, a); va_end(a);
	fputs("\r\n", f);
	fclose(f);
}
}  // namespace Log
