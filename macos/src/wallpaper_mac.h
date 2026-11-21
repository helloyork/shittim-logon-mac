// shittim-logon / macos / src / wallpaper_mac.h
//
// EXPERIMENTAL (settings.txt `experimental_lock_wallpaper`, off by default): the
// scene's empty room as the desktop picture while the screen is locked, so the
// password screen stands in the room the overlay fades out of -- what the Windows
// overlay does with the lock-screen picture (PersonalizationCSP). macOS has no
// lock-screen picture of its own since Sonoma; the lock screen shows the desktop's.
//
// Measured 2026-11-19 (README.md has the whole of it):
//   - it works: the password field stands in the room after the fade;
//   - the original comes back one frame after the unlock, so the desktop shows the
//     room for a frame -- not fixable from here, the unlock is only known afterwards;
//   - the public API can only write a plain picture choice. An appearance-aware or
//     dynamic .heic put back through it loses that and shows its light variant. So a
//     .heic is never swapped: skipped, and said in the log;
//   - a crash between lock and unlock would leave the room on the desktop, so the
//     original's path is written to a file before the swap, and the next start puts
//     it back if that file is still there.

#pragma once

#import <AppKit/AppKit.h>

#include <string>

namespace macwall {

inline NSString *BackupPath() {
    return [NSHomeDirectory() stringByAppendingPathComponent:@"Library/Application Support/ShittimLogon/wallpaper-backup.txt"];
}

class Swap {
public:
    // A previous run died while the room was the desktop picture: put the original back.
    static void RecoverIfNeeded(void (*log)(const wchar_t *, ...)) {
        NSString *text = [NSString stringWithContentsOfFile:BackupPath() encoding:NSUTF8StringEncoding error:nil];
        if (!text) return;
        NSArray *lines = [text componentsSeparatedByString:@"\n"];
        NSArray *screens = NSScreen.screens;
        for (NSUInteger i = 0; i < screens.count && i < lines.count; i++) {
            NSString *p = lines[i];
            if (p.length == 0) continue;
            NSError *err = nil;
            const BOOL ok = [NSWorkspace.sharedWorkspace setDesktopImageURL:[NSURL fileURLWithPath:p]
                                                                  forScreen:screens[i] options:@{} error:&err];
            if (log) log(L"wall: recovered %hs after an unclean exit -> %d", p.UTF8String, ok);
        }
        [NSFileManager.defaultManager removeItemAtPath:BackupPath() error:nil];
    }

    void In(const std::string &png, void (*log)(const wchar_t *, ...)) {
        if (png.empty() || saved_) return;
        NSWorkspace *ws = NSWorkspace.sharedWorkspace;
        NSMutableArray *saved = [NSMutableArray new];
        NSMutableString *backup = [NSMutableString new];
        for (NSScreen *s in NSScreen.screens) {
            NSURL *u = [ws desktopImageURLForScreen:s];
            if (!u || [u.pathExtension.lowercaseString isEqualToString:@"heic"]) {
                if (log) log(L"wall: not swapping -- the desktop picture is %hs, which would lose its "
                             L"appearance setting on the way back", u ? u.lastPathComponent.UTF8String : "(none)");
                return;
            }
            [saved addObject:@[ s, u, [ws desktopImageOptionsForScreen:s] ?: @{} ]];
            [backup appendFormat:@"%@\n", u.path];
        }
        [backup writeToFile:BackupPath() atomically:YES encoding:NSUTF8StringEncoding error:nil];
        saved_ = saved;
        NSURL *room = [NSURL fileURLWithPath:[NSString stringWithUTF8String:png.c_str()]];
        for (NSScreen *s in NSScreen.screens) {
            NSError *err = nil;
            const BOOL ok = [ws setDesktopImageURL:room forScreen:s options:@{} error:&err];
            if (log) log(L"wall: room is the desktop picture -> %d %hs", ok, err ? err.localizedDescription.UTF8String : "");
        }
    }

    void Out(void (*log)(const wchar_t *, ...)) {
        if (!saved_) return;
        for (NSArray *e in saved_) {
            NSError *err = nil;
            const BOOL ok = [NSWorkspace.sharedWorkspace setDesktopImageURL:e[1] forScreen:e[0] options:e[2] error:&err];
            if (log) log(L"wall: restored %hs -> %d %hs", [e[1] path].UTF8String, ok,
                         err ? err.localizedDescription.UTF8String : "");
        }
        saved_ = nil;
        [NSFileManager.defaultManager removeItemAtPath:BackupPath() error:nil];
    }

private:
    NSArray *saved_ = nil;
};

} // namespace macwall
