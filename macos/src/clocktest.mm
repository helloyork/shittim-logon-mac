// shittim-logon / macos / src / clocktest.mm
//
// The clock's text without a screen: every combination of force_24_hour, date_style
// and clock_language, formatted now in this Mac's region -- the check against
// localeclock.h's table (DatePicture's comment) and the Windows overlay's log line.
//
//   clocktest [locale]     a locale identifier stands in for the region (zh_TW, en_GB, ...)

#include "host.h"
#include "ui_mac.h"

#include <cstdio>

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc > 1) {
            // NSLocale.autoupdatingCurrentLocale follows AppleLocale in this process's
            // defaults; the argument domain outranks the global one.
            [NSUserDefaults.standardUserDefaults setVolatileDomain:@{ @"AppleLocale": @(argv[1]) }
                                                           forName:NSArgumentDomain];
        }
        std::printf("region %s\n", NSLocale.autoupdatingCurrentLocale.localeIdentifier.UTF8String);
        const struct { scenes::ClockLanguage l; const char *n; } langs[] = {
            { scenes::ClockLanguage::Auto, "auto" }, { scenes::ClockLanguage::ChineseSimplified, "zh-CN" },
            { scenes::ClockLanguage::ChineseTraditional, "zh-TW" }, { scenes::ClockLanguage::Japanese, "ja-JP" },
            { scenes::ClockLanguage::Korean, "ko-KR" }, { scenes::ClockLanguage::English, "en-US" } };
        const scenes::DateStyle styles[] = { scenes::DateStyle::Windows, scenes::DateStyle::Full, scenes::DateStyle::Compact };
        NSDate *now = [NSDate date];
        for (bool f24 : { true, false })
            for (auto &l : langs)
                for (scenes::DateStyle s : styles) {
                    NSString *t = nil, *d = nil, *p = nil;
                    macui::Format(now, f24, s, l.l, &t, &d, &p);
                    std::printf("force24=%d %-6s %-8s | %-10s | %s\n", (int)f24, l.n, scenes::DateStyleText(s),
                                t.UTF8String, d.UTF8String);
                }
    }
    return 0;
}
