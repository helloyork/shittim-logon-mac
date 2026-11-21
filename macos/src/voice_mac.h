// shittim-logon / macos / src / voice_mac.h
//
// The voice feature on the Mac: overlay/src/voicerun.h with the Windows half swapped.
//
// Everything that decides anything is the shared code, used as it is:
//   render/src/voicelines.h   what each scene's people say (voice::Resolve)
//   render/src/voiceplan.h    which line is next, and when (voice::Director)
//   render/src/voicetext.h    the subtitle for a clip, per language
//   render/src/lipsync.h      the mouth track planned from the clip's PCM
//   render/src/expression.h   the face held while she talks, and after
//   render/src/subtitle.h     the panel's size, place and typewriter
// What is replaced: XAudio2 (overlay/src/voice.h) by AVAudioPlayer, the mapped
// voice.pak (voicepack.h) by the clips' own .ogg files decoded with stb_vorbis, and
// the endpoint machinery (audiodev / audioopen / audioreach), which has no job here:
// macOS routes the session's audio to whatever the user chose.
//
// The run's Step mirrors voicerun::Run::Step line for line, minus devices.

#pragma once

#import <AVFoundation/AVFoundation.h>
#import <AudioToolbox/AudioToolbox.h>

#include "audiodev_mac.h"
#include "../../render/src/expression.h"
#include "../../render/src/lipsync.h"
#include "../../render/src/subtitle.h"
#include "../../render/src/voicelines.h"
#include "../../render/src/voiceplan.h"
#include "../../render/src/voicetext.h"

#define STB_VORBIS_NO_PUSHDATA_API
#include "../../vendor/stb/stb_vorbis.c"
#undef L
#undef C
#undef R

#include <map>
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace macvoice {

struct Pcm {
    std::vector<int16_t> s;   // interleaved
    int channels = 1, rate = 48000;
    size_t frames() const { return channels ? s.size() / channels : 0; }
    bool ok() const { return !s.empty(); }
};

// Decoded on first use and kept: a clip is a few hundred KB of PCM.
class Clips {
public:
    void Open(const std::string &dir) { dir_ = dir; cache_.clear(); }
    const Pcm &Find(const char *name, void (*log)(const wchar_t *, ...)) {
        auto it = cache_.find(name);
        if (it != cache_.end()) return it->second;
        Pcm &p = cache_[name];
        std::string path = dir_ + "/" + name + ".ogg";
        int ch = 0, rate = 0;
        short *out = nullptr;
        const int frames = stb_vorbis_decode_filename(path.c_str(), &ch, &rate, &out);
        if (frames > 0 && out) {
            p.s.assign(out, out + size_t(frames) * ch);
            p.channels = ch; p.rate = rate;
            free(out);
        } else if (DecodeOther(dir_ + "/" + name + ".mp3", &p)) {
            // An imported pack made before the game's own .ogg files (voicesource.h, N8).
        } else if (log) {
            log(L"voice: could not decode %hs", path.c_str());
        }
        return p;
    }
private:
    // Anything Core Audio reads, as 16-bit PCM at the file's own rate and channel count.
    static bool DecodeOther(const std::string &path, Pcm *p) {
        ExtAudioFileRef f = nullptr;
        if (ExtAudioFileOpenURL((__bridge CFURLRef)[NSURL fileURLWithPath:[NSString stringWithUTF8String:path.c_str()]],
                                &f) != noErr || !f)
            return false;
        AudioStreamBasicDescription src = {};
        UInt32 n = sizeof src;
        ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &n, &src);
        AudioStreamBasicDescription want = {};
        want.mSampleRate = src.mSampleRate > 0 ? src.mSampleRate : 48000;
        want.mFormatID = kAudioFormatLinearPCM;
        want.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
        want.mChannelsPerFrame = src.mChannelsPerFrame == 1 ? 1 : 2;
        want.mBitsPerChannel = 16;
        want.mBytesPerFrame = want.mBytesPerPacket = 2 * want.mChannelsPerFrame;
        want.mFramesPerPacket = 1;
        if (ExtAudioFileSetProperty(f, kExtAudioFileProperty_ClientDataFormat, sizeof want, &want) != noErr) {
            ExtAudioFileDispose(f);
            return false;
        }
        std::vector<int16_t> buf(8192 * want.mChannelsPerFrame);
        for (;;) {
            AudioBufferList list;
            list.mNumberBuffers = 1;
            list.mBuffers[0].mNumberChannels = want.mChannelsPerFrame;
            list.mBuffers[0].mDataByteSize = (UInt32)(buf.size() * sizeof(int16_t));
            list.mBuffers[0].mData = buf.data();
            UInt32 frames = 8192;
            if (ExtAudioFileRead(f, &frames, &list) != noErr || frames == 0) break;
            p->s.insert(p->s.end(), buf.begin(), buf.begin() + frames * want.mChannelsPerFrame);
        }
        ExtAudioFileDispose(f);
        p->channels = (int)want.mChannelsPerFrame;
        p->rate = (int)want.mSampleRate;
        return p->ok();
    }

    std::string dir_;
    std::map<std::string, Pcm> cache_;
};

inline constexpr float kFadeOut = 0.25f;
enum class Tick { Nothing, Advanced, Finished };

// voice::Player's contract, on AVAudioPlayer.
//
// AVAudioEngine was the first choice and failed on the lock screen: it starts, then
// every `play` throws "player did not see an IO cycle" after blocking the caller for
// 5-6 s (measured 2026-11-19, both from SSH and in the GUI session while locked).
// NSSound played fine on the same lock screen (the first test run there), and
// AVAudioPlayer sits on the same AudioQueue path, so each clip is an
// AVAudioPlayer over an in-memory WAV of the clip's own PCM -- no resampling, no graph.
//
// Nothing that can block runs on the drawing thread: players are created, started,
// faded and stopped on a serial queue, the way audioopen.h opens the Windows endpoint
// off the drawing thread. The clip clock is the moment `play` returned for this clip;
// until then, and for a clip that would not start, it is the stepped clock -- the
// schedule, the subtitle and the mouth go on either way, which is voice.h's silent mode.
class Player {
public:
    // `device`: a Core Audio UID, or empty for the default output. `silent`: the chosen
    // device is not there -- no player at all, which is voice.h's silent mode (the
    // schedule, the subtitle and the mouth go on).
    bool Open(int volume0to100, const std::string &device, bool silent, void (*log)(const wchar_t *, ...)) {
        Close();
        const float a = std::max(0, std::min(100, volume0to100)) / 100.0f;
        gain_ = a * a;
        device_ = device;
        if (!silent) state_ = std::make_shared<State>();
        return true;
    }
    void Close() {
        Stop();
        if (state_) state_->closed = true;
        state_.reset();
    }
    bool silent() const { return !state_; }
    bool playing() const { return line_ != nullptr; }
    const voice::Clip *clip() const { return (line_ && clip_ < line_->count) ? &line_->clip[clip_] : nullptr; }
    float clipSeconds() const { return clipSeconds_; }
    float clipPosition() const {
        if (!line_) return 0.0f;
        if (!state_) return stepped_;
        const double at = state_->startedAt.load();
        if (at <= 0.0 || state_->startedGen.load() != gen_) return stepped_;
        return (float)(CACurrentMediaTime() - at);
    }
    float fadeLeft() const {
        if (fading_ <= 0.0f) return 1.0f;
        const float gone = (float)(CACurrentMediaTime() - fadeFrom_);
        return gone >= fading_ ? 0.0f : 1.0f - gone / fading_;
    }
    bool fading() const { return fading_ > 0.0f; }

    bool Play(const voice::Line &line, Clips &clips, void (*log)(const wchar_t *, ...)) {
        Stop();
        line_ = &line;
        clip_ = 0;
        if (!Submit(clips, log)) { line_ = nullptr; return false; }
        return true;
    }
    Tick Step(float dt, Clips &clips, void (*log)(const wchar_t *, ...)) {
        if (fading_ > 0.0f) return StepFade() ? Tick::Finished : Tick::Nothing;
        if (!line_) return Tick::Nothing;
        stepped_ += dt;
        if (clipPosition() < clipSeconds_ && stepped_ < clipSeconds_ + 0.5f) return Tick::Nothing;
        clip_++;
        if (clip_ >= line_->count || !Submit(clips, log)) {
            line_ = nullptr; clip_ = 0; clipSeconds_ = 0.0f;
            return Tick::Finished;
        }
        return Tick::Advanced;
    }
    void Stop() {
        if (state_) {
            auto st = state_;
            dispatch_async(Queue(), ^{
                if (st->player) [st->player stop];
                st->player = nil;
            });
        }
        line_ = nullptr; clip_ = 0; clipSeconds_ = 0.0f; fading_ = 0.0f; stepped_ = 0.0f;
    }
    void FadeOut(float seconds = kFadeOut) {
        if (!line_ || fading_ > 0.0f) return;
        fading_ = std::max(0.01f, seconds);
        fadeFrom_ = CACurrentMediaTime();
        if (state_) {
            auto st = state_;
            const float len = fading_;
            dispatch_async(Queue(), ^{ if (st->player) [st->player setVolume:0.0f fadeDuration:len]; });
        }
    }
    bool StepFade() {
        if (fading_ <= 0.0f) return false;
        const float gone = (float)(CACurrentMediaTime() - fadeFrom_);
        if (gone >= fading_) { Stop(); return true; }
        return false;
    }

private:
    static NSData *Wav(const Pcm &p) {
        const uint32_t bytes = (uint32_t)(p.s.size() * 2);
        NSMutableData *d = [NSMutableData dataWithCapacity:44 + bytes];
        auto u32 = [&](uint32_t v) { [d appendBytes:&v length:4]; };
        auto u16 = [&](uint16_t v) { [d appendBytes:&v length:2]; };
        [d appendBytes:"RIFF" length:4]; u32(36 + bytes); [d appendBytes:"WAVE" length:4];
        [d appendBytes:"fmt " length:4]; u32(16); u16(1); u16((uint16_t)p.channels); u32((uint32_t)p.rate);
        u32((uint32_t)(p.rate * p.channels * 2)); u16((uint16_t)(p.channels * 2)); u16(16);
        [d appendBytes:"data" length:4]; u32(bytes);
        [d appendBytes:p.s.data() length:bytes];
        return d;
    }
    bool Submit(Clips &clips, void (*log)(const wchar_t *, ...)) {
        const voice::Clip *c = clip();
        if (!c) return false;
        const Pcm &p = clips.Find(c->name, log);
        if (!p.ok()) return false;
        clipSeconds_ = (float)p.frames() / (float)p.rate;
        stepped_ = 0.0f;
        gen_++;
        if (!state_) return true;
        NSData *wav = Wav(p);
        auto st = state_;
        const uint64_t gen = gen_;
        const float g = gain_;
        NSString *dev = device_.empty() ? nil : [NSString stringWithUTF8String:device_.c_str()];
        std::string name = c->name;
        dispatch_async(Queue(), ^{
            if (st->closed) return;
            if (st->player) [st->player stop];
            NSError *err = nil;
            AVAudioPlayer *pl = [[AVAudioPlayer alloc] initWithData:wav error:&err];
            bool ok = false;
            if (pl) {
                if (dev) pl.currentDevice = dev;
                pl.volume = g;
                [pl prepareToPlay];
                ok = [pl play];
            }
            st->player = ok ? pl : nil;
            if (ok) {
                st->startedAt = CACurrentMediaTime();
                st->startedGen = gen;
            } else {
                std::string why = err ? err.localizedDescription.UTF8String : "play returned NO";
                dispatch_async(dispatch_get_main_queue(), ^{
                    if (log) log(L"voice: %hs would not play (%hs) -- subtitles only for it", name.c_str(), why.c_str());
                });
            }
        });
        return true;
    }

    static dispatch_queue_t Queue() {
        static dispatch_queue_t q = dispatch_queue_create("shittim.voice", DISPATCH_QUEUE_SERIAL);
        return q;
    }
    struct State {
        std::atomic<bool> closed{ false };
        std::atomic<double> startedAt{ 0.0 };
        std::atomic<uint64_t> startedGen{ 0 };
        AVAudioPlayer *player = nil;      // touched only on Queue()
    };
    std::shared_ptr<State> state_;
    std::string device_;
    uint64_t gen_ = 0;
    const voice::Line *line_ = nullptr;
    int clip_ = 0;
    float clipSeconds_ = 0.0f;
    float stepped_ = 0.0f;
    float gain_ = 0.25f;
    float fading_ = 0.0f;
    double fadeFrom_ = 0.0;
};

struct Options {
    bool on = true;
    int volume = 50;
    bool subtitles = true, sceneSubtitles = true, characterSubtitles = true;
    int intervalSeconds = scenes::kVoiceIdleIntervalDefault;
    subtitle::Style style;
    subtitle::Language language = subtitle::Language::Chinese;
    bool lipsync = true;
    std::string device;            // voice_device: a Core Audio UID, empty for auto
    bool deviceFallback = false;   // voice_device_fallback
};

struct Frame {
    fx::Phase phase = fx::Phase::Idle;
    bool onStage = false;
    bool (*anchor)(void *ctx, voice::Who who, int *x, int *y, subtitle::Box *face) = nullptr;
    void *anchorCtx = nullptr;
    subtitle::Box stage;
};

class Run {
public:
    bool on() const { return on_; }
    bool Begin(const std::string &clipDir, const std::string &sceneName, const Options &set, int stageHeight,
               void (*log)(const wchar_t *, ...)) {
        End();
        log_ = log;
        stageH_ = stageHeight > 0 ? stageHeight : 1080;
        subtitles_ = set.subtitles;
        sceneSubtitles_ = set.sceneSubtitles;
        characterSubtitles_ = set.characterSubtitles;
        style_ = set.style;
        language_ = set.language;
        lipsync_ = set.lipsync;
        style_.scale = subtitle::ScaleFor(style_, language_);
        if (!set.on) return false;
        const voice::Repertoire *rep = voice::Resolve(sceneName);
        if (!rep) { if (log) log(L"voice: no lines for scene '%hs' -- staying silent", sceneName.c_str()); return false; }
        clips_.Open(clipDir);
        const macaudio::Choice out = macaudio::Resolve(set.device, set.deviceFallback, log);
        if (!player_.Open(set.volume, out.uid, out.silent, log)) return false;
        seed_ = (uint32_t)(arc4random());
        director_.Begin(rep, set.intervalSeconds, seed_);
        on_ = true;
        if (log) log(L"voice: on for %hs, every %ds, seed %08X", sceneName.c_str(), set.intervalSeconds, seed_);
        return true;
    }
    void End() {
        player_.Close();
        director_.Reset();
        on_ = false;
        speaking_ = nullptr;
        track_ = lipsync::Track{};
        expr_.Reset();
        type_ = subtitle::Typewriter{};
        lastPhase_ = fx::Phase::Idle;
        sawStage_ = false;
        face_ = subtitle::Box{};
    }
    void OnTap() { if (on_) Start(director_.OnTap()); }

    // The room left the screen or the session unlocked: let her go quiet.
    void FadeOut() {
        if (!on_) return;
        if (player_.playing() && !player_.fading()) { player_.FadeOut(); type_.Cancel(); }
    }

    void Step(float dt, const Frame &f) {
        if (!on_) return;
        if (player_.StepFade()) { speaking_ = nullptr; director_.OnFinished(); }
        if (f.onStage && !sawStage_) { sawStage_ = true; Start(director_.OnArrive()); }
        if (f.phase != fx::Phase::Idle && lastPhase_ == fx::Phase::Idle) { Silence(); Start(director_.OnWake()); }
        if (f.phase == fx::Phase::Present && lastPhase_ != fx::Phase::Present) Start(director_.OnPresent());
        lastPhase_ = f.phase;
        switch (player_.Step(dt, clips_, log_)) {
        case Tick::Advanced: BeginClipFromPlayer(); break;
        case Tick::Finished: speaking_ = nullptr; director_.OnFinished(); break;
        case Tick::Nothing: break;
        }
        type_.Step(dt);
        Start(director_.Step(dt, f.phase));
        expr_.Step(dt, speaking_ ? speaking_->expression : nullptr);
        if (pendingAnchor_ && f.anchor) {
            int x = anchorX_, y = anchorY_;
            subtitle::Box face;
            if (speaking_ && f.anchor(f.anchorCtx, speaking_->who, &x, &y, &face)) {
                anchorX_ = x; anchorY_ = y; face_ = face;
            }
            pendingAnchor_ = false;
        }
        if (subtitleUp())
            place_ = subtitle::Place(solved_, anchorX_, anchorY_, f.stage, nullptr, 0, face_.empty() ? nullptr : &face_);
        else
            place_ = subtitle::Placement{};
    }

    bool subtitleWanted() const { return subtitles_ && (voice::CueIsRoom(cue_) ? sceneSubtitles_ : characterSubtitles_); }
    bool subtitleUp() const { return on_ && subtitleWanted() && speaking_ && !type_.gone(); }
    const subtitle::Placement &placement() const { return place_; }
    const subtitle::Style &style() const { return style_; }
    const subtitle::Solved &solved() const { return solved_; }
    const char *text() const { return speaking_ ? said_ : nullptr; }
    int visibleChars() const { return type_.visible(); }
    float alpha() const { return type_.alpha(); }
    const char *expression() const { return on_ ? expr_.Current() : nullptr; }
    bool mouth(float ahead, lipsync::Pose *out) const {
        if (!on_ || !lipsync_ || !out) return false;
        if (speaking_ && !track_.empty()) {
            *out = track_.At(player_.clipPosition() + ahead);
            out->open *= player_.fadeLeft();
            return true;
        }
        if (expr_.Holding()) { *out = lipsync::Pose{ 0.0f }; return true; }
        return false;
    }
    bool speaking() const { return on_ && speaking_ != nullptr; }

private:
    void Start(const voice::Wanted &w) {
        if (!w) return;
        if (!player_.Play(*w.line, clips_, log_)) { director_.OnFailed(); return; }
        cue_ = w.cue;
        if (log_) log_(L"voice: %hs -- %hs", voice::CueName(w.cue), w.line->clip[0].name);
        BeginClipFromPlayer();
    }
    void Silence() {
        const bool was = speaking_ != nullptr;
        player_.Stop();
        type_.Cancel();
        speaking_ = nullptr;
        track_ = lipsync::Track{};
        if (was) director_.OnFinished();
    }
    void BeginClipFromPlayer() {
        speaking_ = player_.clip();
        track_ = lipsync::Track{};
        if (!speaking_) return;
        if (lipsync_ && speaking_->expression) {
            const Pcm &pcm = clips_.Find(speaking_->name, log_);
            if (pcm.ok()) track_ = lipsync::Plan(pcm.s.data(), pcm.frames(), pcm.channels, pcm.rate);
        }
        said_ = voice::SubtitleText(*speaking_, language_);
        solved_ = subtitle::Solve(said_, stageH_, &subtitle::Estimate, nullptr, style_.scale);
        type_.Begin(said_, player_.clipSeconds());
        pendingAnchor_ = subtitleWanted();
    }

    Clips clips_;
    Player player_;
    voice::Director director_;
    void (*log_)(const wchar_t *, ...) = nullptr;
    bool on_ = false;
    const voice::Clip *speaking_ = nullptr;
    voice::Cue cue_ = voice::Cue::None;
    lipsync::Track track_;
    expression::Keeper expr_;
    subtitle::Typewriter type_;
    subtitle::Solved solved_;
    subtitle::Placement place_;
    subtitle::Style style_;
    subtitle::Language language_ = subtitle::Language::Chinese;
    subtitle::Box face_;
    const char *said_ = "";
    int stageH_ = 1080;
    bool subtitles_ = true, sceneSubtitles_ = true, characterSubtitles_ = true, lipsync_ = true;
    fx::Phase lastPhase_ = fx::Phase::Idle;
    bool sawStage_ = false;
    bool pendingAnchor_ = false;
    int anchorX_ = 0, anchorY_ = 0;
    uint32_t seed_ = 0;
};

} // namespace macvoice
