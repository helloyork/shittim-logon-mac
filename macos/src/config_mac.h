// shittim-logon / macos / src / config_mac.h
//
// The Mac's settings file, until there is a settings app:
//
//     ~/Library/Application Support/ShittimLogon/settings.txt
//
// The grammar of scenes.txt on Windows (render/src/scenes.h): `key=value` per line,
// `#` starts a comment, `[06:00 白天]` opens a period and `+day_1` under it switches a
// scene on for that period. The period half is parsed by the shared code
// (scenes::ParsePeriods) so the choice is the Windows one, arithmetic and all.
//
// Written with its defaults and their descriptions the first time it is missing. A
// file written by an older build gets the blocks it does not have appended -- the
// Windows overlay's LateSettings rule, decided the same way: by whether the *key* is
// set on a line of its own, never by whether its name appears in a comment
// (on Windows, a key named in a description once blocked its own append). Descriptions
// follow the project's rule for user-facing text: written, not colloquial.

#pragma once

#import <Foundation/Foundation.h>

#include "../../render/src/scenes.h"
#include "defaults_mac.h"

#include <algorithm>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace macconfig {

struct Settings {
    // EXPERIMENTAL, off by default. While the screen is locked the desktop picture is
    // the scene's empty room, so the password screen shows the same room the overlay
    // fades out of. See macos/README.md for what it costs.
    bool lockWallpaper = false;
    // Everything else the file says, read by the Windows parser itself
    // (scenes::ParseSettings), so `bgm`, `bgm_volume`, `bgm_duck` and `bgm_track` take
    // the values and fall back to the defaults exactly as scenes.txt does. Settings the
    // Mac does not act on are parsed and ignored.
    scenes::Settings set;
    // The day's periods and the scenes each one offers.
    std::vector<scenes::Period> periods;
};

// SHITTIM_SETTINGS_DIR moves the whole directory -- for the settings app's offscreen
// screenshots, which must never touch the person's own file (a Windows test
// harness once did).
inline NSString *Dir() {
    if (const char *e = getenv("SHITTIM_SETTINGS_DIR"); e && *e) return [NSString stringWithUTF8String:e];
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/ShittimLogon"];
}
inline NSString *Path() { return [Dir() stringByAppendingPathComponent:@"settings.txt"]; }

inline std::string HeadText() {
    return "# Shittim Logon for macOS 设置\n"
           "# 每行一项，格式为 键=值；以 # 开头的行为说明。修改后在下次锁定屏幕时生效。\n";
}

// The Windows file's period note and factory blocks (sceneconfig.h PeriodNote,
// DefaultText): day and night, each with all of its own scenes switched on.
inline std::string PeriodText() {
    std::string s =
        "\n"
        "# ---- 时段与场景 -------------------------------------------------------\n"
        "#\n"
        "# 每个时段以一行 [开始时间 名称] 开始，名称可省略。时段自开始时间起，\n"
        "# 延续至下一个时段的开始时间；最后一个时段延续至第一个时段的开始时间。\n"
        "# 时段数量不限。\n"
        "#\n"
        "# 时段内行首的 '+' 表示该场景在此时段启用，每次锁定屏幕时从中选择一个，\n"
        "# 且不与上一次显示的场景相同。解锁不足 15 秒即再次锁定时，沿用上一次的场景。\n"
        "#\n"
        "# 时段与场景本身的昼夜无关：在白天的时段中启用 night_* 场景，显示的仍是\n"
        "# 夜晚的房间与光照。\n"
        "#\n"
        "# 当前时段没有已启用的场景时，从所有时段的场景中选择；所有时段均未启用\n"
        "# 任何场景时使用 night_1。\n";
    s += "\n[" + scenes::ClockTimeText(scenes::kNightEndDefault) + " 白天]\n";
    for (const scenes::Entryv &e : scenes::Table())
        if (e.when == scenes::TimeOfDay::Day) s += std::string("+") + e.name + "\n";
    s += "\n[" + scenes::ClockTimeText(scenes::kNightStartDefault) + " 夜晚]\n";
    for (const scenes::Entryv &e : scenes::Table())
        if (e.when == scenes::TimeOfDay::Night) s += std::string("+") + e.name + "\n";
    return s;
}

inline bool ParseBool(const std::string &v, bool def) {
    if (v == "true" || v == "1" || v == "on" || v == "yes") return true;
    if (v == "false" || v == "0" || v == "off" || v == "no") return false;
    return def;
}

inline Settings Load(void (*log)(const wchar_t *, ...)) {
    Settings s;
    NSFileManager *fm = NSFileManager.defaultManager;
    if (![fm fileExistsAtPath:Path()]) {
        [fm createDirectoryAtPath:Dir() withIntermediateDirectories:YES attributes:nil error:nil];
        std::string text = HeadText();
        for (const auto &kv : Blocks()) text += kv.second;
        text += PeriodText();
        [[NSString stringWithUTF8String:text.c_str()] writeToFile:Path() atomically:YES encoding:NSUTF8StringEncoding error:nil];
        if (log) log(L"config: wrote defaults to %hs", Path().UTF8String);
    }
    NSString *text = [NSString stringWithContentsOfFile:Path() encoding:NSUTF8StringEncoding error:nil] ?: @"";

    // The keys the file sets, wherever they are -- scenes::ParseSettings reads a setting
    // on any line, and settingsfile.h appends one the file lacks at the end, below the
    // last period -- and whether there is a period at all. `music` is a period's own.
    std::set<std::string> keys;
    bool anyPeriod = false;
    for (NSString *raw in [text componentsSeparatedByCharactersInSet:NSCharacterSet.newlineCharacterSet]) {
        const std::string line = scenes::trim(raw.UTF8String);
        if (line.empty() || line[0] == '#') continue;
        if (scenes::ParsePeriodHead(line, nullptr, nullptr)) { anyPeriod = true; continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = scenes::trim(line.substr(0, eq));
        std::string v = scenes::trim(line.substr(eq + 1));
        for (char &c : v) c = (char)std::tolower((unsigned char)c);
        keys.insert(k);
        if (k == "experimental_lock_wallpaper") s.lockWallpaper = ParseBool(v, s.lockWallpaper);
    }

    // The paragraphs of the keys an older file does not set (defaults_mac.h), in front
    // of its first period so the file keeps its settings above its periods; the factory
    // periods at the end of a file with none.
    std::string add;
    int added = 0;
    for (const auto &kv : Blocks())
        if (!keys.count(kv.first)) { add += kv.second; added++; }
    if (!add.empty() || !anyPeriod) {
        std::string body = text.UTF8String;
        if (!add.empty()) {
            size_t at = std::string::npos;
            size_t pos = 0;
            while (pos <= body.size()) {
                const size_t nl = body.find('\n', pos);
                const std::string line = body.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
                if (scenes::ParsePeriodHead(line, nullptr, nullptr)) { at = pos; break; }
                if (nl == std::string::npos) break;
                pos = nl + 1;
            }
            if (!body.empty() && body.back() != '\n') body += "\n";
            if (at == std::string::npos) body += add;
            else body.insert(at, add.substr(1) + "\n");
        }
        if (!anyPeriod) {
            if (!body.empty() && body.back() != '\n') body += "\n";
            body += PeriodText();
        }
        [[NSString stringWithUTF8String:body.c_str()] writeToFile:Path() atomically:YES encoding:NSUTF8StringEncoding error:nil];
        if (log) log(L"config: added %d setting(s) this file did not have yet%hs", added, anyPeriod ? "" : ", and the periods");
        text = [NSString stringWithUTF8String:body.c_str()];
    }

    s.set = scenes::ParseSettings(text.UTF8String);
    s.periods = scenes::ParsePeriods(text.UTF8String);
    if (log) log(L"config: experimental_lock_wallpaper=%hs bgm=%hs volume %d duck %d track '%hs', %d period(s)",
                 s.lockWallpaper ? "true" : "false", s.set.bgm ? "true" : "false", s.set.bgmVolume,
                 s.set.bgmDuck, s.set.bgmTrack.c_str(), (int)s.periods.size());
    return s;
}

// ------------------------------------------------------------------ the scene
//
// The Windows overlay's rotation, decision for decision (overlay/src/sceneconfig.h
// Pick, Requeue, ChooseScene; dcomp.h WaitForLogonDesktop). Only the plumbing differs:
// on Windows the screen and the gap between screens are two processes, here they are
// one, so the two files they talk through are kept anyway -- that is what makes a
// restart of this app behave like a fresh Windows process.
//
//   next-scene    the room the next screen will draw (Windows: the queue)
//   shown-scene   the room the last screen drew (R91)
//
// Windows' `lock_background` (R63) is about the picture Windows bakes for its own
// credential screen and has no Mac counterpart; it is treated as off, which is
// Windows' default.

inline NSString *NextPath() { return [Dir() stringByAppendingPathComponent:@"next-scene"]; }
inline NSString *ShownPath() { return [Dir() stringByAppendingPathComponent:@"shown-scene"]; }
inline std::string ReadName(NSString *path) {
    NSString *t = [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil];
    return t ? scenes::trim(t.UTF8String) : std::string();
}
inline bool WriteName(NSString *path, const std::string &name) {
    [NSFileManager.defaultManager createDirectoryAtPath:Dir() withIntermediateDirectories:YES attributes:nil error:nil];
    return [[NSString stringWithUTF8String:name.c_str()] writeToFile:path atomically:YES
                                                            encoding:NSUTF8StringEncoding error:nil];
}
inline std::string ReadNextScene() { return ReadName(NextPath()); }
inline bool WriteNextScene(const std::string &n) { return WriteName(NextPath(), n); }
inline std::string ReadShownScene() { return ReadName(ShownPath()); }
inline bool WriteShownScene(const std::string &n) { return WriteName(ShownPath(), n); }

inline int MinuteNow() {
    NSDateComponents *c = [NSCalendar.currentCalendar components:NSCalendarUnitHour | NSCalendarUnitMinute fromDate:NSDate.date];
    return (int)(c.hour * 60 + c.minute);
}

// sceneconfig::ChooseScene: `avoid` is skipped unless it is the only thing left.
inline const scenes::Resolved &ChooseScene(const std::vector<scenes::Resolved> &active,
                                           const std::string &avoid) {
    std::vector<size_t> pool;
    for (size_t i = 0; i < active.size(); i++)
        if (active[i].name != avoid) pool.push_back(i);
    if (pool.empty()) return active[0];
    return active[pool[arc4random_uniform((uint32_t)pool.size())]];
}

// sceneconfig::ActiveInPeriodsNow. The Mac file always has periods (Load appends the
// factory ones to a file without), so ActiveNow's period-less branch is never reached.
inline std::vector<scenes::Resolved> ActiveNow(const Settings &s, std::vector<std::string> *rejected,
                                               void (*log)(const wchar_t *, ...)) {
    {
        size_t a = 0, b = 0;
        if (log && scenes::PeriodsCollide(s.periods, &a, &b))
            log(L"scenes: two periods both start at %hs -- the later one in the file can "
                L"never run", scenes::ClockTimeText(s.periods[a].start).c_str());
    }
    scenes::PeriodPick how = scenes::PeriodPick::Period;
    int chosen = -1;
    std::vector<scenes::Resolved> out =
        scenes::ActiveInPeriods(s.periods, MinuteNow(), false, rejected, &how, &chosen);
    if (log && how == scenes::PeriodPick::UnknownOnly && chosen >= 0)
        log(L"scenes: every scene switched on in the period starting at %hs is one this "
            L"build does not have -- choosing from every period's instead",
            scenes::ClockTimeText(s.periods[(size_t)chosen].start).c_str());
    return out;
}

// sceneconfig::MusicFor / MusicNow: the period's own track, or the file's `bgm_track`.
inline std::string MusicNow(const Settings &s) {
    const int at = s.periods.empty() ? -1 : scenes::PeriodAt(s.periods, MinuteNow());
    if (at < 0 || (size_t)at >= s.periods.size() || s.periods[(size_t)at].music.empty())
        return s.set.bgmTrack;
    return s.periods[(size_t)at].music;
}

// sceneconfig::Pick: the scene to show now. **Not a draw** -- the draw happened between
// screens, in Requeue, and this reads its answer out of the queue. `keepIfStillValid`
// is the scene already chosen (the gate's narrow question); empty at startup.
inline scenes::Resolved Pick(const Settings &s, void (*log)(const wchar_t *, ...),
                             const std::string &keepIfStillValid = std::string()) {
    std::vector<std::string> rejected;
    std::vector<scenes::Resolved> active = ActiveNow(s, &rejected, log);
    for (const std::string &r : rejected)
        if (log) log(L"scenes: '%hs' is not a scene this build has -- ignored", r.c_str());
    if (active.empty()) {
        if (log) log(L"scenes: nothing active -- falling back to night_1");
        active.push_back(scenes::Resolve("night_1"));
    }
    if (!keepIfStillValid.empty()) {
        for (const scenes::Resolved &a : active) {
            if (a.name != keepIfStillValid) continue;
            if (log) {
                log(L"scenes: re-read the config -- '%hs' is still active, keeping it",
                    keepIfStillValid.c_str());
                log(L"scenes: showing %hs of %d active -- room %hs, Idle_%02d, character %hs",
                    a.name.c_str(), (int)active.size(), a.room.c_str(), a.idle, a.character.c_str());
            }
            return a;
        }
        if (log) log(L"scenes: re-read the config -- '%hs' is no longer among the %d "
                     L"active, choosing again", keepIfStillValid.c_str(), (int)active.size());
    }
    const std::string queued = ReadNextScene();
    scenes::Resolved now = ChooseScene(active, std::string());
    bool fromQueue = false;
    if (!queued.empty()) {
        for (const scenes::Resolved &a : active)
            if (a.name == queued) { now = a; fromQueue = true; break; }
        if (!fromQueue && log)
            log(L"scenes: '%hs' was queued but is no longer active -- drew %hs instead",
                queued.c_str(), now.name.c_str());
    }
    if (log) log(L"scenes: showing %hs of %d active -- room %hs, Idle_%02d, character %hs",
                 now.name.c_str(), (int)active.size(), now.room.c_str(), now.idle,
                 now.character.c_str());
    if (!fromQueue) WriteNextScene(now.name);
    return now;
}

// sceneconfig::Requeue, between screens. `rotate` is true from exactly one caller: the
// session has been unlocked for fifteen seconds (R57), once per gap between screens;
// false from the minute tick, which only keeps the queue in season. `drawing` is kept
// in step with the queue. Returns whether the queue moved.
inline bool Requeue(const Settings &s, bool rotate, std::string *drawing,
                    void (*log)(const wchar_t *, ...)) {
    std::vector<scenes::Resolved> active = ActiveNow(s, nullptr, nullptr);
    if (active.empty()) return false;

    const std::string queued = ReadNextScene();
    bool eligible = false;
    for (const scenes::Resolved &a : active)
        if (a.name == queued) { eligible = true; break; }

    // One rotation per screen, not one per park (R91).
    const std::string shown = ReadShownScene();
    const bool rotatedSinceShown = !shown.empty() && eligible && queued != shown;
    if (rotate && rotatedSinceShown && log)
        log(L"scenes: the queue already names '%hs', which no screen has drawn since '%hs' -- "
            L"this rotation is declined, one room per screen (R91)", queued.c_str(), shown.c_str());

    std::string want = queued;
    const std::string &avoid = shown.empty() ? queued : shown;
    if (!eligible || (rotate && !rotatedSinceShown))
        want = ChooseScene(active, avoid).name;
    if (want != queued) {
        WriteNextScene(want);
        if (log) log(L"scenes: next screen is '%hs' rather than '%hs' (%hs)",
                     want.c_str(), queued.empty() ? "(nothing)" : queued.c_str(),
                     eligible ? "rotated" : "no longer eligible");
    }
    if (drawing) *drawing = want;
    return want != queued;
}

} // namespace macconfig
