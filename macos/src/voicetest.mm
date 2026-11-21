// shittim-logon / macos / src / voicetest.mm
//
// The voice run without a screen: plays a scene's arrival, wake, greeting and a tap
// through macvoice::Run on a stepped clock and logs what it does -- including any
// exception the audio engine throws, which on the lock screen would stop the frame.
//
//   voicetest <assetDir> <scene> [seconds]

#include "host.h"
#include "voice_mac.h"

#import <Foundation/Foundation.h>

int main(int argc, char **argv) {
    @autoreleasepool {
        setlocale(LC_ALL, "en_US.UTF-8");
        if (argc < 3) { std::fprintf(stderr, "usage: voicetest <assetDir> <scene> [seconds]\n"); return 2; }
        const std::string dir = argv[1], scene = argv[2];
        const float total = argc > 3 ? std::atof(argv[3]) : 20.0f;
        macvoice::Run run;
        macvoice::Options o;
        if (!run.Begin(dir + "/voice", scene, o, 1800, host::Log)) { host::Log(L"Begin failed"); return 1; }
        const float dt = 1.0f / 60.0f;
        for (float t = 0; t < total; t += dt) {
            macvoice::Frame f;
            f.onStage = t > 0.5f;
            f.phase = t < 6.0f ? fx::Phase::Idle : t < 8.0f ? fx::Phase::Reacting : fx::Phase::Present;
            f.stage = subtitle::Box{ 0, 0, 2880, 1800 };
            @try {
                run.Step(dt, f);
                if (std::fabs(t - 14.0f) < dt / 2) run.OnTap();
            } @catch (NSException *e) {
                host::Log(L"EXCEPTION at %.2fs: %hs -- %hs", t, e.name.UTF8String, e.reason.UTF8String);
                return 3;
            }
            lipsync::Pose p;
            static int k = 0;
            if (run.mouth(0, &p) && (k++ % 15) == 0)
                host::Log(L"t %.2f expression %hs mouth open %.2f subtitle '%hs' (%d chars)", t,
                          run.expression() ? run.expression() : "-", p.open, run.text() ? run.text() : "",
                          run.visibleChars());
            [[NSRunLoop currentRunLoop] runUntilDate:[NSDate dateWithTimeIntervalSinceNow:dt]];
        }
        return 0;
    }
}
