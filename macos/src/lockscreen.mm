// shittim-logon / macos / src / lockscreen.mm
//
// The overlay on the macOS lock screen. The real SpineLayer (overlay/src/spine_layer.h)
// drawn through the Metal gpu.h, in a borderless window placed in a SkyLight space at
// absolute level 400 -- above the lock screen (300) -- shown only while the session is
// locked. README.md says why that placement works.
//
// The sequence is the Windows one in full-scene mode:
//   locked        ResetToIdle: the room, the arrival flash bringing its people in
//   key or click  Trigger: reaction, exit flash, the foreground character's entrance
//   Present       the room has faded; only she and the drift are drawn, so the
//                 system's own password field shows through everything else
// Input: while the room covers the screen the window takes clicks (a click wakes);
// in Present it takes them only over her body (head pat) and passes the rest through.
// Her eyes follow the pointer. Keys always go to the password field: the window is
// never key.
//
// The scene is chosen by the Windows overlay's logic, decision for decision
// (config_mac.h: Pick at start and at every lock, Requeue between locks): the next
// room is drawn at random from the period running now (settings.txt, the scenes.txt
// grammar), never the one shown last, once the session has stayed unlocked for 15 s
// (R57) -- a quick relock shows the same room -- and a minute tick keeps it in season.
// The next room is loaded when it is chosen, while nobody is looking, so a lock waits
// on a load only when the season or the file moved since. Music (bgm_mac.h) and the
// voice go quiet while the display sleeps.
//
//   lockscreen [assetDir] [scene] [--scale S] [--ttl seconds]
//     assetDir: omitted, the assets inside the app (BundledAssets) -- how the login item
//               starts it
//     scene: pins one scene (a test); omitted or `auto`, the rotation above
// The log is ~/Library/Logs/ShittimLogon/lockscreen.log, rolling at 1.5 MiB (host.h).

#include "host.h"
#include "bgm_mac.h"
#include "config_mac.h"
#include "ui_mac.h"
#include "voice_mac.h"
#include "wallpaper_mac.h"

#import <AppKit/AppKit.h>
#import <QuartzCore/QuartzCore.h>
#import <IOKit/IOKitLib.h>
#import <IOKit/ps/IOPowerSources.h>
#import <IOKit/pwr_mgt/IOPMLib.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/file.h>

// ------------------------------------------------------------------ SkyLight

namespace sky {
typedef int32_t (*FMain)();
typedef int32_t (*FCreate)(int32_t, int32_t, int32_t);
typedef int32_t (*FSetLevel)(int32_t, int32_t, int32_t);
typedef int32_t (*FShow)(int32_t, CFArrayRef);
typedef int32_t (*FAdd)(int32_t, int32_t, CFArrayRef, int32_t);
static int32_t cid = 0, space = 0;
static FAdd add = nullptr;
static bool Init() {
    void *h = dlopen("/System/Library/PrivateFrameworks/SkyLight.framework/Versions/A/SkyLight", RTLD_NOW);
    if (!h) return false;
    auto m = (FMain)dlsym(h, "SLSMainConnectionID");
    auto c = (FCreate)dlsym(h, "SLSSpaceCreate");
    auto l = (FSetLevel)dlsym(h, "SLSSpaceSetAbsoluteLevel");
    auto s = (FShow)dlsym(h, "SLSShowSpaces");
    add = (FAdd)dlsym(h, "SLSSpaceAddWindowsAndRemoveFromSpaces");
    if (!m || !c || !l || !s || !add) return false;
    cid = m();
    space = c(cid, 1, 0);
    l(cid, space, 400);
    NSArray *a = @[ @(space) ];
    s(cid, (__bridge CFArrayRef)a);
    return true;
}
static void Attach(NSWindow *w) {
    if (!add) return;
    NSArray *a = @[ @((int32_t)w.windowNumber) ];
    add(cid, space, (__bridge CFArrayRef)a, 7);
}
}

static bool SessionLocked() {
    NSDictionary *d = CFBridgingRelease(CGSessionCopyCurrentDictionary());
    return [d[@"CGSSessionScreenIsLocked"] boolValue];
}

// ------------------------------------------------------------------ views

@interface MetalView : NSView
@end
@implementation MetalView
- (CALayer *)makeBackingLayer { return [CAMetalLayer layer]; }
- (BOOL)wantsUpdateLayer { return YES; }
- (BOOL)acceptsFirstMouse:(NSEvent *)e { return YES; }
@end

@interface OverlayWindow : NSWindow
@end
@implementation OverlayWindow
- (BOOL)canBecomeKeyWindow { return NO; }
- (BOOL)canBecomeMainWindow { return NO; }
// The whole screen, always: AppKit may otherwise move a window ordered front so its top
// is below the menu bar, and the frame -- scaled to fit the view -- would be squashed.
- (NSRect)constrainFrameRect:(NSRect)r toScreen:(NSScreen *)s { return r; }
@end

// ------------------------------------------------------------------ the app

static std::string g_dir, g_pinned;
// R57: how long the session has to stay unlocked before the next lock may show
// another room.
static constexpr double kRotateAfter = 15.0;
static float g_scale = 1.0f;
static double g_ttl = 0;

@interface App : NSObject <NSApplicationDelegate>
@end

@implementation App {
    ID3D11Device *dev;
    ID2D1DeviceContext *d2d;
    std::unique_ptr<SpineLayer> layer;
    OverlayWindow *window;
    MetalView *view;
    CAMetalLayer *metal;
    NSTimer *timer;
    int W, H;                 // render size, pixels
    double pxPerPt;           // render pixels per screen point
    double last;
    bool shown;
    // input
    double lastKey;
    NSUInteger lastFlags;
    bool lastButton;
    int frames;
    double fpsT;
    // voice, subtitle, clock
    std::string character;
    macvoice::Run voice;
    macui::Clock clock;
    SubtitleView *subs;
    bool tapArmed;
    float tapX, tapY;
    // settings, and the experimental lock-screen wallpaper
    macconfig::Settings cfg;
    macwall::Swap wall;
    // the scene on screen (or ready for the next lock), and the gap between locks:
    // its generation (a lock ends it), and the minute tick that keeps it in season
    std::string scene;
    uint64_t gapGen;
    NSTimer *seasonTimer;
    // music, and the display asleep under a locked session
    macbgm::Run bgm;
    bool asleep;
    // keep_display_on, while the lock screen is up
    IOPMAssertionID displayHold;
    bool credentialGone;      // credential_scene=false has handed the screen back
    // The geometry the frame is drawn for, checked for the first second of each lock
    // (2026-11-20: the whole screen sometimes stretched sideways for an instant
    // as the room came in).
    NSRect screenFrame;
    int geomFrames;
    std::string geomLast;
}

// overlay.cpp's VoiceAnchor: the room's speaker while the room is up, else the
// foreground character.
static bool VoiceAnchor(void *ctx, voice::Who who, int *x, int *y, subtitle::Box *face) {
    App *app = (__bridge App *)ctx;
    return [app anchorFor:who x:x y:y face:face];
}

- (bool)anchorFor:(voice::Who)who x:(int *)x y:(int *)y face:(subtitle::Box *)face {
    const SpineLayer &L = *layer;
    POINT p = {};
    RECT f = {};
    const bool roomUp = L.phase != fx::Phase::Present && L.roomFade() > 0.5f;
    const bool speakerIsForeground = (who == voice::Who::CharacterA) == (character == "character_a");
    if (face) *face = subtitle::Box{};
    auto takeFace = [&]() {
        if (face && f.right > f.left) *face = subtitle::Box{ (int)f.left, (int)f.top, (int)f.right, (int)f.bottom };
    };
    if (roomUp && L.RoomAnchorOnScreen(&p, !speakerIsForeground, &f)) { *x = p.x; *y = p.y; takeFace(); return true; }
    if (roomUp && !speakerIsForeground && L.RoomAnchorOnScreen(&p, false, &f)) { *x = p.x; *y = p.y; takeFace(); return true; }
    if (L.SubtitleAnchorOnScreen(&p)) { *x = p.x; *y = p.y; return true; }
    return false;
}

- (void)applicationDidFinishLaunching:(NSNotification *)n {
    NSScreen *screen = NSScreen.mainScreen;
    const double backing = screen.backingScaleFactor;
    W = (int)(screen.frame.size.width * backing * g_scale) & ~1;
    H = (int)(screen.frame.size.height * backing * g_scale) & ~1;
    pxPerPt = backing * g_scale;
    host::Log(L"start pid %d, screen %.0fx%.0f pt @%.0fx, render %dx%d", getpid(),
              screen.frame.size.width, screen.frame.size.height, backing, W, H);

    dev = new ID3D11Device();
    if (!dev->Init()) { host::Log(L"no Metal device"); exit(1); }
    d2d = new ID2D1DeviceContext();
    macwall::Swap::RecoverIfNeeded(host::Log);
    cfg = macconfig::Load(host::Log);
    // wmain's ApplyScenePick: what the queue says, or a draw when it cannot be obeyed.
    const std::string first = g_pinned.empty() ? macconfig::Pick(cfg, host::Log).name : g_pinned;
    if (![self loadScene:first]) exit(1);

    screenFrame = screen.frame;
    window = [[OverlayWindow alloc] initWithContentRect:screen.frame styleMask:NSWindowStyleMaskBorderless
                                                backing:NSBackingStoreBuffered defer:NO];
    window.opaque = NO;
    window.backgroundColor = NSColor.clearColor;
    window.hasShadow = NO;
    window.canBecomeVisibleWithoutLogin = YES;
    window.level = (NSWindowLevel)(INT32_MAX - 2);
    window.collectionBehavior = NSWindowCollectionBehaviorCanJoinAllSpaces | NSWindowCollectionBehaviorStationary |
                                NSWindowCollectionBehaviorIgnoresCycle | NSWindowCollectionBehaviorFullScreenAuxiliary;
    view = [[MetalView alloc] initWithFrame:NSMakeRect(0, 0, screen.frame.size.width, screen.frame.size.height)];
    view.wantsLayer = YES;
    window.contentView = view;
    metal = (CAMetalLayer *)view.layer;
    metal.device = dev->dev;
    metal.pixelFormat = MTLPixelFormatBGRA8Unorm;
    metal.framebufferOnly = NO;
    metal.opaque = NO;
    metal.drawableSize = CGSizeMake(W, H);
    metal.contentsScale = pxPerPt;

    subs = [[SubtitleView alloc] initWithFrame:view.bounds];
    subs.pxPerPt = pxPerPt;
    subs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [view addSubview:subs];
    clock.Init(g_dir);

    if (!sky::Init()) host::Log(L"skylight: symbols missing -- the overlay cannot reach the lock screen");
    [window orderFrontRegardless];
    sky::Attach(window);
    [window orderOut:nil];

    NSDistributedNotificationCenter *dnc = NSDistributedNotificationCenter.defaultCenter;
    [dnc addObserver:self selector:@selector(locked) name:@"com.apple.screenIsLocked" object:nil];
    [dnc addObserver:self selector:@selector(unlocked) name:@"com.apple.screenIsUnlocked" object:nil];
    NSNotificationCenter *wnc = NSWorkspace.sharedWorkspace.notificationCenter;
    [wnc addObserver:self selector:@selector(screensSlept) name:NSWorkspaceScreensDidSleepNotification object:nil];
    [wnc addObserver:self selector:@selector(screensWoke) name:NSWorkspaceScreensDidWakeNotification object:nil];
    if (g_ttl > 0) dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(g_ttl * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        host::Log(L"ttl reached, quitting"); [NSApp terminate:nil]; });
    // Started while somebody is using the machine: that is the Windows overlay's park
    // at sign-in, which keeps the queue in season and rotates after 15 s unlocked.
    if (SessionLocked()) [self locked];
    else [self beginGap];
}

// A new layer for `name`, replacing the one there only if it loads. Called at start
// and while the session is unlocked -- the load is the better part of a second and
// nobody is looking at the overlay then.
- (bool)loadScene:(const std::string &)name {
    const double t0 = CACurrentMediaTime();
    auto next = std::make_unique<SpineLayer>();
    std::string who;
    if (!host::ApplyScene(*next, name, who)) return false;
    next->renderScale = cfg.set.renderScale;      // sizes the targets InitGpu makes
    if (!next->Load(g_dir, who, W, H, host::Log)) { host::Log(L"scene: %hs failed to load", name.c_str()); return false; }
    if (!next->InitGpu(dev, dev, d2d, W, H)) { host::Log(L"scene: %hs InitGpu failed", name.c_str()); return false; }
    layer = std::move(next);
    character = who;
    scene = name;
    [self applyLayer];
    host::Log(L"scene: %hs ready in %.0f ms", name.c_str(), (CACurrentMediaTime() - t0) * 1000.0);
    return true;
}

// overlay.cpp ApplyScenePick, the part that lands on the layer: the settings that
// change how she is drawn and where. Run at every lock and on every new layer.
- (void)applyLayer {
    const scenes::Settings &s = cfg.set;
    layer->clockOverCharacter = s.clockOverCharacter;
    layer->fgSuperAuto = s.characterAA == scenes::CharacterAA::Auto;
    layer->fgSuper = scenes::CharacterSuper(s.characterAA, H);
    if (layer->fgOffsetX != s.characterOffsetX || layer->fgOffsetY != s.characterOffsetY) {
        layer->fgOffsetX = s.characterOffsetX;
        layer->fgOffsetY = s.characterOffsetY;
        layer->fgFramed = false;
    }
    layer->driftOn = s.fullscreenFx && s.credentialScene;
    if (!s.headpat) layer->pat.Cancel();
}

// voicerun::Options::From, with the pack resolved to its directory: `factory` is
// assets/voice, anything else assets/voice-<pack>, and a pack with no directory falls
// back to the one that shipped (voicerun::Run::Begin's rule).
- (macvoice::Options)voiceOptions:(std::string *)clipDir {
    const scenes::Settings &s = cfg.set;
    macvoice::Options o;
    o.on = s.voice;
    o.lipsync = s.voiceLipsync;
    o.volume = s.voiceVolume;
    o.subtitles = s.voiceSubtitle;
    o.sceneSubtitles = s.voiceSubtitleScene;
    o.characterSubtitles = s.voiceSubtitleCharacter;
    o.intervalSeconds = s.voiceIdleInterval;
    o.style = s.voiceSubtitleStyle;
    o.language = s.voiceSubtitleLanguage;
    o.device = s.voiceDevice;
    o.deviceFallback = s.voiceDeviceFallback;
    *clipDir = g_dir + "/voice";
    if (scenes::VoicePackIsCustom(s.voicePack)) {
        // The imported pack: its clips as the settings app wrote them (voiceslot.h's
        // slot, as a directory). Missing or empty falls back to the one that shipped.
        const std::string want = std::string(macconfig::Dir().UTF8String) + "/voice-custom";
        NSArray *clips = [NSFileManager.defaultManager contentsOfDirectoryAtPath:[NSString stringWithUTF8String:want.c_str()]
                                                                           error:nil];
        if (clips.count > 1) *clipDir = want;
        else host::Log(L"voice: voice_pack=custom, but nothing has been imported -- using the one that shipped");
    } else if (s.voicePack != scenes::kVoicePackFactory) {
        const std::string want = g_dir + "/voice-" + s.voicePack;
        BOOL dir = NO;
        if ([NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithUTF8String:want.c_str()] isDirectory:&dir] && dir)
            *clipDir = want;
        else
            host::Log(L"voice: no pack called '%hs' here -- using the one that shipped", s.voicePack.c_str());
    }
    return o;
}

// keep_display_on (and keep_display_on_battery): while the lock screen is up the
// display does not go to sleep on its own. Windows holds it with
// SetThreadExecutionState; the Mac's counterpart is a power assertion.
- (void)holdDisplay:(bool)want {
    if (want && !displayHold) {
        const bool battery = [(__bridge NSString *)IOPSGetProvidingPowerSourceType(nullptr)
                                 isEqualToString:@kIOPMBatteryPowerKey];
        if (battery && !cfg.set.keepDisplayOnBattery) {
            host::Log(L"display: keep_display_on -- not on battery (keep_display_on_battery=false)");
            return;
        }
        if (IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep, kIOPMAssertionLevelOn,
                                        CFSTR("Shittim Logon: the lock screen is showing"), &displayHold) == kIOReturnSuccess)
            host::Log(L"display: kept on while the lock screen is up%hs", battery ? " (on battery)" : "");
        else
            displayHold = 0;
    } else if (!want && displayHold) {
        IOPMAssertionRelease(displayHold);
        displayHold = 0;
    }
}

// The frame loop's period: frame_rate_limit, or the 120 Hz the loop has always run at
// when it is 0 (Windows: follow the display).
- (void)startTimer {
    [timer invalidate];
    const int limit = cfg.set.frameRateLimit;
    const double period = limit > 0 && limit < 120 ? 1.0 / limit : 1.0 / 120;
    timer = [NSTimer timerWithTimeInterval:period target:self selector:@selector(tick) userInfo:nil repeats:YES];
    [NSRunLoop.mainRunLoop addTimer:timer forMode:NSRunLoopCommonModes];
}

// sceneconfig::Requeue: `rotate` from the 15-second rotation, not from the minute
// tick. The queue names the room the next lock draws, and this process draws it, so a
// queue that moves is loaded now, while the session is unlocked.
- (void)requeue:(bool)rotate {
    if (!g_pinned.empty()) return;
    cfg = macconfig::Load(nullptr);
    std::string want;
    macconfig::Requeue(cfg, rotate, &want, host::Log);
    if (!want.empty() && want != scene) [self loadScene:want];
}

// Between locks: dcomp.h's WaitForLogonDesktop. The season tick at once and every
// minute after; the one rotation once the session has been unlocked for 15 s (R57).
// A lock ends the gap before either fires again.
- (void)beginGap {
    const uint64_t gen = ++gapGen;
    [seasonTimer invalidate];
    [self requeue:false];
    seasonTimer = [NSTimer scheduledTimerWithTimeInterval:60.0 repeats:YES block:^(NSTimer *t) {
        if (gen != self->gapGen || self->shown) return;
        [self requeue:false];
    }];
    dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)(kRotateAfter * NSEC_PER_SEC)), dispatch_get_main_queue(), ^{
        if (gen != self->gapGen || self->shown) return;
        if (!g_pinned.empty()) return;
        host::Log(L"warm: unlocked for %.0f s -- choosing the room for the next screen now (R57)", kRotateAfter);
        [self requeue:true];
    });
}

- (void)locked {
    host::Log(L"LOCKED");
    gapGen++;
    [seasonTimer invalidate]; seasonTimer = nil;
    cfg = macconfig::Load(host::Log);       // re-read per lock: a change applies on the next one
    // The gate: RepickScene. The room chosen between locks is kept while it is still
    // active; if the clock or the file moved since, Pick chooses again and it is loaded
    // now, which is the one lock that waits. Then it is the room on screen (R91).
    if (g_pinned.empty()) {
        const std::string now = macconfig::Pick(cfg, host::Log, scene).name;
        if (now != scene) [self loadScene:now];
        if (!macconfig::WriteShownScene(scene))
            host::Log(L"scenes: could not record '%hs' as the room on screen", scene.c_str());
    }
    // render_scale sizes the targets: a change is a new layer (Windows rebuilds them).
    if (layer->renderScale != cfg.set.renderScale) {
        host::Log(L"render scale: %.2f (was %.2f)", (double)cfg.set.renderScale, (double)layer->renderScale);
        [self loadScene:scene];
    }
    [self applyLayer];
    if (cfg.set.eyeTracking) host::Log(L"gaze: eye_tracking=true -- her eyes follow the pointer on the password screen");
    if (cfg.set.frameRateLimit > 0) host::Log(L"frame limit: %d fps", cfg.set.frameRateLimit);
    credentialGone = false;
    asleep = false;
    layer->ResetToIdle();
    // The room's camera is worked out in Step(), and this lock's first frame (and the
    // wallpaper still) is drawn before any Step. A layer loaded since the last lock has
    // never stepped, so that frame used scene::Viewport's default -- the authored 16:9
    // world -- squeezed into a 16:10 screen, and the next frame snapped it out: the whole
    // screen seemed to stretch sideways for an instant, on the first lock after a start
    // (2026-11-20). The Windows overlay steps from its first frame on; its own
    // harness calls this after a load too (overlay.cpp).
    layer->updateRoomCamera();
    if (cfg.lockWallpaper) wall.In([self roomStill], host::Log);
    // A fresh run per lock, as the Windows overlay begins one per logon screen: the
    // arrival line plays again when the room's people come in.
    std::string clipDir;
    const macvoice::Options vo = [self voiceOptions:&clipDir];
    voice.Begin(clipDir, scene, vo, H, host::Log);
    // The track is decided here, with the scene (sceneconfig::Pick's settingsOut).
    macbgm::Options bo;
    bo.on = cfg.set.bgm; bo.volume = cfg.set.bgmVolume; bo.duck = cfg.set.bgmDuck;
    bo.track = macconfig::MusicNow(cfg);
    bo.dataDir = macconfig::Dir().UTF8String;
    bo.device = cfg.set.voiceDevice;
    bo.deviceFallback = cfg.set.voiceDeviceFallback;
    bgm.Begin(g_dir, bo, host::Log);
    clock.Configure(cfg.set, macconfig::Dir().UTF8String, host::Log);  // re-read with the rest per lock
    // voice_subtitle_font, for the language the lines are in (subtitlefont.h): the
    // factory face is the subtitle file, or the clock's LINE Seed KR for English.
    {
        const subtitle::Language lang = cfg.set.voiceSubtitleLanguage;
        const std::string factory = g_dir + (subtitle::FactoryFaceFor(lang) == subtitle::FactoryFace::Clock
                                                 ? "/LINESeedKR-Bd.ttf" : "/subtitle-font.ttf");
        const macui::Face f = macui::ResolveFace(cfg.set.voiceSubtitleStyle.font, factory,
                                                 std::string(macconfig::Dir().UTF8String) + "/subtitle-font-custom.ttf",
                                                 false, "subtitle font", host::Log);
        subs.fontFamily = f.name;
        subs.familyName = f.family;
        host::Log(L"ui: subtitle face '%hs' (%hs)", f.name ? f.name.UTF8String : f.family ? f.family.UTF8String : "system",
                  f.label.c_str());
    }
    clock.Invalidate();
    tapArmed = false;
    [subs hidePanel];
    // The first frame of *this* lock goes to the screen before the window does. The
    // layer still holds the last frame of the previous one -- her, in the foreground
    // -- and showing the window first put that on screen for a frame on every lock and
    // every wake (2026-11-19; the Windows overlay's R98 in a new place).
    layer->uiHasContent = clock.Update(layer->uiRT, host::Log);
    [self presentFrame:layer->RenderGpu(dev, dev) sync:true];
    window.ignoresMouseEvents = NO;
    window.alphaValue = 1;
    // Into the level-400 space first, then on screen. Ordered front first, the window
    // spent its first frame in the session's own space -- the one macOS is animating
    // into the lock screen at that moment -- and could be drawn with that space's
    // transition. Attached again afterwards, as before, in case ordering front moved it.
    sky::Attach(window);
    [window orderFrontRegardless];
    sky::Attach(window);
    geomFrames = 0;
    geomLast.clear();
    [self checkGeometry:"show"];
    shown = true;
    last = CACurrentMediaTime();
    lastKey = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventKeyDown);
    lastFlags = NSEvent.modifierFlags;
    lastButton = false;
    frames = 0; fpsT = last;
    [self startTimer];
    if (cfg.set.keepDisplayOn) [self holdDisplay:true];
}

- (void)unlocked {
    host::Log(L"UNLOCKED");
    [self holdDisplay:false];
    wall.Out(host::Log);
    voice.End();
    [subs hidePanel];
    shown = false;
    [timer invalidate]; timer = nil;
    // Leave nothing in the layer for the next lock to show: a transparent frame, on
    // screen, before the window goes.
    [self presentFrame:nullptr sync:true];
    [window orderOut:nil];
    // An unlock is the room leaving, not the password going in: overlay.cpp's table,
    // "out over 0.5 s".
    bgm.Leave(macbgm::kFadePause, true);
    asleep = false;
    [self beginGap];
}

// The display went to sleep under the lock screen: nobody is looking, so nothing
// plays and nothing is drawn (the Windows overlay's R46). The scene stays where it
// was and carries on when the display wakes.
- (void)screensSlept {
    if (!shown || asleep) return;
    host::Log(L"display asleep -- quiet");
    asleep = true;
    voice.FadeOut();
    bgm.Leave(macbgm::kFadePause, false);
    [timer invalidate]; timer = nil;
}

- (void)screensWoke {
    if (!shown || !asleep) return;
    host::Log(L"display awake");
    asleep = false;
    last = CACurrentMediaTime();
    [self startTimer];
}

- (void)applicationWillTerminate:(NSNotification *)n { [self holdDisplay:false]; wall.Out(host::Log); bgm.End(0.0f); }

// The scene's empty room at this screen's size, for the experimental lock wallpaper:
// one frame of the freshly reset layer -- the room before its people arrive -- with
// the clock left out. Drawn once per scene and size and kept.
- (std::string)roomStill {
    NSString *name = [NSString stringWithFormat:@"lock-room-%s-%dx%d.png", scene.c_str(), W, H];
    NSString *path = [macconfig::Dir() stringByAppendingPathComponent:name];
    if ([NSFileManager.defaultManager fileExistsAtPath:path]) return path.UTF8String;
    const bool clockWas = layer->uiHasContent;
    layer->uiHasContent = false;
    ID2D1Bitmap1 *bmp = layer->RenderGpu(dev, dev);
    dev->Flush(true);
    layer->uiHasContent = clockWas;
    auto *surf = bmp ? dynamic_cast<ID3D11Texture2D *>(bmp->surface) : nullptr;
    if (!surf) return std::string();
    const int w = layer->resolveRT.w, h = layer->resolveRT.h;
    std::vector<uint8_t> bgra(size_t(w) * h * 4);
    [surf->tex getBytes:bgra.data() bytesPerRow:(NSUInteger)w * 4 fromRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:0];
    sl::Image img; img.width = w; img.height = h; img.rgba.resize(bgra.size());
    for (size_t p = 0; p < size_t(w) * h; p++) {
        img.rgba[p * 4 + 0] = bgra[p * 4 + 2];
        img.rgba[p * 4 + 1] = bgra[p * 4 + 1];
        img.rgba[p * 4 + 2] = bgra[p * 4 + 0];
        img.rgba[p * 4 + 3] = 255;           // the room is opaque; a desktop picture has no alpha
    }
    try { sl::savePng(img, path.UTF8String); } catch (const std::exception &) { return std::string(); }
    host::Log(L"wall: baked %hs", path.UTF8String);
    return path.UTF8String;
}

// Hands a finished frame (or, with `bmp` null, a transparent one) to the screen. With
// `sync` the drawable is presented inside the current Core Animation transaction after
// the GPU has scheduled it, so whatever follows -- showing or hiding the window -- is
// composited with this frame and not the one before it.
- (void)presentFrame:(ID2D1Bitmap1 *)bmp sync:(bool)sync {
    if (sync) metal.presentsWithTransaction = YES;
    // render_scale: the scene's targets are smaller than the screen (spine_layer.h
    // scaledDim), and Windows' DCompDrawImpl scales the frame into the window. Here the
    // drawable takes the frame's size and the layer's own gravity (resize) scales it up --
    // a 1:1 copy into a screen-sized drawable put it in the top-left corner and left the
    // rest of the drawable as whatever an earlier frame had there.
    if (bmp && layer) {
        const CGSize want = CGSizeMake(layer->resolveRT.w, layer->resolveRT.h);
        if (!CGSizeEqualToSize(metal.drawableSize, want)) metal.drawableSize = want;
    }
    id<CAMetalDrawable> drawable = [metal nextDrawable];
    if (drawable) {
        dev->EndEncoder();
        auto *surf = bmp ? dynamic_cast<ID3D11Texture2D *>(bmp->surface) : nullptr;
        if (surf) {
            id<MTLBlitCommandEncoder> b = [dev->Cmd() blitCommandEncoder];
            [b copyFromTexture:surf->tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(layer->resolveRT.w, layer->resolveRT.h, 1)
                     toTexture:drawable.texture destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
            [b endEncoding];
        } else {
            MTLRenderPassDescriptor *d = [MTLRenderPassDescriptor renderPassDescriptor];
            d.colorAttachments[0].texture = drawable.texture;
            d.colorAttachments[0].loadAction = MTLLoadActionClear;
            d.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);
            d.colorAttachments[0].storeAction = MTLStoreActionStore;
            [[dev->Cmd() renderCommandEncoderWithDescriptor:d] endEncoding];
        }
        if (!sync) [dev->Cmd() presentDrawable:drawable];
    }
    if (sync) {
        id<MTLCommandBuffer> cmd = dev->Cmd();
        dev->Flush(false);
        [cmd waitUntilScheduled];
        if (drawable) [drawable present];
        [CATransaction flush];
        metal.presentsWithTransaction = NO;
    } else {
        dev->Flush(false);
    }
}

// Logs the window, view and drawable when they are not what the frame is drawn for --
// once per distinct state, so a stretch somebody sees can be found by its time.
- (void)checkGeometry:(const char *)when {
    const NSRect wf = window.frame;
    const CGSize vb = view.bounds.size, ds = metal.drawableSize;
    const double scr = NSScreen.mainScreen.backingScaleFactor;
    const bool ok = NSEqualRects(wf, screenFrame) && NSEqualSizes(vb, screenFrame.size) &&
                    fabs(ds.width * vb.height - ds.height * vb.width) < 1.0 && fabs(scr * g_scale - pxPerPt) < 0.01;
    char buf[256];
    std::snprintf(buf, sizeof buf, "window %.0f,%.0f %.0fx%.0f view %.0fx%.0f drawable %.0fx%.0f scale %.1f",
                  wf.origin.x, wf.origin.y, wf.size.width, wf.size.height, vb.width, vb.height, ds.width, ds.height, scr);
    if (ok || geomLast == buf) return;
    geomLast = buf;
    host::Log(L"geometry (%hs, frame %d): %hs -- drawn for %.0fx%.0f pt", when, geomFrames, buf,
              screenFrame.size.width, screenFrame.size.height);
}

- (void)tick {
    if (!shown) return;
    if (geomFrames < 120) { geomFrames++; [self checkGeometry:"lock"]; }
    const double now = CACurrentMediaTime();
    float dt = (float)(now - last);
    last = now;
    if (dt > 0.1f) dt = 0.1f;

    // Pointer, in render pixels, y down.
    const NSPoint m = NSEvent.mouseLocation;
    const NSRect sf = NSScreen.mainScreen.frame;
    const float px = (float)((m.x - sf.origin.x) * pxPerPt);
    const float py = (float)((sf.origin.y + sf.size.height - m.y) * pxPerPt);
    const bool button = (NSEvent.pressedMouseButtons & 1) != 0;

    // Wake: any key (including a bare modifier) or a click while the room is up.
    const double k = CGEventSourceSecondsSinceLastEventType(kCGEventSourceStateHIDSystemState, kCGEventKeyDown);
    const NSUInteger flags = NSEvent.modifierFlags & NSEventModifierFlagDeviceIndependentFlagsMask;
    const bool key = k + 0.005 < lastKey || flags != lastFlags;
    lastKey = k; lastFlags = flags;
    if (layer->phase == fx::Phase::Idle && (key || (button && !lastButton))) {
        host::Log(L"wake: %hs", key ? "key" : "click");
        layer->Trigger();
    }

    // credential_scene=false: once the room has gone she does not enter, and nothing of
    // ours is drawn over the password screen (overlay.cpp hands the whole monitor back).
    // The voice goes on, as it does on Windows behind the cut window.
    const bool gone = !cfg.set.credentialScene && layer->phase >= fx::Phase::Entering;
    if (gone && !credentialGone) {
        credentialGone = true;
        host::Log(L"input: credential_scene off -- the whole screen is the system's; "
                  L"the character does not enter and nothing of ours is drawn");
        [self presentFrame:nullptr sync:true];
        window.alphaValue = 0;
    }

    // In front of everything until she is present; then only over her.
    const bool present = !gone && layer->phase == fx::Phase::Present;
    const bool overHer = present && layer->IsOverBody(px, py);
    const bool take = !gone && (!present || (cfg.set.headpat && overHer) || layer->pat.patting());
    if (window.ignoresMouseEvents == take) window.ignoresMouseEvents = !take;
    // A tap on her (pressed and released without dragging) is a line from her: the
    // reference's rule, as overlay.cpp keeps it. Checked before PatUpdate so a press
    // that becomes a pat is still seen as a press.
    if (present && !layer->pat.patting()) {
        if (button && !lastButton) {
            tapArmed = layer->IsOverBody(px, py);
            tapX = px; tapY = py;
        } else if (!button && lastButton && tapArmed) {
            const float slop = 0.02f * layer->target.height;
            if (fabsf(px - tapX) <= slop && fabsf(py - tapY) <= slop) {
                host::Log(L"voice: tap at %.0f,%.0f -- she answers", px, py);
                voice.OnTap();
            }
            tapArmed = false;
        }
    } else if (!button) {
        tapArmed = false;
    }
    // eye_tracking (off by default, as on Windows) and headpat (on by default).
    layer->GazeUpdate(cfg.set.eyeTracking, present, px, py);
    if (present && cfg.set.headpat) layer->PatUpdate(button, true, px, py);
    lastButton = button;

    // The voice, before the step that poses her, as VoiceStep does.
    macvoice::Frame vf;
    vf.phase = layer->phase;
    vf.onStage = layer->arrive.onStage();
    vf.anchor = VoiceAnchor;
    vf.anchorCtx = (__bridge void *)self;
    vf.stage = subtitle::Box{ 0, 0, layer->target.width, layer->target.height };
    voice.Step(dt, vf);
    if (const char *want = voice.expression()) layer->SetExpression(want);
    lipsync::Pose pose;
    const bool talking = voice.mouth(dt, &pose);
    layer->SetMouth(talking, pose);
    bgm.Step(true, voice.speaking());       // the tick runs only while the room is heard

    // The clock, inside the room.
    layer->uiHasContent = clock.Update(layer->uiRT, host::Log);

    layer->Step(dt);

    if (gone) {                              // nothing of ours on screen
        [subs hidePanel];
        dev->Flush(false);
        return;
    }

    // The subtitle, over everything.
    if (voice.subtitleUp() && voice.placement().visible && voice.text()) {
        const std::vector<uint32_t> cps = subtitle::Decode(voice.text());
        const int n = std::min((int)cps.size(), voice.visibleChars());
        NSMutableString *visibleText = [NSMutableString new];
        for (int i = 0; i < n; i++) {
            const uint32_t cp = cps[(size_t)i];
            [visibleText appendString:[[NSString alloc] initWithBytes:&cp length:4 encoding:NSUTF32LittleEndianStringEncoding]];
        }
        [subs showPanel:voice.placement().panel text:visibleText fontPx:voice.solved().fontPx topAlign:voice.solved().topAlign
                  alpha:voice.alpha() style:voice.style()];
    } else {
        [subs hidePanel];
    }
    ID2D1Bitmap1 *bmp = layer->RenderGpu(dev, dev);
    if (bmp) [self presentFrame:bmp sync:false];
    else dev->Flush(false);

    frames++;
    if (now - fpsT >= 2.0) {
        host::Log(L"fps %.1f phase %d take %d batches %d triangles %d", frames / (now - fpsT), (int)layer->phase,
                  (int)take, (int)layer->lastBatches, (int)layer->lastTriangles);
        frames = 0; fpsT = now;
    }
}
@end

// Where the assets are when none is named: the app's own Resources/assets, or -- the
// overlay is a helper inside Shittim Logon.app (Contents/Library/LoginItems) -- the
// containing app's.
static std::string BundledAssets() {
    NSString *own = NSBundle.mainBundle.bundlePath;
    for (NSString *dir in @[ [own stringByAppendingPathComponent:@"Contents/Resources/assets"],
                             [own stringByAppendingPathComponent:@"../../../Resources/assets"] ]) {
        BOOL isDir = NO;
        if ([NSFileManager.defaultManager fileExistsAtPath:dir isDirectory:&isDir] && isDir)
            return dir.stringByStandardizingPath.UTF8String;
    }
    return "";
}

// One overlay per session, as instance.h is on Windows: the login item and somebody
// opening the helper by hand would otherwise put two windows over the lock screen.
// The lock is the kernel's, so a crash releases it.
static bool OnlyInstance() {
    NSString *dir = macconfig::Dir();
    [NSFileManager.defaultManager createDirectoryAtPath:dir withIntermediateDirectories:YES attributes:nil error:nil];
    const int fd = open([dir stringByAppendingPathComponent:@"lockscreen.lock"].fileSystemRepresentation,
                        O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    return fd >= 0 && flock(fd, LOCK_EX | LOCK_NB) == 0;   // held until the process ends
}

int main(int argc, char **argv) {
    @autoreleasepool {
        int i = 1;
        if (argc > 1 && std::strncmp(argv[1], "--", 2) != 0) { g_dir = argv[1]; i = 2; }
        else g_dir = BundledAssets();
        if (i < argc && std::strncmp(argv[i], "--", 2) != 0) {
            if (std::strcmp(argv[i], "auto") != 0) g_pinned = argv[i];
            i++;
        }
        for (; i + 1 < argc; i += 2) {
            if (!std::strcmp(argv[i], "--scale")) g_scale = std::atof(argv[i + 1]);
            if (!std::strcmp(argv[i], "--ttl")) g_ttl = std::atof(argv[i + 1]);
        }
        setlocale(LC_ALL, "en_US.UTF-8");
        host::OpenLog(host::DefaultLogPath());
        if (g_dir.empty()) {
            host::Log(L"no assets: neither named nor inside the app");
            std::fprintf(stderr, "usage: lockscreen [assetDir] [scene|auto] [--scale S] [--ttl s]\n");
            return 2;
        }
        // Exit 0: the login item's KeepAlive restarts only a failure, and this is not one.
        if (!OnlyInstance()) { host::Log(L"another overlay is running in this session -- leaving"); return 0; }
        // Turning it off (the settings app unregisters the login item) is a SIGTERM from
        // launchd: put the desktop picture back and stop the music the way quitting does.
        signal(SIGTERM, SIG_IGN);
        static dispatch_source_t term = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, SIGTERM, 0, dispatch_get_main_queue());
        dispatch_source_set_event_handler(term, ^{ host::Log(L"SIGTERM -- quitting"); [NSApp terminate:nil]; });
        dispatch_resume(term);
        NSApplication *app = NSApplication.sharedApplication;
        app.activationPolicy = NSApplicationActivationPolicyAccessory;
        App *d = [App new];
        app.delegate = d;
        [app run];
    }
    return 0;
}
