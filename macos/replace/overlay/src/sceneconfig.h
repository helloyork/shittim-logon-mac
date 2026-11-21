// shittim-logon / macos / replace / overlay / src / sceneconfig.h
//
// The Mac stand-in for overlay/src/sceneconfig.h, for exactly one reader:
// config/src/settingsfile.h, the Windows settings app's line-by-line editor, which the
// Mac settings app compiles unchanged. It asks this header for two things -- where the
// file is (`Path`) and what is in it (`Read`) -- and saves through four Win32 file
// calls, shimmed here with POSIX. Nothing else on the Mac includes sceneconfig.h; the
// overlay's own scene logic is macos/src/config_mac.h.
//
// Truncate-and-write in place, as settingsfile.h's Save does on Windows (its note: a
// rename would lose the file's permissions there; here it keeps the file's identity
// for an editor that has it open).

#pragma once

#include <windows.h>

#include <cstdio>
#include <string>

#include "../../render/src/scenes.h"
#include "../../macos/src/config_mac.h"

// ---------------------------------------------------------------- the four calls

typedef FILE *HANDLE;
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE) nullptr)
#endif
#define GENERIC_WRITE 0x40000000u
#define FILE_SHARE_READ 0x1u
#define CREATE_ALWAYS 2u
#define FILE_ATTRIBUTE_NORMAL 0x80u

inline std::string NarrowPath(const wchar_t *w) {
    std::string out;
    for (; *w; ++w) {
        const uint32_t c = (uint32_t)*w;
        if (c < 0x80) out.push_back((char)c);
        else if (c < 0x800) { out.push_back((char)(0xC0 | (c >> 6))); out.push_back((char)(0x80 | (c & 0x3F))); }
        else if (c < 0x10000) { out.push_back((char)(0xE0 | (c >> 12))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
        else { out.push_back((char)(0xF0 | (c >> 18))); out.push_back((char)(0x80 | ((c >> 12) & 0x3F))); out.push_back((char)(0x80 | ((c >> 6) & 0x3F))); out.push_back((char)(0x80 | (c & 0x3F))); }
    }
    return out;
}
inline HANDLE CreateFileW(const wchar_t *path, DWORD, DWORD, void *, DWORD, DWORD, void *) {
    return std::fopen(NarrowPath(path).c_str(), "wb");
}
inline BOOL WriteFile(HANDLE h, const void *data, DWORD n, DWORD *wrote, void *) {
    const size_t w = std::fwrite(data, 1, n, h);
    if (wrote) *wrote = (DWORD)w;
    return w == n;
}
inline BOOL CloseHandle(HANDLE h) { return h && std::fclose(h) == 0; }

// settingsfile.h's SetFloat formats with MSVC's array form.
#include <cstdarg>
template <size_t N> inline int sprintf_s(char (&buf)[N], const char *f, ...) {
    va_list a;
    va_start(a, f);
    const int n = std::vsnprintf(buf, N, f, a);
    va_end(a);
    return n;
}

// ---------------------------------------------------------------- the file

namespace sceneconfig {

inline std::wstring Path() {
    const std::string p = macconfig::Path().UTF8String;
    std::wstring w;
    NSString *s = [NSString stringWithUTF8String:p.c_str()];
    NSData *d = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
    w.assign((const wchar_t *)d.bytes, d.length / sizeof(wchar_t));
    return w;
}

// The text as it is. The blocks a file lacks were appended by macconfig::Load before
// the settings app reads it (the Windows Read does that appending itself).
inline std::string Read(void (*)(const wchar_t *, ...)) {
    NSString *t = [NSString stringWithContentsOfFile:macconfig::Path() encoding:NSUTF8StringEncoding error:nil];
    return t ? std::string(t.UTF8String) : std::string();
}

} // namespace sceneconfig
