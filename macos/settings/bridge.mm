// shittim-logon / macos / settings / bridge.mm
//
// bridge.h, over the shared code. Each piece below names the Windows function it is a
// port of (config/src/configwindow.h unless said otherwise); where the Windows one is
// usable as it is -- settingsfile.h, scenes.h, the strings -- it is called, not copied.

#import "bridge.h"

#include "../../config/src/settingsfile.h"   // pulls in the Mac sceneconfig.h (macos/replace)
#include "../../macos/src/audiodev_mac.h"
#include "../../macos/src/clocktext_mac.h"
#include "../../macos/src/config_mac.h"
#include "../../macos/src/wallpaper_mac.h"

#include <map>
#include <unordered_map>

namespace i18n {
struct Entry {
    const wchar_t *hans;
    const wchar_t *hant;
    const wchar_t *en;
    const wchar_t *ja;
};
}
#include "../../ui/strings.h"
#include "strings_mac.h"

// ---------------------------------------------------------------- text

static NSString *NS(const std::wstring &w) {
    return [[NSString alloc] initWithBytes:w.data() length:w.size() * sizeof(wchar_t)
                                  encoding:NSUTF32LittleEndianStringEncoding] ?: @"";
}
static NSString *NS(const std::string &s) { return [NSString stringWithUTF8String:s.c_str()] ?: @""; }
static std::wstring W(NSString *s) {
    NSData *d = [s dataUsingEncoding:NSUTF32LittleEndianStringEncoding];
    return std::wstring((const wchar_t *)d.bytes, d.length / sizeof(wchar_t));
}
static std::string U(NSString *s) { return s.UTF8String ? s.UTF8String : ""; }

static NSInteger g_lang = -1;

// The overlay's assets: SHITTIM_ASSETS, else the app's own Resources/assets (the
// distributed app: build.sh `app`), else the development layout ~/shittim-mac/assets.
static std::string AssetDir() {
    const char *env = getenv("SHITTIM_ASSETS");
    if (env && *env) return env;
    NSString *bundled = [NSBundle.mainBundle.resourcePath stringByAppendingPathComponent:@"assets"];
    BOOL isDir = NO;
    if ([NSFileManager.defaultManager fileExistsAtPath:bundled isDirectory:&isDir] && isDir) return bundled.UTF8String;
    return std::string(NSHomeDirectory().UTF8String) + "/shittim-mac/assets";
}

// host.h DefaultLogPath: where the overlay writes.
static std::string LogPath() {
    return std::string(NSHomeDirectory().UTF8String) + "/Library/Logs/ShittimLogon/lockscreen.log";
}
static bool Exists(const std::string &p) {
    return [NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithUTF8String:p.c_str()]];
}

// i18n::FromCode's idea of the system's language: 繁體 by script or region, then the
// language subtag. Anything else is English, as the Windows app's fallback is.
static NSInteger SystemLanguage() {
    for (NSString *pref in NSLocale.preferredLanguages) {
        NSDictionary *c = [NSLocale componentsFromLocaleIdentifier:pref];
        NSString *lang = c[NSLocaleLanguageCode], *script = c[NSLocaleScriptCode], *region = c[NSLocaleCountryCode];
        if ([lang isEqualToString:@"zh"]) {
            if ([script isEqualToString:@"Hant"] || [region isEqualToString:@"TW"] || [region isEqualToString:@"HK"] ||
                [region isEqualToString:@"MO"])
                return 1;
            return 0;
        }
        if ([lang isEqualToString:@"ja"]) return 3;
        if ([lang isEqualToString:@"en"]) return 2;
    }
    return 2;
}

static const wchar_t *Lookup(const std::wstring &hans) {
    static std::unordered_map<std::wstring, const i18n::Entry *> table = [] {
        std::unordered_map<std::wstring, const i18n::Entry *> t;
        for (const i18n::Entry &e : i18n::kStrings) t.emplace(e.hans, &e);
        for (const i18n::Entry &e : i18n::kStringsMac) t[e.hans] = &e;
        return t;
    }();
    const auto it = table.find(hans);
    if (it == table.end()) {
        NSLog(@"settings: no row for 「%@」 -- shown in Chinese", NS(hans));
        return nullptr;
    }
    switch (SLSettings.language) {
        case 1: return it->second->hant;
        case 2: return it->second->en;
        case 3: return it->second->ja;
        default: return it->second->hans;
    }
}

static std::wstring Tr(const wchar_t *hans) {
    const wchar_t *t = Lookup(hans);
    return t ? t : hans;
}

// The first conversion (%s, %d or %.1f) replaced by `arg`, already formatted. The rows
// these fill carry their conversions in the same order in every language (check.py
// holds them to it), so filling in order is filling the right one.
static std::wstring Fill(std::wstring f, const std::wstring &arg) {
    for (size_t i = 0; i + 1 < f.size(); i++) {
        if (f[i] != L'%') continue;
        if (f[i + 1] == L's' || f[i + 1] == L'd' || f[i + 1] == L'u') return f.replace(i, 2, arg);
        if (f.compare(i, 4, L"%llu") == 0) return f.replace(i, 4, arg);   // i18n: no
        if (f.compare(i, 4, L"%.1f") == 0) return f.replace(i, 4, arg);
        if (f[i + 1] == L'%') { f.erase(i, 1); continue; }   // a literal percent sign
    }
    return f;
}
static std::wstring Fill2(std::wstring f, const std::wstring &a, const std::wstring &b) {
    return Fill(Fill(f, a), b);
}
static std::wstring Wide(const std::string &s) { return W(NS(s)); }

#include "bridge_files.h"
#include "../../render/src/voicetext.h"

// ---------------------------------------------------------------- the model

@implementation SLSettings {
    settingsfile::File _file;
    scenes::Settings _set;
    NSInteger _revision;
    NSString *_saveError;
}

+ (SLSettings *)shared {
    static SLSettings *s = [SLSettings new];
    return s;
}

- (instancetype)init {
    if ((self = [super init])) [self reload];
    return self;
}

- (void)reload {
    macconfig::Load(nullptr);            // defaults, or the blocks an older file lacks
    _file.Load();
    _set = scenes::ParseSettings(_file.Text());
    _revision++;
}

- (NSInteger)revision { return _revision; }
- (NSString *)saveError { return _saveError; }
- (NSString *)filePath { return macconfig::Path(); }

// configwindow.h Touch: save, read back, let the page redraw.
- (void)touch {
    _saveError = _file.Save() ? nil : NS(Tr(L"无法写入配置文件，请检查文件权限"));
    _set = scenes::ParseSettings(_file.Text());
    _revision++;
}

// ---- settings

static const std::map<std::string, bool scenes::Settings::*> &Bools() {
    static const std::map<std::string, bool scenes::Settings::*> m = {
        { "clock_over_character", &scenes::Settings::clockOverCharacter },
        { "force_24_hour", &scenes::Settings::force24Hour },
        { "headpat", &scenes::Settings::headpat },
        { "eye_tracking", &scenes::Settings::eyeTracking },
        { "voice", &scenes::Settings::voice },
        { "voice_lipsync", &scenes::Settings::voiceLipsync },
        { "voice_subtitle", &scenes::Settings::voiceSubtitle },
        { "voice_subtitle_scene", &scenes::Settings::voiceSubtitleScene },
        { "voice_subtitle_character", &scenes::Settings::voiceSubtitleCharacter },
        { "voice_device_fallback", &scenes::Settings::voiceDeviceFallback },
        { "bgm", &scenes::Settings::bgm },
        { "fullscreen_fx", &scenes::Settings::fullscreenFx },
        { "credential_scene", &scenes::Settings::credentialScene },
        { "dismiss_button", &scenes::Settings::dismissButton },
        { "keep_display_on", &scenes::Settings::keepDisplayOn },
        { "keep_display_on_battery", &scenes::Settings::keepDisplayOnBattery },
    };
    return m;
}
static const std::map<std::string, float scenes::Settings::*> &Floats() {
    static const std::map<std::string, float scenes::Settings::*> m = {
        { "clock_offset_x", &scenes::Settings::clockOffsetX },
        { "clock_offset_y", &scenes::Settings::clockOffsetY },
        { "date_shift_x", &scenes::Settings::dateShiftX },
        { "date_shift_y", &scenes::Settings::dateShiftY },
        { "character_offset_x", &scenes::Settings::characterOffsetX },
        { "character_offset_y", &scenes::Settings::characterOffsetY },
        { "render_scale", &scenes::Settings::renderScale },
    };
    return m;
}
static const std::map<std::string, int scenes::Settings::*> &Ints() {
    static const std::map<std::string, int scenes::Settings::*> m = {
        { "bgm_volume", &scenes::Settings::bgmVolume },
        { "bgm_duck", &scenes::Settings::bgmDuck },
        { "voice_volume", &scenes::Settings::voiceVolume },
        { "voice_idle_interval", &scenes::Settings::voiceIdleInterval },
        { "frame_rate_limit", &scenes::Settings::frameRateLimit },
    };
    return m;
}

- (BOOL)boolFor:(NSString *)key {
    // The Mac's own switch, which scenes::Settings does not carry: read as config_mac.h
    // reads it, from the line that sets it.
    if ([key isEqualToString:@"experimental_lock_wallpaper"]) {
        const int at = _file.FindSetting("experimental_lock_wallpaper");
        if (at < 0) return NO;
        const std::string &line = _file.lines[(size_t)at];
        std::string v = scenes::trim(line.substr(line.find('=') + 1));
        for (char &c : v) c = (char)std::tolower((unsigned char)c);
        return macconfig::ParseBool(v, false);
    }
    const auto it = Bools().find(U(key));
    if (it == Bools().end()) { NSLog(@"settings: %@ is not a switch", key); return NO; }
    return _set.*(it->second);
}
- (void)setBool:(BOOL)value for:(NSString *)key {
    _file.SetBool(U(key), value);
    [self touch];
}
- (float)floatFor:(NSString *)key {
    if ([key isEqualToString:@"voice_subtitle_scale"]) return _set.voiceSubtitleStyle.scale;
    const auto it = Floats().find(U(key));
    if (it == Floats().end()) { NSLog(@"settings: %@ is not a number", key); return 0; }
    return _set.*(it->second);
}
- (void)setFloat:(float)value for:(NSString *)key {
    _file.SetFloat(U(key), value);
    [self touch];
}
- (NSInteger)intFor:(NSString *)key {
    if ([key isEqualToString:@"voice_subtitle_border_width"]) return (NSInteger)_set.voiceSubtitleStyle.borderWidth;
    if ([key isEqualToString:@"voice_subtitle_corner"]) return (NSInteger)_set.voiceSubtitleStyle.corner;
    const auto it = Ints().find(U(key));
    if (it == Ints().end()) { NSLog(@"settings: %@ is not a count", key); return 0; }
    return _set.*(it->second);
}
- (void)setInt:(NSInteger)value for:(NSString *)key {
    _file.SetSetting(U(key), std::to_string((long)value));
    [self touch];
}
- (NSString *)stringFor:(NSString *)key {
    const std::string k = U(key);
    if (k == "date_style") return NS(std::string(scenes::DateStyleText(_set.dateStyle)));
    if (k == "clock_language") return NS(std::string(scenes::ClockLanguageText(_set.clockLanguage)));
    if (k == "character_antialias")
        return _set.characterAA == scenes::CharacterAA::Auto ? @"auto" : _set.characterAA == scenes::CharacterAA::On ? @"on" : @"off";
    if (k == "clock_font") return NS(_set.clockFont);
    if (k == "voice_subtitle_font") return NS(_set.voiceSubtitleStyle.font);
    if (k == "bgm_track") return NS(_set.bgmTrack);
    if (k == "voice_pack") return NS(_set.voicePack);
    if (k == "voice_device") return NS(_set.voiceDevice);
    if (k == "voice_subtitle_language") return NS(std::string(subtitle::LanguageKey(_set.voiceSubtitleLanguage)));
    if (k == "voice_subtitle_background") return NS(subtitle::ToHex(_set.voiceSubtitleStyle.background));
    if (k == "voice_subtitle_border") return NS(subtitle::ToHex(_set.voiceSubtitleStyle.border));
    if (k == "voice_subtitle_text") return NS(subtitle::ToHex(_set.voiceSubtitleStyle.text));
    NSLog(@"settings: %@ is not a named value", key);
    return @"";
}
- (void)setString:(NSString *)value for:(NSString *)key {
    _file.SetSetting(U(key), U(value));
    [self touch];
}

// ---- the periods

- (std::vector<scenes::Period>)periods { return _file.Periods(); }

- (NSInteger)periodCount { return (NSInteger)self.periods.size(); }
- (NSArray<NSNumber *> *)periodOrder {
    NSMutableArray *out = [NSMutableArray array];
    for (size_t i : scenes::PeriodOrder(self.periods)) [out addObject:@(i)];
    return out;
}
- (NSString *)periodLabel:(NSInteger)i {
    const auto ps = self.periods;
    return i >= 0 && (size_t)i < ps.size() ? NS(scenes::PeriodLabel(ps[(size_t)i])) : @"";
}
- (NSString *)periodName:(NSInteger)i {
    const auto ps = self.periods;
    return i >= 0 && (size_t)i < ps.size() ? NS(ps[(size_t)i].name) : @"";
}
- (NSInteger)periodStart:(NSInteger)i {
    const auto ps = self.periods;
    return i >= 0 && (size_t)i < ps.size() ? ps[(size_t)i].start : 0;
}

// PeriodSpan.
- (NSString *)periodSpan:(NSInteger)i {
    const auto ps = self.periods;
    if ((size_t)i >= ps.size()) return @"";
    if (ps.size() < 2) return NS(Tr(L"全天"));
    const int end = scenes::PeriodEndOf(ps, (size_t)i);
    const std::wstring a = Wide(scenes::ClockTimeText(ps[(size_t)i].start)), b = Wide(scenes::ClockTimeText(end));
    return NS(Fill2(Tr(end <= ps[(size_t)i].start ? L"%s 至次日 %s" : L"%s 至 %s"), a, b));
}

- (BOOL)isCurrentPeriod:(NSInteger)i {
    const auto ps = self.periods;
    return ps.size() >= 2 && scenes::PeriodAt(ps, macconfig::MinuteNow()) == (int)i;
}

// TrackLabel / MusicLabel: a track's title from its pack header, as the Windows app
// reads it (bgmpack.h's file names, the imported one first).
// bgmpack.h's names: an imported track (settings directory) before a shipped one.
static std::string UserTrack(const std::string &id) { return std::string(macconfig::Dir().UTF8String) + "/bgm-" + id + ".pak"; }
static std::string FactoryTrack(const std::string &id) {
    return AssetDir() + (id.empty() || id == scenes::kBgmTrackFactory ? std::string("/bgm.pak") : "/bgm-" + id + ".pak");
}
struct TrackInfo { bool ok = false, imported = false; std::string title; float seconds = 0; double bytes = 0; };
static TrackInfo ReadTrack(const std::string &id) {
    TrackInfo t;
    for (const std::string &p : { UserTrack(id), FactoryTrack(id) }) {
        FILE *f = std::fopen(p.c_str(), "rb");
        if (!f) continue;
        uint32_t h[8] = {};
        char title[128] = {};
        const bool ok = std::fread(h, 4, 8, f) == 8 && std::fread(title, 1, 128, f) == 128 && h[0] == 0x47424C53u;
        std::fseek(f, 0, SEEK_END);
        t.bytes = (double)std::ftell(f);
        std::fclose(f);
        if (!ok) continue;
        title[127] = 0;
        t.ok = true;
        t.imported = p == UserTrack(id);
        t.title = title;
        t.seconds = h[2] ? (float)h[4] / (float)h[2] : 0.0f;    // frames / rate
        return t;
    }
    return t;
}
static std::wstring TrackLabel(const std::string &id) {
    if (id.empty()) return Tr(L"默认曲目");
    const TrackInfo t = ReadTrack(id);
    return t.ok && !t.title.empty() ? Wide(t.title) : Wide(id) + Tr(L"（缺失）");
}
static std::wstring TrackLength(float seconds) {
    const int total = (int)(seconds + 0.5f);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%d:%02d", total / 60, total % 60);
    return Wide(buf);
}
static std::wstring TrackDetail(const std::string &id) {
    const TrackInfo t = ReadTrack(id);
    if (!t.ok) return Tr(Exists(UserTrack(id)) ? L"文件无法读取，可移除后重新导入" : L"此安装中没有该曲目");
    if (t.imported) {
        char mb[32];
        std::snprintf(mb, sizeof mb, "%.1f", t.bytes / (1024.0 * 1024.0));
        return Fill(Fill(Tr(L"%s · %.1f MB · 已导入"), TrackLength(t.seconds)), Wide(mb));
    }
    return Fill(Tr(L"%s · 随软件安装"), TrackLength(t.seconds));
}
// bgmpack::FactoryIds and UserIds: `factory` first, then bgm-<id>.pak by name.
static std::vector<std::string> IdsIn(const std::string &dir) {
    std::vector<std::string> out;
    NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:[NSString stringWithUTF8String:dir.c_str()]
                                                                        error:nil] sortedArrayUsingSelector:@selector(compare:)];
    for (NSString *n in names)
        if ([n hasPrefix:@"bgm-"] && [n hasSuffix:@".pak"] && n.length > 8)
            out.push_back(U([n substringWithRange:NSMakeRange(4, n.length - 8)]));
    return out;
}
static std::vector<std::string> AllTrackIds(const std::vector<std::string> &keep) {
    std::vector<std::string> ids;
    if (Exists(AssetDir() + "/bgm.pak")) ids.push_back(scenes::kBgmTrackFactory);
    const auto add = [&ids](const std::string &id) {
        if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    };
    for (const std::string &id : IdsIn(AssetDir())) if (id != "custom") add(id);
    for (const std::string &id : IdsIn(macconfig::Dir().UTF8String)) add(id);
    for (const std::string &id : keep) add(id);
    return ids;
}
// WithTrack: `list` with `id` in or out, in AllTrackIds' order.
static std::vector<std::string> WithTrack(const std::vector<std::string> &list, const std::string &id, bool on) {
    std::vector<std::string> out;
    for (const std::string &x : AllTrackIds(list))
        if (x == id ? on : std::find(list.begin(), list.end(), x) != list.end()) out.push_back(x);
    return out;
}
static std::wstring MusicLabel(const std::string &list) {
    const std::vector<std::string> ids = scenes::TrackList(list);
    if (ids.empty()) return Tr(L"默认曲目");
    if (ids.size() == 1) return TrackLabel(ids[0]);
    return Fill(Tr(L"%d 首随机播放"), std::to_wstring(ids.size()));
}

// PeriodSpan, of any list of periods (the file's, or the bar's while a boundary is held).
static std::wstring SpanOf(const std::vector<scenes::Period> &ps, size_t i) {
    if (i >= ps.size()) return std::wstring();
    if (ps.size() < 2) return Tr(L"全天");
    const int end = scenes::PeriodEndOf(ps, i);
    const std::wstring a = Wide(scenes::ClockTimeText(ps[i].start)), b = Wide(scenes::ClockTimeText(end));
    return Fill2(Tr(end <= ps[i].start ? L"%s 至次日 %s" : L"%s 至 %s"), a, b);
}

// PeriodSummary.
static std::wstring SummaryOf(const std::vector<scenes::Period> &ps, size_t i) {
    if (i >= ps.size()) return std::wstring();
    int on = 0;
    for (const scenes::Entry &e : ps[i].scenes)
        if (e.active && scenes::Resolve(e.name).ok) on++;
    std::wstring s;
    if (ps.size() >= 2 && scenes::PeriodAt(ps, macconfig::MinuteNow()) == (int)i) s = Tr(L"当前时段 · ");
    s += SpanOf(ps, i) + L" · ";
    s += on ? W([SLSettings tr:@"%d 个场景" count:on]) : Tr(L"未启用场景");
    return s + L" · " + MusicLabel(ps[i].music);
}
- (NSString *)periodSummary:(NSInteger)i { return i < 0 ? @"" : NS(SummaryOf(self.periods, (size_t)i)); }
- (NSString *)periodSummary:(NSInteger)i withStart:(NSInteger)minute of:(NSInteger)held {
    auto ps = self.periods;
    if (held >= 0 && (size_t)held < ps.size()) ps[(size_t)held].start = (int)minute;
    return i < 0 ? @"" : NS(SummaryOf(ps, (size_t)i));
}
- (NSInteger)clampStart:(NSInteger)minute at:(NSInteger)i {
    const auto ps = self.periods;
    return (size_t)i < ps.size() ? scenes::ClampStart(ps, (size_t)i, (int)minute) : minute;
}
- (NSInteger)minuteNow { return macconfig::MinuteNow(); }

// ---- BuildPeriodMusic

- (BOOL)periodUsesDefaultMusic:(NSInteger)i {
    const auto ps = self.periods;
    return (size_t)i >= ps.size() || scenes::TrackList(ps[(size_t)i].music).empty();
}
// Off starts from the default's own list, so turning it off changes nothing anybody
// hears until a box is ticked or cleared.
- (void)setPeriodUsesDefaultMusic:(BOOL)on at:(NSInteger)i {
    _file.SetMusicIn((size_t)i, on ? std::string() : scenes::JoinTracks(scenes::TrackList(_set.bgmTrack)));
    [self touch];
}
- (NSString *)defaultMusicLabel { return NS(Tr(L"开启时使用「声音」页中设置的曲目：") + MusicLabel(_set.bgmTrack)); }
- (NSArray<NSArray<NSString *> *> *)tracksForPeriod:(NSInteger)i {
    const auto ps = self.periods;
    NSMutableArray *out = [NSMutableArray array];
    if ((size_t)i >= ps.size()) return out;
    for (const std::string &id : AllTrackIds(scenes::TrackList(ps[(size_t)i].music)))
        [out addObject:@[ NS(id), NS(TrackLabel(id)), NS(TrackDetail(id)) ]];
    return out;
}
- (BOOL)track:(NSString *)track inPeriod:(NSInteger)i {
    const auto ps = self.periods;
    if ((size_t)i >= ps.size()) return NO;
    const auto list = scenes::TrackList(ps[(size_t)i].music);
    return std::find(list.begin(), list.end(), U(track)) != list.end();
}
- (BOOL)setTrack:(NSString *)track inPeriod:(NSInteger)i on:(BOOL)on {
    const auto ps = self.periods;
    if ((size_t)i >= ps.size()) return NO;
    const auto now = WithTrack(scenes::TrackList(ps[(size_t)i].music), U(track), on);
    if (now.empty()) return NO;
    _file.SetMusicIn((size_t)i, scenes::JoinTracks(now));
    [self touch];
    return YES;
}

// ---- the subtitle preview

// configwindow.h kPreviewClip: character A's, and one of the longer lines.
- (NSString *)previewLine {
    const voice::Clip *c = voice::FindClip("character_a_line_4");
    return c ? NS(std::string(voice::SubtitleText(*c, _set.voiceSubtitleLanguage))) : @"";
}
- (NSArray<NSNumber *> *)previewGeometry:(float)scale {
    const std::string line = U(self.previewLine);
    const float sc = std::min(scale, subtitle::ScaleMax(_set.voiceSubtitleLanguage));
    const subtitle::Solved s = subtitle::Solve(line.c_str(), 1080, &subtitle::Estimate, nullptr, sc);
    const float pw = (float)s.panelW;
    return @[ @(s.panelW), @(s.panelH), @(s.fontPx),
              @(pw * (subtitle::kMarginXFrac / subtitle::kPanelWidthFrac)),
              @(pw * (subtitle::kMarginYFrac / subtitle::kPanelWidthFrac)),
              @(s.fontPx * subtitle::kLineHeightRatio) ];
}
- (float)scaleMax { return subtitle::ScaleMax(_set.voiceSubtitleLanguage); }
- (NSString *)previewFontName {
    const subtitle::Language lang = _set.voiceSubtitleLanguage;
    const std::string factory = AssetDir() + (subtitle::FactoryFaceFor(lang) == subtitle::FactoryFace::Clock
                                                  ? "/LINESeedKR-Bd.ttf" : "/subtitle-font.ttf");
    return PreviewFace(_set.voiceSubtitleStyle.font, factory, FontSlot(false));
}

- (NSInteger)periodPrev:(NSInteger)i { return (NSInteger)scenes::PeriodPrev(self.periods, (size_t)i); }
- (NSInteger)periodNext:(NSInteger)i { return (NSInteger)scenes::PeriodNext(self.periods, (size_t)i); }
- (NSArray<NSNumber *> *)startChoices:(NSInteger)which {
    NSMutableArray *out = [NSMutableArray array];
    for (int m : scenes::StartChoices(self.periods, (size_t)which)) [out addObject:@(m)];
    return out;
}
- (void)setPeriodStart:(NSInteger)minute at:(NSInteger)i { _file.SetPeriodStart((size_t)i, (int)minute); [self touch]; }

// BuildPeriod's name box: at most kPeriodNameMax characters, unchanged text not saved.
- (void)setPeriodName:(NSString *)name at:(NSInteger)i {
    static const size_t kPeriodNameMax = 12;     // configwindow.h's
    std::wstring w = W(name);
    if (w.size() > kPeriodNameMax) w.resize(kPeriodNameMax);
    const std::string before = _file.Text();
    _file.SetPeriodName((size_t)i, U(NS(w)));
    if (_file.Text() == before) return;
    [self touch];
}
- (BOOL)scene:(NSString *)scene inPeriod:(NSInteger)i {
    const auto ps = self.periods;
    if ((size_t)i >= ps.size()) return NO;
    for (const scenes::Entry &x : ps[(size_t)i].scenes)
        if (x.name == U(scene)) return x.active;
    return NO;
}
- (void)setScene:(NSString *)scene inPeriod:(NSInteger)i active:(BOOL)on {
    _file.SetSceneIn((size_t)i, U(scene), on);
    [self touch];
}
- (BOOL)canAddPeriod { return scenes::SplitPoint(self.periods) >= 0; }

// AddPeriodSplit: the longest period halved, the new one taking its scenes and music.
- (void)addPeriod {
    const auto ps = self.periods;
    size_t split = 0;
    const int at = scenes::SplitPoint(ps, &split);
    if (at < 0) return;
    const size_t idx = _file.AddPeriod(at, std::string());
    for (const scenes::Entry &e : ps[split].scenes)
        if (e.active) _file.SetSceneIn(idx, e.name, true);
    if (!ps[split].music.empty()) _file.SetMusicIn(idx, ps[split].music);
    [self touch];
}
- (void)removePeriod:(NSInteger)i { _file.RemovePeriod((size_t)i); [self touch]; }

// ---- the scenes

- (NSArray<NSString *> *)sceneNames {
    NSMutableArray *out = [NSMutableArray array];
    for (const scenes::Entryv &e : settingsfile::All()) [out addObject:NS(std::string(e.name))];
    return out;
}

// ThumbPath: the shipped thumbnail, `thumb-<scene>.png` in the assets.
- (NSString *)thumbnailFor:(NSString *)scene {
    const std::string p = AssetDir() + "/thumb-" + U(scene) + ".png";
    return Exists(p) ? NS(p) : nil;
}

// ---- sound

- (NSArray<NSArray<NSString *> *> *)outputDevices {
    NSMutableArray *out = [NSMutableArray array];
    for (const macaudio::Device &d : macaudio::Outputs()) [out addObject:@[ NS(d.uid), NS(d.name) ]];
    return out;
}

// OutputRow, with macaudio::Resolve's rule behind each sentence.
- (NSString *)outputStatus:(NSInteger *)state {
    *state = 0;
    if (!_set.voice && !_set.bgm) { *state = 1; return NS(Tr(L"语音与背景音乐均已关闭")); }
    if (_set.voiceDevice.empty()) return NS(Tr(L"跟随系统默认设备"));
    for (const macaudio::Device &d : macaudio::Outputs())
        if (d.uid == _set.voiceDevice) return NS(Fill(Tr(L"已指定「%s」"), Wide(d.name)));
    if (_set.voiceDeviceFallback) return NS(Tr(L"所选设备已不存在，改用系统默认设备"));
    *state = 2;
    return NS(Tr(L"所选设备已不存在，锁定屏幕保持静音"));
}

// AddVoicePack's list: `factory` (assets/voice) and every assets/voice-<id>, named by
// the language it speaks where that is known (the Windows packs' titles).
- (NSArray<NSArray<NSString *> *> *)voicePacks {
    NSMutableArray *out = [NSMutableArray array];
    auto label = [](const std::string &id) -> std::wstring {
        if (id == scenes::kVoicePackFactory) return Tr(L"日语");
        if (id == "ko") return Tr(L"韩语");
        return Wide(id);
    };
    if (Exists(AssetDir() + "/voice")) [out addObject:@[ @"factory", NS(label(scenes::kVoicePackFactory)) ]];
    NSArray *names = [[NSFileManager.defaultManager contentsOfDirectoryAtPath:NS(AssetDir()) error:nil]
        sortedArrayUsingSelector:@selector(compare:)];
    bool listed = _set.voicePack == scenes::kVoicePackFactory || scenes::VoicePackIsCustom(_set.voicePack);
    for (NSString *n in names) {
        BOOL dir = NO;
        if (![n hasPrefix:@"voice-"] || n.length <= 6) continue;
        if (![NSFileManager.defaultManager fileExistsAtPath:NS(AssetDir() + "/" + U(n)) isDirectory:&dir] || !dir) continue;
        const std::string id = U([n substringFromIndex:6]);
        listed = listed || id == _set.voicePack;
        [out addObject:@[ NS(id), NS(label(id)) ]];
    }
    if (!listed) [out addObject:@[ NS(_set.voicePack), NS(Wide(_set.voicePack) + Tr(L"（缺失）")) ]];
    return out;
}

- (NSArray<NSArray<NSString *> *> *)tracks {
    NSMutableArray *out = [NSMutableArray array];
    for (const std::string &id : AllTrackIds(scenes::TrackList(_set.bgmTrack)))
        [out addObject:@[ NS(id), NS(TrackLabel(id)), NS(TrackDetail(id)) ]];
    return out;
}
- (BOOL)isDefaultTrack:(NSString *)track {
    const auto list = scenes::TrackList(_set.bgmTrack);
    return std::find(list.begin(), list.end(), U(track)) != list.end();
}
// SetDefaultTrack / WithTrack: the list in AllTrackIds' order, never empty.
- (BOOL)setDefaultTrack:(NSString *)track on:(BOOL)on {
    const auto list = scenes::TrackList(_set.bgmTrack);
    std::vector<std::string> now;
    for (const std::string &x : AllTrackIds(list))
        if (x == U(track) ? (bool)on : std::find(list.begin(), list.end(), x) != list.end()) now.push_back(x);
    if (now.empty()) return NO;
    _file.SetSetting("bgm_track", scenes::JoinTracks(now));
    [self touch];
    return YES;
}

- (BOOL)isImportedTrack:(NSString *)track { return Exists(UserTrack(U(track))); }

// ---- files (bridge_files.h)

// ImportTrack. **Not selected automatically**: a track is chosen per period, and picking
// one on the person's behalf would be picking a period too.
- (NSString *)importTrack:(NSString *)path {
    std::vector<std::string> taken = IdsIn(AssetDir());
    taken.push_back(scenes::kBgmTrackFactory);
    for (const std::string &id : IdsIn(DataDir())) taken.push_back(id);
    const std::string id = IdFor(path, taken);
    if (id.empty()) return NS(Tr(L"无法为此文件生成名称，请先移除部分已导入的曲目"));
    const TrackImport r = ImportTrackFile(path, UserTrack(id));
    if (!r.ok) return NS(r.problem);
    _revision++;
    return NS(Fill(Fill(Fill(Tr(L"已导入「%s」（%s，%.1f MB），可在各时段中选择"), r.title), TrackLength((float)r.seconds)),
                   MegaBytes(r.bytes)));
}

// RemoveTrack: references first, then the file. A default that loses its last track goes
// back to the one that shipped; a period that loses its last goes back to the default.
- (NSString *)removeTrack:(NSString *)track {
    const std::string id = U(track);
    if (!IdIsSafe(id) || std::remove(UserTrack(id).c_str()) != 0)
        return NS(Tr(L"无法移除：该文件可能正被锁定屏幕使用，请锁定一次屏幕后重试"));
    std::vector<std::string> def = scenes::TrackList(_set.bgmTrack);
    if (std::find(def.begin(), def.end(), id) != def.end()) {
        def.erase(std::remove(def.begin(), def.end(), id), def.end());
        _file.SetSetting("bgm_track", def.empty() ? std::string(scenes::kBgmTrackFactory) : scenes::JoinTracks(def));
    }
    const auto ps = self.periods;
    for (size_t i = 0; i < ps.size(); i++) {
        std::vector<std::string> list = scenes::TrackList(ps[i].music);
        if (std::find(list.begin(), list.end(), id) == list.end()) continue;
        list.erase(std::remove(list.begin(), list.end(), id), list.end());
        _file.SetMusicIn(i, scenes::JoinTracks(list));
    }
    [self touch];
    return NS(Tr(L"已移除该曲目"));
}

// ImportedPackStatus.
- (NSString *)voicePackStatus {
    const VoicePackInfo v = ReadVoiceSlot();
    if (!v.ok) return NS(Tr(L"尚未导入，点击「浏览…」选择语音包文件"));
    // PackTitle: the two that ship are titled in Chinese and shown in this window's language.
    std::wstring title = v.title;
    for (const wchar_t *k : { L"日语", L"韩语" })
        if (title == k) title = Tr(k);
    return NS(Fill(Fill(Fill(Tr(L"%s · %u 段 · %s"), title.empty() ? Tr(L"未命名语音包") : title),
                        std::to_wstring(v.clips)), TrackLength((float)v.seconds)));
}
- (BOOL)hasImportedVoicePack { return ReadVoiceSlot().ok; }

- (NSString *)importVoicePack:(NSString *)path {
    const VoiceImport r = ImportVoiceFile(path);
    if (!r.ok) return NS(r.problem);
    _revision++;
    std::wstring title = r.title;
    for (const wchar_t *k : { L"日语", L"韩语" })
        if (title == k) title = Tr(k);
    if (r.broken)
        return NS(Fill(Fill(Fill(Tr(L"已导入「%s」（%d 段），其中 %d 段无法解码，对应台词不会播放"), title),
                            std::to_wstring(r.clips)), std::to_wstring(r.broken)));
    return NS(Fill(Fill(Fill(Tr(L"已导入「%s」（%d 段，%.1f MB）"), title), std::to_wstring(r.clips)), MegaBytes(r.bytes)));
}

// ClearVoicePack: the slot goes, and the setting moves back to the pack that shipped.
- (NSString *)clearVoicePack {
    NSError *e = nil;
    if (Exists(VoiceSlot()) && ![NSFileManager.defaultManager removeItemAtPath:NS(VoiceSlot()) error:&e])
        return NS(Tr(L"无法移除：该文件正被锁定屏幕使用，请锁定一次屏幕后重试"));
    _file.SetSetting("voice_pack", scenes::kVoicePackFactory);
    [self touch];
    return NS(Tr(L"已移除导入的语音包，改用出厂语音包"));
}

- (NSString *)fontStatus:(BOOL)clock {
    if (clock) return NS(FontStatusText(_set.clockFont, AssetDir() + "/LINESeedKR-Bd.ttf", FontSlot(true)));
    const subtitle::Language lang = _set.voiceSubtitleLanguage;
    const std::string factory = AssetDir() + (subtitle::FactoryFaceFor(lang) == subtitle::FactoryFace::Clock
                                                  ? "/LINESeedKR-Bd.ttf" : "/subtitle-font.ttf");
    return NS(FontStatusText(_set.voiceSubtitleStyle.font, factory, FontSlot(false)));
}
- (NSString *)fontCopyStatus:(BOOL)clock { return NS(FontCopyText(FontSlot(clock))); }
- (BOOL)hasImportedFont:(BOOL)clock { return InspectFont(FontSlot(clock)).loads; }

// ImportFont / ImportClockFont: choosing a file is choosing to use it.
- (NSString *)importFont:(NSString *)path clock:(BOOL)clock {
    const FontImport r = ImportFontFile(path, FontSlot(clock));
    if (!r.ok) return NS(r.problem);
    _file.SetSetting(clock ? "clock_font" : "voice_subtitle_font", subtitle::kFontCustom);
    [self touch];
    return NS(Fill(Fill(Tr(clock ? L"已导入时钟字体「%s」（%.1f MB）" : L"已导入字体「%s」（%.1f MB）"), r.family),
                   MegaBytes(r.bytes)));
}

// ClearFont / ClearClockFont: the slot emptied, the setting back to the factory face.
- (NSString *)clearFont:(BOOL)clock {
    const std::string slot = FontSlot(clock);
    if (Exists(slot) && std::remove(slot.c_str()) != 0) return NS(Tr(L"写入字体文件失败，请重新选择文件。"));
    _file.SetSetting(clock ? "clock_font" : "voice_subtitle_font", subtitle::kFontFactory);
    [self touch];
    return NS(Tr(clock ? L"已移除自定义时钟字体，改用出厂字体" : L"已移除自定义字体，改用出厂字体"));
}

// ExportLogs and ExportReport, with what the Mac has to report.
// wallpaper_mac.h's recovery, for uninstall: an overlay that died during a lock with
// the experimental wallpaper on left the room as the desktop picture, and no overlay
// will start again to put it back.
- (void)restoreWallpaperIfLeft {
    macwall::Swap::RecoverIfNeeded(nullptr);
}

- (NSString *)exportLogs:(NSString **)path {
    if (path) *path = nil;
    NSDateFormatter *df = [NSDateFormatter new];
    df.dateFormat = @"yyyy-MM-dd HH:mm:ss";
    df.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    const NSOperatingSystemVersion os = NSProcessInfo.processInfo.operatingSystemVersion;
    std::wstring s = Tr(L"Shittim Logon 日志导出\r\n\r\n");
    s += Tr(L"导出时间：") + W([df stringFromDate:NSDate.date]) + L"\r\n";
    s += Tr(L"数据目录：") + Wide(DataDir()) + L"\r\n";
    s += Tr(L"macOS 版本：") + W([NSString stringWithFormat:@"%ld.%ld.%ld", (long)os.majorVersion,
                                                         (long)os.minorVersion, (long)os.patchVersion]) + L"\r\n";
    s += Tr(L"显示器：");
    NSArray<NSScreen *> *screens = NSScreen.screens;
    for (NSUInteger i = 0; i < screens.count; i++) {
        if (i) s += Tr(L"；");
        const NSSize px = [screens[i] convertRectToBacking:screens[i].frame].size;
        s += W(screens[i].localizedName) + L" " + std::to_wstring((int)px.width) + L"x" + std::to_wstring((int)px.height) +   // i18n: no
             (i == 0 ? Tr(L"（主）") : L"");
    }
    if (screens.count == 0) s += Tr(L"（无法枚举）");
    s += Tr(L"\r\n\r\n诊断\r\n");
    NSInteger state = 0;
    s += L"  " + Tr(L"输出设备") + Tr(L"：") + W([self outputStatus:&state]) + L"\r\n";
    s += Tr(L"\r\n同一压缩包内：settings.txt（当前设置），logs 目录下的日志。\r\n");
    s += Tr(L"超出大小上限的日志只保留末尾，该文件首行注明。\r\n");
    const LogExport r = ExportLogFiles(LogPath(),
                                       U(self.filePath), U(NS(s)));
    if (!r.ok) return NS(r.why);
    if (path) *path = NS(r.path);
    std::wstring said = Tr(L"已导出到「下载」，") + Bytes(r.bytes);
    if (r.truncated) said += Tr(L"；过大的日志只保留末尾");
    return NS(said);
}

// ---- the clock

// ClockLanguageStatus, with macOS in Windows' place.
- (NSString *)clockLanguageStatus {
    NSString *own = NSLocale.autoupdatingCurrentLocale.localeIdentifier;
    if (_set.clockLanguage == scenes::ClockLanguage::Auto) {
        static NSString *const kUi[] = { @"zh-Hans", @"zh-Hant", @"en", @"ja" };
        NSLocale *ui = [NSLocale localeWithLocaleIdentifier:kUi[std::max<NSInteger>(0, std::min<NSInteger>(3, SLSettings.language))]];
        NSString *name = [ui localizedStringForLocaleIdentifier:own] ?: NS(Tr(L"无法确定"));
        return NS(Fill(Tr(L"跟随 macOS 的区域格式，当前为「%s」"), W(name)));
    }
    if (macui::Speaks(own, _set.clockLanguage)) return NS(Tr(L"与 macOS 当前区域格式的语言相同，按该区域格式显示"));
    return NS(Tr(L"时间与日期按该语言的标准格式显示，不再使用区域格式中的自定义格式"));
}

// DateExample: today's date as the lock screen will write it -- macui::Format itself.
- (NSString *)dateExample {
    NSString *locale = NSLocale.autoupdatingCurrentLocale.localeIdentifier;
    if (NSString *canon = macui::CanonicalLocale(_set.clockLanguage))
        if (!macui::Speaks(locale, _set.clockLanguage)) locale = canon;
    if (_set.dateStyle != scenes::DateStyle::Windows && !macui::DatePicture(locale, _set.dateStyle, true))
        return NS(Tr(L"当前语言不适用此项，按 macOS 长日期格式显示；可在「语言」中选择"));
    NSString *t = nil, *d = nil;
    macui::Format(NSDate.date, false, _set.dateStyle, _set.clockLanguage, &t, &d, nullptr);
    return NS(Fill(Tr(L"当前显示为「%s」"), W(d)));
}

// ---- strings

+ (NSInteger)language {
    if (g_lang < 0) {
        NSString *saved = [NSUserDefaults.standardUserDefaults stringForKey:@"language"];
        const NSInteger codes = [@[ @"zh-Hans", @"zh-Hant", @"en", @"ja" ] indexOfObject:saved ?: @""];
        g_lang = codes != NSNotFound ? codes : SystemLanguage();
    }
    return g_lang;
}
+ (void)setLanguage:(NSInteger)l {
    g_lang = std::max<NSInteger>(0, std::min<NSInteger>(3, l));
    [NSUserDefaults.standardUserDefaults setObject:@[ @"zh-Hans", @"zh-Hant", @"en", @"ja" ][g_lang] forKey:@"language"];
}
+ (void)useLanguage:(NSInteger)l { g_lang = std::max<NSInteger>(0, std::min<NSInteger>(3, l)); }
+ (NSString *)tr:(NSString *)hans { return NS(Tr(W(hans).c_str())); }
+ (NSString *)tr:(NSString *)hans with:(NSString *)arg { return NS(Fill(Tr(W(hans).c_str()), W(arg))); }
+ (NSString *)tr:(NSString *)hans count:(NSInteger)n {
    std::wstring f = Tr(W(hans).c_str());
    const size_t bar = f.find(L'|');
    if (bar != std::wstring::npos) f = n == 1 ? f.substr(0, bar) : f.substr(bar + 1);
    return NS(Fill(f, std::to_wstring((long)n)));
}
+ (NSString *)clockText:(NSInteger)minute { return NS(scenes::ClockTimeText((int)minute)); }

@end
