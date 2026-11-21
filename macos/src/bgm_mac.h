// shittim-logon / macos / src / bgm_mac.h
//
// The background music on the Mac: overlay/src/bgm.h's behaviour with the Windows half
// swapped. The decisions are bgm.h's, with bgm.h's numbers; only the player differs
// (AVAudioPlayer instead of an XAudio2 source voice).
//
//   fades in      the room is heard                     kFadeIn (first), kFadeResume
//   fades out     the display sleeps                    kFadePause, then paused
//                 the session unlocks (the room leaves) kFadePause, then released
//   ducks         while a voice line is on air          to bgm_duck % at kDuckRate
//
// Which track: `bgm_track`, or the period's `music=` (sceneconfig::MusicFor), decided
// once per lock with the scene. One name is one track looped forever, falling back to
// the default track if it does not open. Several names (comma-separated,
// scenes::TrackList) play at random: the lock starts on one that is not the track heard
// last (`shown-track` beside the settings, written whenever the room becomes heard and
// at every hand-over while it is heard), and each track hands over to another of the
// list when it ends. A track that does not open is dropped from this lock's list.
//
// Files, as bgmpack.h names them: `bgm.pak` for `factory`, `bgm-<id>.pak` otherwise,
// one the person imported (in the settings directory) before one that shipped (in the
// asset directory). A version 1 pack is 16-bit PCM trimmed for a seamless loop and is
// handed to the player as an in-memory WAV. `bgm.mp3` stands in for the factory pack
// if that one will not open.
//
// The rule from macos/README.md holds: nothing that can block runs on the frame
// thread. Players are made, started, paused, handed over and released on a serial
// queue of their own, and the main thread only computes the level and posts it.

#pragma once

#import <AVFoundation/AVFoundation.h>

#include "audiodev_mac.h"
#include "../../render/src/scenes.h"

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

// The end of a track that does not loop, delivered to the queue.
@interface MacBgmEnd : NSObject <AVAudioPlayerDelegate>
@property (nonatomic, copy) void (^ended)(AVAudioPlayer *);
@end
@implementation MacBgmEnd
- (void)audioPlayerDidFinishPlaying:(AVAudioPlayer *)p successfully:(BOOL)ok {
    if (self.ended) self.ended(p);
}
@end

namespace macbgm {

// bgm.h's.
inline constexpr float kFadeIn = 2.0f;
inline constexpr float kFadeResume = 0.8f;
inline constexpr float kFadePause = 0.5f;
inline constexpr float kSubmitFade = 0.6f;
inline constexpr float kDuckRate = 4.0f;

struct Options {
    bool on = false;
    int volume = scenes::kBgmVolumeDefault;
    int duck = scenes::kBgmDuckDefault;
    std::string track = scenes::kBgmTrackFactory;
    std::string dataDir;            // where `shown-track` and imported tracks live
    std::string device;             // voice_device, shared with the voice (bgm::Options::From)
    bool deviceFallback = false;
};

class Run {
public:
    bool on() const { return on_; }

    // One per lock, as the Windows overlay begins one per screen.
    void Begin(const std::string &assetDir, const Options &o, void (*log)(const wchar_t *, ...)) {
        End(0.0f);
        log_ = log;
        on_ = o.on;
        if (!on_) return;
        volume_ = std::max(0, std::min(100, o.volume));
        duckTo_ = (float)std::max(0, std::min(100, o.duck)) / 100.0f;
        level_ = 0.0f; target_ = 0.0f; duck_ = 1.0f; applied_ = -1.0f;
        heard_ = false; everHeard_ = false; running_ = false;
        lastTick_ = 0.0;
        st_ = std::make_shared<State>();
        auto st = st_;
        st->assetDir = assetDir;
        st->dataDir = o.dataDir;
        st->log = log;
        // The voice's device, by the voice's rule; silent is a run with no player, which
        // keeps its fades and its log but makes no sound.
        const macaudio::Choice out = macaudio::Resolve(o.device, o.deviceFallback, nullptr);
        st->device = out.uid;
        st->silent = out.silent;
        st->list = scenes::TrackList(o.track);
        if (st->list.empty()) st->list.push_back(scenes::kBgmTrackFactory);
        st->end = [MacBgmEnd new];
        std::weak_ptr<State> weak = st;
        st->end.ended = ^(AVAudioPlayer *p) {
            dispatch_async(Queue(), ^{ if (auto s = weak.lock()) Ended(*s, p); });
        };
        dispatch_async(Queue(), ^{
            if (st->silent) { Say(*st, "bgm: the chosen output is not connected -- silent until it is back"); return; }
            OpenFirst(*st);
            Apply(*st);                     // whatever the loop asked for while this opened
        });
        if (log) log(L"bgm: on, volume %d, duck to %d%%, track '%hs'", volume_,
                     (int)(duckTo_ * 100.0f + 0.5f), o.track.c_str());
    }

    // Once round the frame loop. `heard`: the room is in front of somebody -- the lock
    // screen is up and the display awake. (Windows' `visible && !passwordIn`; the Mac
    // has no opening over the room, so the password going in is the unlock.)
    void Step(bool heard, bool speaking) {
        if (!on_ || !st_) return;
        const double now = CACurrentMediaTime();
        float dt = lastTick_ > 0.0 ? (float)(now - lastTick_) : 0.0f;
        lastTick_ = now;
        dt = std::max(0.0f, std::min(0.5f, dt));    // bgm.h: a long gap is one slow frame

        if (heard != heard_) {
            heard_ = heard;
            st_->heard = heard;
            target_ = heard ? 1.0f : 0.0f;
            if (heard) {
                rate_ = 1.0f / (everHeard_ ? kFadeResume : kFadeIn);
                // Several tracks: this is the one being heard, so the next lock starts
                // on another. Written at every return, as bgm.h does.
                auto st = st_;
                dispatch_async(Queue(), ^{ if (st->list.size() > 1) { st->last = st->current; WriteShown(*st, st->current); } });
                if (log_) log_(everHeard_ ? L"bgm: the room is back -- fading in" : L"bgm: the room is up -- fading in");
                everHeard_ = true;
            } else {
                rate_ = 1.0f / kFadePause;
            }
        }
        if (level_ != target_) {
            const float step = rate_ * dt;
            level_ = level_ < target_ ? std::min(target_, level_ + step) : std::max(target_, level_ - step);
        }
        const float duckTo = speaking ? duckTo_ : 1.0f;
        if (duck_ != duckTo) {
            const float step = kDuckRate * dt;
            duck_ = duck_ < duckTo ? std::min(duckTo, duck_ + step) : std::max(duckTo, duck_ - step);
        }

        const bool wantRunning = !(level_ <= 0.0f && target_ <= 0.0f);
        const float a = (float)volume_ / 100.0f;
        const float want = a * a * level_ * duck_;
        // Posted only when it has moved enough to hear, or reached an end.
        const bool moved = std::fabs(want - applied_) >= 0.004f || (want != applied_ && (level_ == target_ && duck_ == duckTo));
        if (wantRunning != running_ || moved) {
            running_ = wantRunning;
            applied_ = want;
            auto st = st_;
            st->wantVol = want;
            st->wantRun = wantRunning;
            ++st->gen;                      // cancels a pause Leave left pending
            dispatch_async(Queue(), ^{ Apply(*st); });
        }
    }

    // The room left without the frame loop to ramp it: the display went to sleep
    // (`release` false: paused, kept, comes back where it was) or the session unlocked
    // (`release` true: let go once it is silent). Both are bgm.h's "nothing is on
    // screen", kFadePause. The fade is the player's own.
    void Leave(float seconds, bool release) {
        if (!on_ || !st_) return;
        heard_ = false; level_ = 0.0f; target_ = 0.0f; running_ = false; applied_ = 0.0f;
        lastTick_ = 0.0;
        auto st = st_;
        st->heard = false;
        st->wantVol = 0.0f;
        st->wantRun = false;
        const uint64_t gen = ++st->gen;
        dispatch_async(Queue(), ^{
            if (!st->player || st->gen != gen) return;
            [st->player setVolume:0.0f fadeDuration:seconds];
        });
        dispatch_after(dispatch_time(DISPATCH_TIME_NOW, (int64_t)((seconds + 0.05) * NSEC_PER_SEC)), Queue(), ^{
            if (!st->player || st->gen != gen) return;
            [st->player pause];
            if (release) { st->player.delegate = nil; st->player = nil; }
        });
        if (release) { st_.reset(); on_ = false; }
    }

    void End(float seconds) {
        if (st_) Leave(seconds, true);
        on_ = false;
    }

private:
    struct State {
        std::atomic<uint64_t> gen{ 0 };
        std::atomic<float> wantVol{ 0.0f };   // the latest the frame loop asked for
        std::atomic<bool> wantRun{ false };
        std::atomic<bool> heard{ false };
        // Everything below is touched only on Queue().
        AVAudioPlayer *player = nil;
        MacBgmEnd *end = nil;
        std::string assetDir, dataDir;
        std::string device;                   // a Core Audio UID, empty for the default
        bool silent = false;
        std::vector<std::string> list;        // this lock's tracks
        std::string current, last;            // playing; heard last
        void (*log)(const wchar_t *, ...) = nullptr;
    };

    static dispatch_queue_t Queue() {
        static dispatch_queue_t q = dispatch_queue_create("shittim.bgm", DISPATCH_QUEUE_SERIAL);
        return q;
    }

    static void Say(State &st, const std::string &line) {
        if (!st.log) return;
        auto log = st.log;
        dispatch_async(dispatch_get_main_queue(), ^{ log(L"%hs", line.c_str()); });
    }

    static bool Exists(const std::string &p) { return [NSFileManager.defaultManager fileExistsAtPath:[NSString stringWithUTF8String:p.c_str()]]; }
    static std::string FactoryPath(const State &st, const std::string &id) {
        return st.assetDir + (id.empty() || id == scenes::kBgmTrackFactory ? std::string("/bgm.pak") : "/bgm-" + id + ".pak");
    }
    static std::string UserPath(const State &st, const std::string &id) {
        return st.dataDir + "/bgm-" + (id.empty() ? std::string("custom") : id) + ".pak";
    }

    // A version 1 pack's PCM as a WAV.
    static AVAudioPlayer *OpenPak(const std::string &path, std::string *title) {
        FILE *f = std::fopen(path.c_str(), "rb");
        if (!f) return nil;
        AVAudioPlayer *p = nil;
        uint32_t h[8] = {};
        char t[128] = {};
        const bool head = std::fread(h, 4, 8, f) == 8 && std::fread(t, 1, 128, f) == 128;
        // magic 'SLBG', version 1 (samples), rate, channels, frames, hash, offset, bytes
        if (head && h[0] == 0x47424C53u && h[1] == 1 && h[3] >= 1 && h[3] <= 2 && h[2] >= 8000 &&
            h[2] <= 192000 && h[7] > 0 && h[7] < 0x60000000u && std::fseek(f, (long)h[6], SEEK_SET) == 0) {
            NSMutableData *d = [NSMutableData dataWithLength:44 + h[7]];
            uint8_t *b = (uint8_t *)d.mutableBytes;
            if (std::fread(b + 44, 1, h[7], f) == h[7]) {
                auto u32 = [&](size_t at, uint32_t v) { std::memcpy(b + at, &v, 4); };
                auto u16 = [&](size_t at, uint16_t v) { std::memcpy(b + at, &v, 2); };
                std::memcpy(b, "RIFF", 4); u32(4, 36 + h[7]); std::memcpy(b + 8, "WAVEfmt ", 8);
                u32(16, 16); u16(20, 1); u16(22, (uint16_t)h[3]); u32(24, h[2]);
                u32(28, h[2] * h[3] * 2); u16(32, (uint16_t)(h[3] * 2)); u16(34, 16);
                std::memcpy(b + 36, "data", 4); u32(40, h[7]);
                p = [[AVAudioPlayer alloc] initWithData:d error:nil];
                t[127] = 0;
                if (title) *title = t;
            }
        }
        std::fclose(f);
        return p;
    }

    // bgm.h OpenTrackInto: an imported track by this name, then one that shipped, then
    // -- `fallback`, a single track -- the default one.
    static AVAudioPlayer *OpenTrack(State &st, const std::string &id, bool fallback) {
        std::string title;
        if (Exists(UserPath(st, id)))
            if (AVAudioPlayer *p = OpenPak(UserPath(st, id), &title)) { Say(st, "bgm: '" + id + "' -- '" + title + "', imported"); return p; }
        if (AVAudioPlayer *p = OpenPak(FactoryPath(st, id), &title)) { Say(st, "bgm: '" + id + "' -- '" + title + "'"); return p; }
        const bool factory = id.empty() || id == scenes::kBgmTrackFactory;
        if (!fallback && !factory) return nil;
        if (!factory) {
            Say(st, "bgm: this package has no track called '" + id + "' -- using the default one");
            if (AVAudioPlayer *p = OpenPak(FactoryPath(st, scenes::kBgmTrackFactory), &title)) { Say(st, "bgm: default -- '" + title + "'"); return p; }
        }
        NSURL *mp3 = [NSURL fileURLWithPath:[NSString stringWithUTF8String:(st.assetDir + "/bgm.mp3").c_str()]];
        AVAudioPlayer *p = [[AVAudioPlayer alloc] initWithContentsOfURL:mp3 error:nil];
        Say(st, p ? "bgm: bgm.pak will not open -- playing bgm.mp3 (its encoder padding may leave a gap at the loop)"
                  : "bgm: no track opens -- silent");
        return p;
    }

    static void Drop(State &st, const std::string &id) {
        for (size_t i = 0; i < st.list.size(); i++) if (st.list[i] == id) { st.list.erase(st.list.begin() + (long)i); break; }
    }

    static void Adopt(State &st, AVAudioPlayer *p, const std::string &id, bool loop) {
        if (st.player) { st.player.delegate = nil; [st.player stop]; }
        if (!st.device.empty()) p.currentDevice = [NSString stringWithUTF8String:st.device.c_str()];
        p.numberOfLoops = loop ? -1 : 0;
        p.delegate = loop ? nil : st.end;
        p.volume = 0.0f;
        [p prepareToPlay];
        st.player = p;
        st.current = id;
    }

    // bgm.h Begin + OpenFirst.
    static void OpenFirst(State &st) {
        if (st.list.size() > 1) {
            std::vector<std::string> keep;
            for (const std::string &id : st.list) {
                if (Exists(UserPath(st, id)) || Exists(FactoryPath(st, id))) keep.push_back(id);
                else Say(st, "bgm: '" + id + "' has no file here -- left out of this screen's tracks");
            }
            if (keep.empty()) keep.push_back(scenes::kBgmTrackFactory);
            st.list = keep;
        }
        if (st.list.size() > 1) {
            const std::string was = ReadShown(st);
            if (!was.empty()) st.last = was;
        }
        while (st.list.size() > 1) {
            const std::string id = scenes::NextTrack(st.list, st.last, arc4random());
            if (AVAudioPlayer *p = OpenTrack(st, id, false)) {
                Adopt(st, p, id, false);
                st.last = id;
                std::string names;
                for (const std::string &n : st.list) names += (names.empty() ? "" : ", ") + n;
                Say(st, "bgm: " + std::to_string(st.list.size()) + " tracks, played at random (" + names +
                        ") -- this screen starts on '" + id + "'");
                return;
            }
            Drop(st, id);
        }
        if (AVAudioPlayer *p = OpenTrack(st, st.list[0], true)) { Adopt(st, p, st.list[0], true); st.last = st.list[0]; }
    }

    // bgm.h StepPlaylist + HandOver: a track that does not loop has ended.
    static void Ended(State &st, AVAudioPlayer *p) {
        if (p != st.player) return;
        const std::string was = st.current;
        while (st.list.size() > 1) {
            const std::string id = scenes::NextTrack(st.list, st.current, arc4random());
            if (AVAudioPlayer *next = OpenTrack(st, id, false)) {
                Adopt(st, next, id, false);
                // shown-track is the track somebody heard last: only while the room is heard.
                if (st.heard) { st.last = id; WriteShown(st, id); }
                Say(st, "bgm: '" + was + "' ended -- now '" + id + "', at random from " + std::to_string(st.list.size()));
                Apply(st);
                return;
            }
            Say(st, "bgm: '" + id + "' will not open -- left out of this screen's tracks");
            Drop(st, id);
        }
        // Nothing else to go to: this one plays again, and loops from now on.
        p.numberOfLoops = -1;
        p.delegate = nil;
        Say(st, "bgm: '" + was + "' is the only track left that opens -- it loops");
        if (st.wantRun) [p play];
    }

    static std::string ShownPath(const State &st) { return st.dataDir + "/shown-track"; }
    static std::string ReadShown(const State &st) {
        NSString *t = [NSString stringWithContentsOfFile:[NSString stringWithUTF8String:ShownPath(st).c_str()]
                                                encoding:NSUTF8StringEncoding error:nil];
        return t ? scenes::trim(t.UTF8String) : std::string();
    }
    static void WriteShown(const State &st, const std::string &id) {
        if (id.empty()) return;
        [[NSString stringWithUTF8String:id.c_str()] writeToFile:[NSString stringWithUTF8String:ShownPath(st).c_str()]
                                                     atomically:YES encoding:NSUTF8StringEncoding error:nil];
    }

    // On Queue(): the player to what the frame loop last asked for.
    static void Apply(State &st) {
        if (!st.player) return;
        st.player.volume = st.wantVol;
        if (st.wantRun && !st.player.playing) [st.player play];
        if (!st.wantRun && st.player.playing) [st.player pause];
    }

    std::shared_ptr<State> st_;
    void (*log_)(const wchar_t *, ...) = nullptr;
    bool on_ = false;
    int volume_ = 40;
    float duckTo_ = 0.35f;
    float level_ = 0.0f, target_ = 0.0f, rate_ = 1.0f, duck_ = 1.0f, applied_ = -1.0f;
    bool heard_ = false, everHeard_ = false, running_ = false;
    double lastTick_ = 0.0;
};

} // namespace macbgm
