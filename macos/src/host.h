// shittim-logon / macos / src / host.h
//
// What every Mac host shares: the spine extension, the log function the shared
// headers call with MSVC-style wide format strings, and setting up a SpineLayer for
// a scene the way overlay.cpp does (ApplyScene, scenes::Resolve, full scene).

#pragma once

#include "../../overlay/src/spine_layer.h"
#include "../../render/src/spine_io.h"

#include <cstdarg>
#include <cstdio>
#include <cwchar>
#include <string>

#include <sys/stat.h>

spine::SpineExtension *spine::getDefaultExtension() { return new sl::WidePathExtension(); }

namespace host {

inline FILE *&LogFile() { static FILE *f = nullptr; return f; }

// The log rolls, as rolllog.h does on Windows, but by size (about 3 MB at
// most): two generations, the file and `<file>.1`, each up to 1.5 MiB.
// Kept across restarts -- a start appends. Only the overlay writes it, so there is no
// second writer to re-read for.
constexpr long kLogMaxBytes = 1536 * 1024;

inline std::string &LogPath() { static std::string p; return p; }

// ~/Library/Logs/ShittimLogon/lockscreen.log, where Console.app looks.
inline std::string DefaultLogPath() {
    const char *home = getenv("HOME");
    return std::string(home ? home : "/tmp") + "/Library/Logs/ShittimLogon/lockscreen.log";
}

inline void OpenLog(const std::string &path) {
    LogPath() = path;
    const std::string dir = path.substr(0, path.rfind('/'));
    for (size_t i = 1; i <= dir.size(); i++)
        if (i == dir.size() || dir[i] == '/') mkdir(dir.substr(0, i).c_str(), 0755);
    LogFile() = std::fopen(path.c_str(), "a");
}

// After a line: past the cap, the file becomes `.1` (replacing the one before) and a
// fresh one is started.
inline void RollIfFull() {
    FILE *f = LogFile();
    if (!f || std::ftell(f) < kLogMaxBytes) return;
    std::fclose(f);
    const std::string &p = LogPath();
    std::rename(p.c_str(), (p + ".1").c_str());
    LogFile() = std::fopen(p.c_str(), "a");
}

// MSVC's wide printf reads %s as a wide string and %hs as a narrow one; POSIX reads
// %s as narrow and %ls as wide. The shared code is written for MSVC.
inline std::wstring PosixFormat(const wchar_t *f) {
    std::wstring o;
    for (const wchar_t *p = f; *p; p++) {
        o.push_back(*p);
        if (*p != L'%') continue;
        if (p[1] == L'%') { o.push_back(L'%'); p++; continue; }
        p++;
        while (*p && std::wcschr(L"-+ #0123456789.*", *p)) o.push_back(*p++);
        if (*p == L'h' && (p[1] == L's' || p[1] == L'c')) { o.push_back(p[1]); p++; continue; }
        if (*p == L'l' && (p[1] == L's' || p[1] == L'c')) { o.push_back(L'l'); o.push_back(p[1]); p++; continue; }
        if (*p == L's' || *p == L'c') { o.push_back(L'l'); o.push_back(*p); continue; }
        if (*p == L'S') { o.push_back(L's'); continue; }
        if (*p == L'C') { o.push_back(L'c'); continue; }
        if (!*p) break;
        o.push_back(*p);
    }
    return o;
}

inline void Log(const wchar_t *fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vswprintf(buf, 2048, PosixFormat(fmt).c_str(), ap);
    va_end(ap);
    const std::string s = sl::narrowUtf8(buf);
    struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
    struct tm tmv; localtime_r(&ts.tv_sec, &tmv);
    char stamp[32];
    std::snprintf(stamp, sizeof(stamp), "%02d:%02d:%02d.%03ld", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, ts.tv_nsec / 1000000);
    std::fprintf(stderr, "%s %s\n", stamp, s.c_str());
    if (LogFile()) { std::fprintf(LogFile(), "%s %s\n", stamp, s.c_str()); std::fflush(LogFile()); RollIfFull(); }
}

// The scene into the layer, as overlay.cpp's ApplyScene does for full-scene mode.
inline bool ApplyScene(SpineLayer &layer, const std::string &name, std::string &character) {
    const scenes::Resolved s = scenes::Resolve(name);
    if (!s.ok) { Log(L"scene: unknown '%hs'", name.c_str()); return false; }
    const bool day = s.when == scenes::TimeOfDay::Day;
    character = s.character;
    layer.roomName = s.room;
    layer.idleIndex = s.idle;
    layer.companionIdle = s.companion;
    layer.roomGain = 1.0f * s.gain;
    layer.clockGain = day ? 1.20f : 1.00f;
    layer.drift.day = day;
    layer.fullScene = true;
    layer.fgSuperAuto = true;
    layer.driftOn = true;
    Log(L"scene: %hs -- room %hs idle %d companion %d, foreground %hs",
        name.c_str(), s.room.c_str(), s.idle, s.companion, s.character.c_str());
    return true;
}

} // namespace host
