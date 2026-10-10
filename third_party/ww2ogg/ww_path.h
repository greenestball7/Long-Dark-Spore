// FrostSpore patch: open ww2ogg's files by UTF-8 path on Windows (player folders may hold non-ASCII names).
#pragma once
#include <string>
#ifdef _WIN32
#include <windows.h>
inline std::wstring ww_path(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n ? n - 1 : 0, L'\0');
    if (n > 1) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}
#else
inline std::string ww_path(const std::string& s) { return s; }
#endif
