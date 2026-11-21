// shittim-logon / macos / src / clocktext_mac.h
//
// The clock's text, localeclock.h's Format decision for decision -- see the header of
// ui_mac.h for the rules. On its own so the settings app's date example can include it
// without the overlay's drawing code: the example is the same function, not a copy.

#pragma once

#import <Foundation/Foundation.h>

#include "../../render/src/scenes.h"

#include <cctype>
#include <string>

namespace macui {

// ---------------------------------------------------------------- the clock text
//
// localeclock.h, with ICU patterns where Windows has its own pictures (dddd -> EEEE).

inline std::string LanguageOf(NSString *locale) {
    std::string lang;
    for (const char *c = locale.UTF8String; *c && *c != '-' && *c != '_'; c++)
        lang.push_back((char)std::tolower((unsigned char)*c));
    return lang;
}

// Whether `locale` writes Chinese in traditional characters: its script when it names
// one, else its region (zh_TW, zh_HK, zh_MO).
inline bool TraditionalChinese(NSString *locale) {
    NSDictionary *c = [NSLocale componentsFromLocaleIdentifier:locale];
    NSString *script = c[NSLocaleScriptCode], *region = c[NSLocaleCountryCode];
    if (script.length) return [script isEqualToString:@"Hant"];
    return [region isEqualToString:@"TW"] || [region isEqualToString:@"HK"] || [region isEqualToString:@"MO"];
}

inline NSString *CanonicalLocale(scenes::ClockLanguage lang) {
    switch (lang) {
        case scenes::ClockLanguage::ChineseSimplified:  return @"zh_CN";
        case scenes::ClockLanguage::ChineseTraditional: return @"zh_TW";
        case scenes::ClockLanguage::Japanese:           return @"ja_JP";
        case scenes::ClockLanguage::Korean:             return @"ko_KR";
        case scenes::ClockLanguage::English:            return @"en_US";
        default:                                        return nil;
    }
}

// localeclock::Speaks: the region already is the language chosen.
inline bool Speaks(NSString *locale, scenes::ClockLanguage lang) {
    NSString *canon = CanonicalLocale(lang);
    if (!canon || !locale.length) return false;
    const std::string want = LanguageOf(canon);
    if (LanguageOf(locale) != want) return false;
    if (want != "zh") return true;
    return TraditionalChinese(locale) == (lang == scenes::ClockLanguage::ChineseTraditional);
}

// localeclock::EnglishMonthFirst, from the region's own default long date.
inline bool EnglishMonthFirst(NSLocale *locale) {
    NSString *f = [NSDateFormatter dateFormatFromTemplate:@"MMMMdy" options:0 locale:locale] ?: @"";
    const NSRange m = [f rangeOfString:@"M"], d = [f rangeOfString:@"d"];
    if (m.location == NSNotFound || d.location == NSNotFound) return true;
    return m.location < d.location;
}

// localeclock::DatePicture, as ICU patterns. Nil means the person's long date.
inline NSString *DatePicture(NSString *locale, scenes::DateStyle style, bool monthFirst) {
    const bool full = style == scenes::DateStyle::Full;
    if (!full && style != scenes::DateStyle::Compact) return nil;
    const std::string lang = LanguageOf(locale);
    if (lang == "ja") return full ? @"yyyy'年'M'月'd'日' EEEE" : @"M'月'd'日'（EEE）";
    if (lang == "zh") return full ? @"yyyy'年'M'月'd'日' EEEE" : @"M'月'd'日' EEE";
    if (lang == "ko") return full ? @"yyyy'년' M'월' d'일' EEEE" : @"M'월' d'일' (EEE)";
    if (lang == "en")
        return full ? (monthFirst ? @"EEEE, MMMM d, yyyy" : @"EEEE, d MMMM yyyy")
                    : (monthFirst ? @"EEE, MMM d" : @"EEE d MMM");
    return nil;
}

// localeclock::Format. `picture` comes back for the log: the date picture used, or nil
// for the person's long date.
inline void Format(NSDate *now, bool force24, scenes::DateStyle style, scenes::ClockLanguage language,
                   NSString **timeOut, NSString **dateOut, NSString **pictureOut) {
    // localeclock::Choose: the region as it is, unless another language is asked for
    // that the region does not already speak -- then that language's standard format.
    NSLocale *use = NSLocale.autoupdatingCurrentLocale;
    if (NSString *canon = CanonicalLocale(language))
        if (!Speaks(use.localeIdentifier, language)) use = [NSLocale localeWithLocaleIdentifier:canon];

    // force_24_hour is the only picture made up here; otherwise the locale's short time.
    NSDateFormatter *tf = [NSDateFormatter new];
    tf.locale = use;
    if (force24) tf.dateFormat = @"HH:mm";
    else { tf.dateStyle = NSDateFormatterNoStyle; tf.timeStyle = NSDateFormatterShortStyle; }
    NSString *t = [tf stringFromDate:now];

    const bool monthFirst = LanguageOf(use.localeIdentifier) == "en" ? EnglishMonthFirst(use) : true;
    NSString *picture = DatePicture(use.localeIdentifier, style, monthFirst);
    NSDateFormatter *df = [NSDateFormatter new];
    df.locale = use;
    if (picture) df.dateFormat = picture;
    else { df.dateStyle = NSDateFormatterLongStyle; df.timeStyle = NSDateFormatterNoStyle; }
    NSString *d = [df stringFromDate:now];

    // Never blank (localeclock's fallbacks: 24-hour time, ISO date).
    if (!t.length) { tf = [NSDateFormatter new]; tf.dateFormat = @"HH:mm"; t = [tf stringFromDate:now]; }
    if (!d.length) { df = [NSDateFormatter new]; df.dateFormat = @"yyyy-MM-dd"; d = [df stringFromDate:now]; }
    *timeOut = t;
    *dateOut = d;
    if (pictureOut) *pictureOut = picture;
}

} // namespace macui
