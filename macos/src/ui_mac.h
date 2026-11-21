// shittim-logon / macos / src / ui_mac.h
//
// The two pieces of text the overlay draws, with CoreText instead of DirectWrite:
//
//   the scene clock   overlay/src/ui.h DrawSceneClock: drawn into the layer's uiRT,
//                     which RenderGpu composites *inside* the room and cuts by the
//                     characters' coverage, so they stand in front of it. Same face
//                     (LINESeedKR-Bd, weight 700), same sizes and heights as fractions
//                     of the screen, same corner marks. Redrawn when the minute turns.
//   the subtitle      overlay/src/ui.h DrawSubtitle: a panel over everything, at the
//                     place subtitle::Place chose, in the factory subtitle face.
//
// The clock's text is localeclock.h's Format, decision for decision: `force_24_hour`
// (on by default) is `HH:mm`, otherwise the locale's short time; the date is this
// project's own picture for Chinese, Japanese, Korean and English when `date_style` is
// full or compact, otherwise the person's long date; `clock_language` formats against
// that language's locale unless the person's region already speaks it. The person's
// region is the macOS one (NSLocale), as Windows reads the signed-in user's.

#pragma once

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#include "voice_mac.h"
#include "../../render/src/scenes.h"
#include "clocktext_mac.h"

#include <cstring>

namespace macui {

inline void RegisterFont(const std::string &path) {
    NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    CFErrorRef err = nullptr;
    if (!CTFontManagerRegisterFontsForURL((__bridge CFURLRef)u, kCTFontManagerScopeProcess, &err) && err) CFRelease(err);
}

inline NSString *FamilyOf(const std::string &path) {
    NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    NSArray *d = CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)u));
    if (d.count == 0) return nil;
    return CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)d[0], kCTFontFamilyNameAttribute));
}

// ---------------------------------------------------------------- typefaces
//
// `clock_font` and `voice_subtitle_font`, resolved the way subtitlefont.h does it:
//
//   factory  the shipped file (the clock's LINE Seed KR; the subtitle face, or LINE Seed
//            KR for English lines -- subtitle::FactoryFaceFor)
//   system   the system UI font
//   custom   the imported file in the settings directory (clock-font-custom.ttf,
//            subtitle-font-custom.ttf -- fontslot.h's names)
//   other    an installed family name, or a path to a font file
//
// What cannot be resolved falls back to the factory face for the clock
// (factoryIsLastResort) and to the system font for the subtitle, with a line saying so.

struct Face {
    NSString *name = nil;      // an exact face (PostScript name), from a file
    NSString *family = nil;    // an installed family, asked at the weight wanted
    std::string label;         // for the log
    bool system() const { return !name && !family; }
};

// The first face in a font file, registered for this process.
inline NSString *FaceInFile(const std::string &path) {
    if (![NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithUTF8String:path.c_str()]]) return nil;
    RegisterFont(path);
    NSURL *u = [NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]];
    NSArray *d = CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)u));
    if (d.count == 0) return nil;
    return CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)d[0], kCTFontNameAttribute));
}

inline Face ResolveFace(const std::string &want, const std::string &factoryPath, const std::string &customPath,
                        bool factoryIsLastResort, const char *what, void (*log)(const wchar_t *, ...)) {
    Face f;
    auto factory = [&]() { f.name = FaceInFile(factoryPath); f.family = nil; f.label = "factory"; };
    auto fallBack = [&](const std::string &why) {
        if (log) log(L"ui: %hs '%hs' %hs -- using the %hs", what, want.c_str(), why.c_str(),
                     factoryIsLastResort ? "factory face" : "system font");
        if (factoryIsLastResort) factory();
        else { f = Face{}; f.label = "system"; }
    };
    switch (subtitle::FontSourceOf(want)) {
        case subtitle::FontSource::Factory: factory(); break;
        case subtitle::FontSource::System:  f.label = "system"; break;
        case subtitle::FontSource::Custom:
            // subtitlefont.h: an imported copy that cannot be used falls back to the face
            // that shipped -- for the subtitle too, as the track does.
            if ((f.name = FaceInFile(customPath))) f.label = "custom";
            else {
                if (log) log(L"ui: %hs custom, but %hs has no usable font -- using the factory face", what,
                             customPath.c_str());
                factory();
            }
            break;
        case subtitle::FontSource::File:
            if ((f.name = FaceInFile(want))) f.label = "file";
            else fallBack("will not open");
            break;
        case subtitle::FontSource::Family: {
            NSString *fam = [NSString stringWithUTF8String:want.c_str()];
            if ([NSFontManager.sharedFontManager availableMembersOfFontFamily:fam].count) { f.family = fam; f.label = "family"; }
            else fallBack("is not an installed family");
            break;
        }
    }
    if (f.label == "factory" && !f.name && log) log(L"ui: the factory %hs file is missing -- system font", what);
    return f;
}

// ---------------------------------------------------------------- the clock

class Clock {
public:
    // overlay.cpp ApplyScenePick's ClockPlacement: the settings over ui.h's defaults
    // (timeY 0.180, dateY 0.257, both moved by clock_offset_y; the date by date_shift_*).
    void Configure(const scenes::Settings &set, const std::string &dataDir, void (*log)(const wchar_t *, ...)) {
        // dcomp.h ResolveClockFont: the factory face is the last resort.
        face_ = ResolveFace(set.clockFont, assetDir_ + "/LINESeedKR-Bd.ttf", dataDir + "/clock-font-custom.ttf",
                            true, "clock font", log);
        at_.force24 = set.force24Hour;
        at_.dateStyle = set.dateStyle;
        at_.language = set.clockLanguage;
        at_.timeY = 0.180f + set.clockOffsetY;
        at_.dateY = 0.257f + set.clockOffsetY + set.dateShiftY;
        at_.offsetX = set.clockOffsetX;
        at_.dateShiftX = set.dateShiftX;
        minute_ = -1;           // redrawn at the next Update; called once per lock
        logged_ = false;
    }

    void Init(const std::string &assetDir) {
        assetDir_ = assetDir;
        face_ = ResolveFace(subtitle::kFontFactory, assetDir_ + "/LINESeedKR-Bd.ttf", std::string(), true, "clock font", nullptr);
    }

    // Draws the clock for the current minute into `target` (BGRA8 premultiplied, shared)
    // when the minute has changed. Returns true when the texture holds a clock.
    bool Update(const gpu::Target &target, void (*log)(const wchar_t *, ...)) {
        NSDate *now = [NSDate date];
        const long minute = (long)(now.timeIntervalSince1970 / 60.0);
        if (minute == minute_ && target.w == w_) return true;
        minute_ = minute; w_ = target.w;
        const int W = target.w, H = target.h;

        NSString *timeText = nil, *dateText = nil, *picture = nil;
        Format(now, at_.force24, at_.dateStyle, at_.language, &timeText, &dateText, &picture);

        std::vector<uint8_t> px(size_t(W) * H * 4, 0);
        CGColorSpaceRef cs = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef ctx = CGBitmapContextCreate(px.data(), W, H, 8, size_t(W) * 4, cs,
                                                 kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Little);
        CGColorSpaceRelease(cs);
        if (!ctx) return false;
        // y down, like the D2D target it replaces
        CGContextTranslateCTM(ctx, 0, H);
        CGContextScaleCTM(ctx, 1, -1);
        NSGraphicsContext *g = [NSGraphicsContext graphicsContextWithCGContext:ctx flipped:YES];
        [NSGraphicsContext saveGraphicsState];
        NSGraphicsContext.currentContext = g;

        const float timePx = H * 0.120f, datePx = H * 0.0278f;
        Centered(timeText, timePx, 700, W, H * at_.timeY, 1.0f, 0.0f);
        Centered(dateText, datePx, 700, W, H * at_.dateY, 0.92f, (float)W * at_.dateShiftX);
        // The three corner marks, half-strength, in the original's pale blue.
        const float m = H * 0.033f, markPx = H * 0.0139f;
        NSColor *mark = [NSColor colorWithSRGBRed:0xDC / 255.0 green:0xEE / 255.0 blue:0xF7 / 255.0 alpha:0.5];
        NSFont *sym = [NSFont systemFontOfSize:markPx];
        NSDictionary *ma = @{ NSFontAttributeName: sym, NSForegroundColorAttributeName: mark };
        struct { NSString *g; float x, y; bool right; } marks[] = {
            { @"△", m, m, false }, { @"✛", (float)W - m, m, true }, { @"○", m, (float)H - m, false } };
        for (auto &k : marks) {
            NSSize s = [k.g sizeWithAttributes:ma];
            [k.g drawAtPoint:NSMakePoint(k.right ? k.x - s.width : k.x, k.y - s.height / 2) withAttributes:ma];
        }
        [NSGraphicsContext restoreGraphicsState];
        CGContextRelease(ctx);

        [target.tex->tex replaceRegion:MTLRegionMake2D(0, 0, W, H) mipmapLevel:0 withBytes:px.data() bytesPerRow:(NSUInteger)W * 4];
        if (!logged_ && log) {
            logged_ = true;
            log(L"ui: clock '%hs' / '%hs' in '%hs' at weight 700 (force_24_hour=%d date_style=%hs, date picture %hs)",
                timeText.UTF8String, dateText.UTF8String, FaceText().UTF8String, (int)at_.force24,
                scenes::DateStyleText(at_.dateStyle), picture ? picture.UTF8String : "the long date");
        }
        return true;
    }
    void Invalidate() { minute_ = -1; }

private:
    void Centered(NSString *s, float px, int weight, int W, float centreY, float alpha, float shift) {
        // A face from a file is drawn at its own weight (ui.h: a chosen file holds one
        // weight); a family is asked for the nearest it really has to 700; the system
        // font at bold.
        NSFont *f = face_.name ? [NSFont fontWithName:face_.name size:px]
                  : face_.family ? [NSFontManager.sharedFontManager fontWithFamily:face_.family
                                                                            traits:(weight >= 700 ? NSBoldFontMask : 0)
                                                                            weight:(weight >= 700 ? 9 : 5) size:px]
                  : nil;
        if (!f) f = [NSFont boldSystemFontOfSize:px];
        NSDictionary *a = @{ NSFontAttributeName: f,
                             NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:1 green:1 blue:1 alpha:alpha] };
        NSSize sz = [s sizeWithAttributes:a];
        [s drawAtPoint:NSMakePoint((W - sz.width) / 2 + W * at_.offsetX + shift, centreY - sz.height / 2) withAttributes:a];
    }
    // ui.h ClockPlacement's defaults.
    struct At {
        float timeY = 0.180f, dateY = 0.257f, offsetX = 0.0f, dateShiftX = 0.0f;
        bool force24 = true;
        scenes::DateStyle dateStyle = scenes::DateStyle::Windows;
        scenes::ClockLanguage language = scenes::ClockLanguage::Auto;
    } at_;
    NSString *FaceText() const {
        return [NSString stringWithFormat:@"%@ (%s)", face_.name ?: face_.family ?: @"system", face_.label.c_str()];
    }
    std::string assetDir_;
    Face face_;
    long minute_ = -1;
    int w_ = 0;
    bool logged_ = false;
};

} // namespace macui

// ---------------------------------------------------------------- the subtitle

// A transparent, flipped view over the Metal view. Coordinates handed in are render
// pixels; `pxPerPt` converts them to the view's points.
@interface SubtitleView : NSView
@property(nonatomic) double pxPerPt;
@property(nonatomic, copy) NSString *fontFamily;     // an exact face name, or nil
@property(nonatomic, copy) NSString *familyName;     // an installed family, when fontFamily is nil
- (void)showPanel:(subtitle::Box)panel text:(NSString *)text fontPx:(float)fontPx topAlign:(bool)top
            alpha:(float)alpha style:(const subtitle::Style &)style;
- (void)hidePanel;
@end

@implementation SubtitleView {
    subtitle::Box _panel;
    NSString *_text;
    float _fontPx, _alpha;
    bool _top, _shown;
    subtitle::Style _style;
}
- (BOOL)isFlipped { return YES; }
- (NSView *)hitTest:(NSPoint)p { return nil; }    // never takes a click
- (void)showPanel:(subtitle::Box)panel text:(NSString *)text fontPx:(float)fontPx topAlign:(bool)top
            alpha:(float)alpha style:(const subtitle::Style &)style {
    const bool same = _shown && panel.left == _panel.left && panel.top == _panel.top && panel.right == _panel.right &&
                      panel.bottom == _panel.bottom && [text isEqualToString:_text] && fabsf(alpha - _alpha) < 0.004f;
    _panel = panel; _text = [text copy]; _fontPx = fontPx; _top = top; _alpha = alpha; _style = style; _shown = true;
    if (!same) self.needsDisplay = YES;
}
- (void)hidePanel {
    if (!_shown) return;
    _shown = false;
    self.needsDisplay = YES;
}
- (void)drawRect:(NSRect)dirty {
    [NSColor.clearColor set];
    NSRectFill(dirty);
    if (!_shown || _alpha <= 0.004f) return;
    const double k = self.pxPerPt > 0 ? self.pxPerPt : 1.0;
    NSRect box = NSMakeRect(_panel.left / k, _panel.top / k, (_panel.right - _panel.left) / k, (_panel.bottom - _panel.top) / k);
    const subtitle::Style &s = _style;
    const CGFloat radius = s.corner;
    NSBezierPath *shape = radius > 0 ? [NSBezierPath bezierPathWithRoundedRect:box xRadius:radius yRadius:radius]
                                     : [NSBezierPath bezierPathWithRect:box];
    if (s.background.a > 0) {
        [[NSColor colorWithSRGBRed:s.background.r green:s.background.g blue:s.background.b alpha:s.background.a * _alpha] set];
        [shape fill];
    }
    if (s.border.a > 0 && s.borderWidth > 0) {
        const CGFloat h = s.borderWidth * 0.5;
        NSRect in = NSInsetRect(box, h, h);
        NSBezierPath *edge = radius > 0 ? [NSBezierPath bezierPathWithRoundedRect:in xRadius:MAX(0, radius - h) yRadius:MAX(0, radius - h)]
                                        : [NSBezierPath bezierPathWithRect:in];
        edge.lineWidth = s.borderWidth;
        [[NSColor colorWithSRGBRed:s.border.r green:s.border.g blue:s.border.b alpha:s.border.a * _alpha] set];
        [edge stroke];
    }
    if (_text.length == 0) return;
    const CGFloat w = box.size.width;
    const CGFloat mx = w * (subtitle::kMarginXFrac / subtitle::kPanelWidthFrac);
    const CGFloat my = w * (subtitle::kMarginYFrac / subtitle::kPanelWidthFrac);
    NSRect inner = NSInsetRect(box, mx, my);
    const CGFloat pt = _fontPx / k;
    NSFont *f = self.fontFamily ? [NSFont fontWithName:self.fontFamily size:pt]
              : self.familyName ? [NSFontManager.sharedFontManager fontWithFamily:self.familyName traits:0 weight:5 size:pt]
              : nil;
    if (!f) f = [NSFont systemFontOfSize:pt];
    NSMutableParagraphStyle *ps = [NSMutableParagraphStyle new];
    ps.minimumLineHeight = ps.maximumLineHeight = pt * subtitle::kLineHeightRatio;
    ps.lineBreakMode = NSLineBreakByWordWrapping;
    NSDictionary *a = @{ NSFontAttributeName: f, NSParagraphStyleAttributeName: ps,
                         NSForegroundColorAttributeName: [NSColor colorWithSRGBRed:s.text.r green:s.text.g blue:s.text.b alpha:s.text.a * _alpha],
                         NSBaselineOffsetAttributeName: @((pt * subtitle::kLineHeightRatio - (f.ascender - f.descender)) / 2) };
    NSRect need = [_text boundingRectWithSize:NSMakeSize(inner.size.width, CGFLOAT_MAX)
                                      options:NSStringDrawingUsesLineFragmentOrigin attributes:a];
    NSRect at = inner;
    if (!_top && need.size.height < inner.size.height) at.origin.y += (inner.size.height - need.size.height) / 2;
    [_text drawWithRect:at options:NSStringDrawingUsesLineFragmentOrigin | NSStringDrawingTruncatesLastVisibleLine attributes:a];
}
@end
