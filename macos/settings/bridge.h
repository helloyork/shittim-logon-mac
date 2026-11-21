// shittim-logon / macos / settings / bridge.h
//
// What the SwiftUI settings app sees of the shared C++: the settings file, edited line
// by line by config/src/settingsfile.h and read back through scenes::ParseSettings
// (so every value means what it means on Windows), the day's periods, the clock's
// example text (macui::Format, the overlay's own), and the four-language strings
// (ui/strings.h, then strings_mac.h).
//
// Plain Objective-C, because Swift imports it through the bridging header. Every
// setter saves at once, as the Windows app's Touch does; `revision` moves on every
// change so SwiftUI knows to read again.

#pragma once

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface SLSettings : NSObject

+ (SLSettings *)shared;

/// Re-reads the file (writing the defaults, or the blocks an older file lacks, first).
- (void)reload;
@property (readonly) NSInteger revision;
/// The last save's failure, in the interface language; nil when it worked.
@property (readonly, nullable) NSString *saveError;
@property (readonly) NSString *filePath;

// ---- settings, by their key in the file

- (BOOL)boolFor:(NSString *)key;
- (void)setBool:(BOOL)value for:(NSString *)key;
- (float)floatFor:(NSString *)key;
- (void)setFloat:(float)value for:(NSString *)key;
- (NSInteger)intFor:(NSString *)key;
- (void)setInt:(NSInteger)value for:(NSString *)key;
/// Enumerations and names, spelled as the Windows app writes them (`date_style` is
/// windows / full / compact, `clock_language` auto / zh-CN / ..., `character_antialias`
/// auto / on / off, fonts factory / system / custom / a family).
- (NSString *)stringFor:(NSString *)key;
- (void)setString:(NSString *)value for:(NSString *)key;

// ---- the periods

- (NSInteger)periodCount;
/// Indices in the order of the day, starting from the earliest.
- (NSArray<NSNumber *> *)periodOrder;
- (NSString *)periodLabel:(NSInteger)i;        // its name, or its start time
- (NSString *)periodName:(NSInteger)i;         // the name as written, possibly empty
- (NSInteger)periodStart:(NSInteger)i;         // minutes past midnight
- (NSString *)periodSpan:(NSInteger)i;         // 「06:00 至 18:00」
- (NSString *)periodSummary:(NSInteger)i;      // the row's description, as on Windows
- (BOOL)isCurrentPeriod:(NSInteger)i;
- (NSInteger)periodPrev:(NSInteger)i;
- (NSInteger)periodNext:(NSInteger)i;
- (NSArray<NSNumber *> *)startChoices:(NSInteger)which;
- (void)setPeriodStart:(NSInteger)minute at:(NSInteger)i;
- (void)setPeriodName:(NSString *)name at:(NSInteger)i;
- (BOOL)scene:(NSString *)scene inPeriod:(NSInteger)i;
- (void)setScene:(NSString *)scene inPeriod:(NSInteger)i active:(BOOL)on;
- (BOOL)canAddPeriod;
- (void)addPeriod;
- (void)removePeriod:(NSInteger)i;

// The period bar (periodbar.h): where a dragged boundary may go, and the row
// descriptions as they would read with it there -- the file is written on release.
- (NSInteger)clampStart:(NSInteger)minute at:(NSInteger)i;
- (NSString *)periodSummary:(NSInteger)i withStart:(NSInteger)minute of:(NSInteger)held;
- (NSInteger)minuteNow;

// BuildPeriodMusic: a period plays the default tracks, or its own list.
- (BOOL)periodUsesDefaultMusic:(NSInteger)i;
- (void)setPeriodUsesDefaultMusic:(BOOL)on at:(NSInteger)i;
- (NSString *)defaultMusicLabel;
/// Every track, as [id, title, detail], with the ones this period names that are gone.
- (NSArray<NSArray<NSString *> *> *)tracksForPeriod:(NSInteger)i;
- (BOOL)track:(NSString *)track inPeriod:(NSInteger)i;
/// SetTrackInPeriod: NO, and nothing written, when it would leave the period no track.
- (BOOL)setTrack:(NSString *)track inPeriod:(NSInteger)i on:(BOOL)on;

// ---- the scenes

- (NSArray<NSString *> *)sceneNames;
- (nullable NSString *)thumbnailFor:(NSString *)scene;

// ---- sound

/// Every output device connected now, as [uid, name] (audiodev_mac.h).
- (NSArray<NSArray<NSString *> *> *)outputDevices;
/// configwindow.h OutputRow's sentence; `state` 0 fine, 1 not applicable, 2 needs attention.
- (NSString *)outputStatus:(NSInteger *)state;
/// The voice packs in the assets, as [id, label].
- (NSArray<NSArray<NSString *> *> *)voicePacks;
/// Every track there is, as [id, title, detail] (AllTrackIds, TrackLabel, TrackDetail).
- (NSArray<NSArray<NSString *> *> *)tracks;
- (BOOL)isDefaultTrack:(NSString *)track;
/// SetDefaultTrack: NO, and nothing written, when it would leave no default track.
- (BOOL)setDefaultTrack:(NSString *)track on:(BOOL)on;
- (BOOL)isImportedTrack:(NSString *)track;

// ---- files this app puts where the overlay reads them (bridge_files.h). Each returns
// the sentence the Windows app shows in its toast, in the interface language.

/// ImportTrack: decoded to 48 kHz stereo PCM, silence trimmed, written as bgm-<id>.pak.
- (NSString *)importTrack:(NSString *)path;
/// RemoveTrack: the file, and every list that names it.
- (NSString *)removeTrack:(NSString *)track;
/// The 语音包文件 row: what the imported pack is, or that there is none.
- (NSString *)voicePackStatus;
- (BOOL)hasImportedVoicePack;
/// ImportVoicePack: a .slvp, its clips written as files into voice-custom/.
- (NSString *)importVoicePack:(NSString *)path;
- (NSString *)clearVoicePack;
/// FontStatusFor and FontCopyStatus, for the clock (`clock`) or the subtitle.
- (NSString *)fontStatus:(BOOL)clock;
- (NSString *)fontCopyStatus:(BOOL)clock;
- (BOOL)hasImportedFont:(BOOL)clock;
- (NSString *)importFont:(NSString *)path clock:(BOOL)clock;
- (NSString *)clearFont:(BOOL)clock;
/// ExportLogs: a zip in Downloads. `path` is where it went, nil when it failed.
- (NSString *)exportLogs:(NSString *_Nullable *_Nullable)path;

/// Uninstall: puts back a desktop picture an overlay left behind (wallpaper_mac.h).
- (void)restoreWallpaperIfLeft;

// ---- the subtitle preview (PaintSubtitlePreview)

/// One of the character's real lines, in the subtitle language.
- (NSString *)previewLine;
/// subtitle::Solve at 1080 px for `scale`: [panelW, panelH, fontPx, marginX, marginY, lineHeight].
- (NSArray<NSNumber *> *)previewGeometry:(float)scale;
/// The largest 字号 the subtitle language allows (subtitle::ScaleMax).
- (float)scaleMax;
/// The face the overlay will use, registered for this process: its PostScript name, or
/// nil for the system font.
- (nullable NSString *)previewFontName;

// ---- the clock

- (NSString *)clockLanguageStatus;
- (NSString *)dateExample;

// ---- strings

/// 0 简体中文, 1 繁體中文, 2 English, 3 日本語 (i18n::Lang's numbers).
@property (class) NSInteger language;
/// For this process only, not saved: the offscreen shots (setting `language` saves it).
+ (void)useLanguage:(NSInteger)l;
+ (NSString *)tr:(NSString *)hans;
/// A row whose text carries one %s (or %d), filled.
+ (NSString *)tr:(NSString *)hans with:(NSString *)arg;
/// fmtn: "%d 个场景", with the English singular|plural split.
+ (NSString *)tr:(NSString *)hans count:(NSInteger)n;
+ (NSString *)clockText:(NSInteger)minute;    // 「06:00」

@end

NS_ASSUME_NONNULL_END
