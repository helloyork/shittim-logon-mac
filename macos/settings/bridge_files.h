// shittim-logon / macos / settings / bridge_files.h
//
// The files the settings app puts where the overlay reads them, and the one it hands
// out: the Mac halves of config/src/audioimport.h + bgmcommit.h (an imported track),
// voicesource.h + voicecommit.h (an imported voice pack), fontimport.h + fontcommit.h
// (an imported typeface) and logexport.h (the zip in Downloads). Included by bridge.mm
// after its text helpers (Tr, Fill, NS, W, U, Wide, AssetDir, Exists); the methods that
// call these are in bridge.mm.
//
// What is the same as Windows: the slot names (bgm-<id>.pak, clock-font-custom.ttf,
// subtitle-font-custom.ttf), the id rule, the checks and the sentence each failure is
// reported in, and every write going to a temporary file first and into place by rename.
//
// What is not, and why:
//   * No elevation. On Windows the slots are administrators only (R93) because a SYSTEM
//     process on the Winlogon desktop parses them. The Mac overlay is the signed-in
//     person's own process, so the person's own settings directory is the right place,
//     and the "needs an administrator" lines are not shown.
//   * Tracks are written as a version 1 pack -- decoded PCM, silence already trimmed --
//     which is what macos/src/bgm_mac.h reads. Windows writes version 2 (the encoded
//     file plus the trim as two numbers) since the decoder moved to the overlay; the
//     Mac overlay has no reason to decode at lock time, Core Audio decodes here.
//   * A voice pack stays as its clips: the Mac overlay reads one file per clip
//     (voice_mac.h), so a .slvp's clips are written out as files into voice-custom/
//     rather than baked into one PCM pack. A baked .pak is the Windows installer's
//     product and is not accepted here.

#pragma once

#import <AudioToolbox/AudioToolbox.h>
#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

#define STB_VORBIS_NO_PUSHDATA_API
#include "../../vendor/stb/stb_vorbis.c"
#undef L
#undef C
#undef R

#include "../../render/src/voicelines.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

static std::string DataDir() { return macconfig::Dir().UTF8String; }

static uint32_t Fnv1a(const uint8_t *data, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= data[i]; h *= 16777619u; }
    return h;
}

static double FileBytes(const std::string &path) {
    NSDictionary *a = [NSFileManager.defaultManager attributesOfItemAtPath:NS(path) error:nil];
    return a ? [a[NSFileSize] doubleValue] : -1.0;
}

// A temporary sibling, written whole, then renamed over the destination: a failure at any
// point leaves the old file as it was (audioimport.h's "a temporary file first").
static bool WriteInPlace(const std::string &dest, const std::vector<uint8_t> &bytes) {
    const std::string temp = dest + ".partial";
    FILE *f = std::fopen(temp.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(bytes.data(), 1, bytes.size(), f) == bytes.size();
    if (std::fclose(f) != 0 || !ok || std::rename(temp.c_str(), dest.c_str()) != 0) {
        std::remove(temp.c_str());
        return false;
    }
    return true;
}

static bool StartsWith4(const std::string &path, const char magic[4]) {
    FILE *f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    char b[4] = {};
    const bool yes = std::fread(b, 1, 4, f) == 4 && std::memcmp(b, magic, 4) == 0;
    std::fclose(f);
    return yes;
}

// ---------------------------------------------------------------- tracks

// bgmcommit::IdIsSafe and IdFor, as they are.
static bool IdIsSafe(const std::string &id) {
    if (id.empty() || id.size() > 32) return false;
    for (char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) return false;
    return id != "." && id != "..";
}
static std::string IdFor(NSString *path, const std::vector<std::string> &taken) {
    const std::wstring name = W(path.lastPathComponent.stringByDeletingPathExtension);
    std::string id;
    for (wchar_t c : name) {
        if (c >= L'A' && c <= L'Z') c = (wchar_t)(c - L'A' + L'a');
        if ((c >= L'a' && c <= L'z') || (c >= L'0' && c <= L'9') || c == L'-' || c == L'_')
            id.push_back((char)c);
        else if (!id.empty() && id.back() != '-')
            id.push_back('-');
        if (id.size() >= 24) break;
    }
    while (!id.empty() && id.back() == '-') id.pop_back();
    if (!IdIsSafe(id) || id == "custom") id.clear();
    const auto used = [&taken](const std::string &x) { return std::find(taken.begin(), taken.end(), x) != taken.end(); };
    if (!id.empty() && !used(id)) return id;
    const std::string stem = id.empty() ? std::string("track") : id;
    for (int n = 1; n < 1000; n++) {
        const std::string tryId = stem + std::to_string(n);
        if (IdIsSafe(tryId) && !used(tryId)) return tryId;
    }
    return std::string();
}

// audioimport.h's limits: 48000 stereo, at most 256 MB of samples (about 23 minutes),
// and |sample| <= 4 counted as silence at the two ends (ScanSilence).
static constexpr uint32_t kTrackRate = 48000, kTrackChannels = 2;
static constexpr unsigned long long kTrackMaxBytes = 256ull * 1024ull * 1024ull;
static constexpr int kSilence = 4;

struct TrackImport {
    bool ok = false;
    std::wstring problem, title;
    double seconds = 0, bytes = 0;
};

// The file's own name, trimmed to fit the header's 128 bytes on a character boundary
// (TitleFor, PackTitle).
static void PackTitle(NSString *title, char out[128]) {
    std::memset(out, 0, 128);
    std::string kept;
    for (NSUInteger at = 0; at < title.length;) {
        const NSRange r = [title rangeOfComposedCharacterSequenceAtIndex:at];
        const char *piece = [title substringWithRange:r].UTF8String;
        if (!piece || kept.size() + std::strlen(piece) > 127) break;
        kept += piece;
        at = NSMaxRange(r);
    }
    std::memcpy(out, kept.data(), kept.size());
}

// CopyTrack: decode with Core Audio (ExtAudioFile converts to the client format,
// resampling included), trim, write the pack with its magic.
static TrackImport ImportTrackFile(NSString *source, const std::string &dest) {
    TrackImport r;
    if (StartsWith4(U(source), "OggS")) {
        r.problem = Tr(L"锁定屏幕无法播放 ogg 与 opus 格式的曲目，请先转换为 mp3 等格式再导入。");
        return r;
    }
    ExtAudioFileRef in = nullptr;
    if (ExtAudioFileOpenURL((__bridge CFURLRef)[NSURL fileURLWithPath:source], &in) != noErr || !in) {
        r.problem = Tr(L"无法读取此音频文件。");
        return r;
    }
    AudioStreamBasicDescription want = {};
    want.mSampleRate = kTrackRate;
    want.mFormatID = kAudioFormatLinearPCM;
    want.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
    want.mChannelsPerFrame = kTrackChannels;
    want.mBitsPerChannel = 16;
    want.mBytesPerFrame = want.mBytesPerPacket = 2 * kTrackChannels;
    want.mFramesPerPacket = 1;
    if (ExtAudioFileSetProperty(in, kExtAudioFileProperty_ClientDataFormat, sizeof want, &want) != noErr) {
        ExtAudioFileDispose(in);
        r.problem = Tr(L"系统解码器无法将此文件转换为本软件所需的格式。");
        return r;
    }
    std::vector<int16_t> pcm;
    std::vector<int16_t> buf(32768 * kTrackChannels);
    bool broken = false, tooLong = false;
    for (;;) {
        AudioBufferList list;
        list.mNumberBuffers = 1;
        list.mBuffers[0].mNumberChannels = kTrackChannels;
        list.mBuffers[0].mDataByteSize = (UInt32)(buf.size() * sizeof(int16_t));
        list.mBuffers[0].mData = buf.data();
        UInt32 frames = 32768;
        if (ExtAudioFileRead(in, &frames, &list) != noErr) { broken = true; break; }
        if (frames == 0) break;
        pcm.insert(pcm.end(), buf.begin(), buf.begin() + frames * kTrackChannels);
        if (pcm.size() * sizeof(int16_t) > kTrackMaxBytes) { tooLong = true; break; }
    }
    ExtAudioFileDispose(in);
    if (tooLong) { r.problem = Tr(L"曲目时长超出上限（约 23 分钟），请选择较短的曲目。"); return r; }
    if (broken)  { r.problem = Tr(L"解码过程中断，文件可能已损坏。"); return r; }

    // ScanSilence: the first and last frame with a sample above the floor.
    const size_t frames = pcm.size() / kTrackChannels;
    size_t first = frames, last = 0;
    for (size_t i = 0; i < pcm.size(); i++) {
        const int v = pcm[i] < 0 ? -(int)pcm[i] : (int)pcm[i];
        if (v > kSilence) {
            const size_t f = i / kTrackChannels;
            if (f < first) first = f;
            last = f;
        }
    }
    if (frames == 0 || first >= frames) {
        r.problem = Tr(L"解码结果为空，此文件可能不含有效音频。");
        return r;
    }
    const size_t take = last + 1 - first;
    const uint8_t *payload = (const uint8_t *)(pcm.data() + first * kTrackChannels);
    const size_t payloadBytes = take * kTrackChannels * sizeof(int16_t);

    // bgmpack.h version 1: 32 bytes of numbers, 128 of title, then the samples. The magic
    // is the last thing filled in, so a file cut short is one the overlay refuses.
    std::vector<uint8_t> out(160 + payloadBytes);
    uint32_t h[8] = { 0x47424C53u, 1, kTrackRate, kTrackChannels, (uint32_t)take,
                      Fnv1a(payload, payloadBytes), 160, (uint32_t)payloadBytes };
    std::memcpy(out.data(), h, sizeof h);
    NSString *title = source.lastPathComponent.stringByDeletingPathExtension;
    PackTitle(title, (char *)out.data() + 32);
    std::memcpy(out.data() + 160, payload, payloadBytes);
    if (!WriteInPlace(dest, out)) {
        r.problem = Tr(L"写入音频包失败。");
        return r;
    }
    r.ok = true;
    r.title = W(NS(std::string((const char *)out.data() + 32)));
    r.seconds = (double)take / kTrackRate;
    r.bytes = (double)out.size();
    return r;
}

// ---------------------------------------------------------------- voice packs

// voicesource.h's container, "SLVS" version 1, with its limits.
static constexpr uint32_t kSlvsMagic = 0x53564C53u, kSlvsMaxCount = 512, kSlvsMaxClip = 16u << 20;
static constexpr uint64_t kSlvsMaxFile = 256ull << 20;

static std::string VoiceSlot() { return DataDir() + "/voice-custom"; }

struct VoiceImport {
    bool ok = false;
    std::wstring problem, title;
    int clips = 0, broken = 0;
    double bytes = 0;
};

// A clip, decodable or not: stb_vorbis for the game's own .ogg (what the overlay decodes
// them with), Core Audio for an older pack's .mp3.
static bool ClipDecodes(const std::string &path, double *seconds) {
    *seconds = 0;
    if (StartsWith4(path, "OggS")) {
        int err = 0;
        stb_vorbis *v = stb_vorbis_open_filename(path.c_str(), &err, nullptr);
        if (!v) return false;
        *seconds = stb_vorbis_stream_length_in_seconds(v);
        stb_vorbis_close(v);
        return *seconds > 0;
    }
    ExtAudioFileRef f = nullptr;
    if (ExtAudioFileOpenURL((__bridge CFURLRef)[NSURL fileURLWithPath:NS(path)], &f) != noErr || !f) return false;
    SInt64 frames = 0;
    AudioStreamBasicDescription fmt = {};
    UInt32 n = sizeof frames, m = sizeof fmt;
    const bool ok = ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileLengthFrames, &n, &frames) == noErr &&
                    ExtAudioFileGetProperty(f, kExtAudioFileProperty_FileDataFormat, &m, &fmt) == noErr &&
                    frames > 0 && fmt.mSampleRate > 0;
    if (ok) *seconds = (double)frames / fmt.mSampleRate;
    ExtAudioFileDispose(f);
    return ok;
}

// ReadAll + ParseBundle + MissingFrom, then the clips out as files, into a fresh directory
// that replaces the slot by rename only once every clip is written.
static VoiceImport ImportVoiceFile(NSString *source) {
    VoiceImport r;
    if ([source.pathExtension.lowercaseString isEqualToString:@"pak"]) {
        r.problem = Tr(L"这个语音包文件无法读取，可能已损坏或来自其他版本。");
        return r;
    }
    const double size = FileBytes(U(source));
    if (size < 0) { r.problem = Tr(L"无法打开所选文件。"); return r; }
    if (size == 0) { r.problem = Tr(L"所选文件为空。"); return r; }
    if (size > (double)kSlvsMaxFile) { r.problem = Tr(L"所选文件过大，不是本软件的语音包。"); return r; }
    NSData *data = [NSData dataWithContentsOfFile:source];
    if (!data) { r.problem = Tr(L"读取所选文件失败。"); return r; }
    const uint8_t *raw = (const uint8_t *)data.bytes;
    const size_t len = data.length;

    if (len < 160) { r.problem = Tr(L"文件太小，不是语音包。"); return r; }
    uint32_t h[8];
    std::memcpy(h, raw, sizeof h);
    const uint32_t count = h[2], hash = h[3], blobOffset = h[4], blobBytes = h[5];
    if (h[0] != kSlvsMagic) { r.problem = Tr(L"这不是语音包文件。"); return r; }
    if (h[1] != 1) { r.problem = Tr(L"语音包的格式版本本软件无法读取，请使用与本版本匹配的语音包。"); return r; }
    if (count == 0 || count > kSlvsMaxCount) { r.problem = Tr(L"语音包的片段数量异常。"); return r; }
    const uint64_t indexEnd = 160ull + (uint64_t)count * 64, blobEnd = (uint64_t)blobOffset + blobBytes;
    if (indexEnd > len || blobOffset < indexEnd || blobEnd > len) {
        r.problem = Tr(L"语音包的内容超出文件范围，文件可能未下载完整。");
        return r;
    }
    struct Clip { std::string name; uint32_t offset, bytes; };
    std::vector<Clip> clips;
    for (uint32_t i = 0; i < count; i++) {
        const uint8_t *e = raw + 160 + (size_t)i * 64;
        const char *name = (const char *)e;
        if (strnlen(name, 48) == 48) { r.problem = Tr(L"语音包的片段名称异常。"); return r; }
        uint32_t off, bytes;
        std::memcpy(&off, e + 48, 4);
        std::memcpy(&bytes, e + 52, 4);
        if (bytes == 0 || bytes > kSlvsMaxClip || (uint64_t)off + bytes > blobBytes) {
            r.problem = Tr(L"语音包的片段内容超出文件范围，文件可能未下载完整。");
            return r;
        }
        clips.push_back({ name, off, bytes });
    }
    if (Fnv1a(raw + blobOffset, blobBytes) != hash) {
        r.problem = Tr(L"语音包的校验值不符，文件可能已损坏或未下载完整。");
        return r;
    }
    // Every name is one of the line table's (they become file names here), and every name
    // the table asks for is there.
    const std::vector<std::string> table = voice::AllClipNames();
    for (const Clip &c : clips)
        if (std::find(table.begin(), table.end(), c.name) == table.end()) {
            r.problem = Tr(L"语音包中有名称异常的片段，未导入。");
            return r;
        }
    for (const std::string &n : table)
        if (std::find_if(clips.begin(), clips.end(), [&](const Clip &c) { return c.name == n; }) == clips.end()) {
            r.problem = Tr(L"这个语音包缺少本版本需要的片段，未导入。");
            return r;
        }

    NSFileManager *fm = NSFileManager.defaultManager;
    const std::string fresh = VoiceSlot() + ".partial", old = VoiceSlot() + ".old";
    [fm removeItemAtPath:NS(fresh) error:nil];
    [fm removeItemAtPath:NS(old) error:nil];
    if (![fm createDirectoryAtPath:NS(fresh) withIntermediateDirectories:YES attributes:nil error:nil]) {
        r.problem = Tr(L"无法创建临时目录。");
        return r;
    }
    for (const Clip &c : clips) {
        const uint8_t *b = raw + blobOffset + c.offset;
        const bool ogg = c.bytes >= 4 && std::memcmp(b, "OggS", 4) == 0;
        const std::string path = fresh + "/" + c.name + (ogg ? ".ogg" : ".mp3");
        FILE *f = std::fopen(path.c_str(), "wb");
        const bool ok = f && std::fwrite(b, 1, c.bytes, f) == c.bytes;
        if (f && std::fclose(f) != 0) { [fm removeItemAtPath:NS(fresh) error:nil]; r.problem = Tr(L"无法写入临时文件，磁盘空间可能不足。"); return r; }
        if (!ok) { [fm removeItemAtPath:NS(fresh) error:nil]; r.problem = Tr(L"无法写入临时文件，磁盘空间可能不足。"); return r; }
        double seconds = 0;
        if (!ClipDecodes(path, &seconds)) r.broken++;
        r.bytes += c.bytes;
    }
    r.title = W(NS(std::string((const char *)raw + 32, strnlen((const char *)raw + 32, 128))));
    [[NS(r.title) dataUsingEncoding:NSUTF8StringEncoding] writeToFile:NS(fresh + "/title.txt") atomically:YES];
    // The swap: the old slot aside, the new one in, the old one gone.
    if (Exists(VoiceSlot()) && ![fm moveItemAtPath:NS(VoiceSlot()) toPath:NS(old) error:nil]) {
        [fm removeItemAtPath:NS(fresh) error:nil];
        r.problem = Tr(L"无法导入：该文件正被锁定屏幕使用，请锁定一次屏幕后重试");
        return r;
    }
    if (![fm moveItemAtPath:NS(fresh) toPath:NS(VoiceSlot()) error:nil]) {
        [fm moveItemAtPath:NS(old) toPath:NS(VoiceSlot()) error:nil];
        [fm removeItemAtPath:NS(fresh) error:nil];
        r.problem = Tr(L"无法写入临时文件，磁盘空间可能不足。");
        return r;
    }
    [fm removeItemAtPath:NS(old) error:nil];
    r.ok = true;
    r.clips = (int)clips.size();
    return r;
}

struct VoicePackInfo { bool ok = false; std::wstring title; int clips = 0; double seconds = 0; };

// ReadVoicePack(custom): what the slot holds, as the 语音包文件 row says it.
static VoicePackInfo ReadVoiceSlot() {
    VoicePackInfo v;
    NSArray *names = [NSFileManager.defaultManager contentsOfDirectoryAtPath:NS(VoiceSlot()) error:nil];
    for (NSString *n in names) {
        NSString *ext = n.pathExtension;
        if (![ext isEqualToString:@"ogg"] && ![ext isEqualToString:@"mp3"]) continue;
        double s = 0;
        if (ClipDecodes(VoiceSlot() + "/" + U(n), &s)) { v.clips++; v.seconds += s; }
    }
    if (v.clips == 0) return v;
    v.ok = true;
    NSString *t = [NSString stringWithContentsOfFile:NS(VoiceSlot() + "/title.txt") encoding:NSUTF8StringEncoding error:nil];
    v.title = W(t ?: @"");
    return v;
}

// ---------------------------------------------------------------- typefaces

// fontslot.h's names, in the settings directory -- where ui_mac.h ResolveFace looks.
static std::string FontSlot(bool clock) {
    return DataDir() + (clock ? "/clock-font-custom.ttf" : "/subtitle-font-custom.ttf");
}

struct FontFile { bool exists = false, loads = false; std::string family, postscript; double bytes = 0; };

static FontFile InspectFont(const std::string &path) {
    FontFile f;
    f.bytes = FileBytes(path);
    f.exists = f.bytes > 0;
    if (!f.exists) return f;
    NSArray *d = CFBridgingRelease(CTFontManagerCreateFontDescriptorsFromURL(
        (__bridge CFURLRef)[NSURL fileURLWithPath:NS(path)]));
    if (d.count == 0) return f;
    NSString *fam = CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)d[0], kCTFontFamilyNameAttribute));
    NSString *ps = CFBridgingRelease(CTFontDescriptorCopyAttribute((__bridge CTFontDescriptorRef)d[0], kCTFontNameAttribute));
    f.loads = true;
    f.family = U(fam ?: @"");
    f.postscript = U(ps ?: @"");
    return f;
}

static std::wstring ShortFamily(const std::string &fam) {
    const std::wstring w = Wide(fam);
    return w.size() > 18 ? w.substr(0, 17) + L"…" : w;   // i18n: no
}

static std::wstring MegaBytes(double bytes) {
    char b[32];
    std::snprintf(b, sizeof b, "%.1f", bytes / (1024.0 * 1024.0));
    return Wide(b);
}

// The face ui_mac.h ResolveFace lands on, for the preview: registered for this process,
// and named by PostScript name; nil for the system font.
static NSString *PreviewFace(const std::string &want, const std::string &factory, const std::string &custom) {
    auto registered = [](const std::string &path) -> NSString * {
        const FontFile f = InspectFont(path);
        if (!f.loads) return nil;
        CFErrorRef err = nullptr;
        if (!CTFontManagerRegisterFontsForURL((__bridge CFURLRef)[NSURL fileURLWithPath:NS(path)],
                                              kCTFontManagerScopeProcess, &err) && err)
            CFRelease(err);   // already registered: the same file, so the same face
        return NS(f.postscript);
    };
    switch (subtitle::FontSourceOf(want)) {
        case subtitle::FontSource::Factory: return registered(factory);
        case subtitle::FontSource::System:  return nil;
        case subtitle::FontSource::Custom: {
            NSString *n = registered(custom);
            return n ?: registered(factory);
        }
        case subtitle::FontSource::File:    return registered(want);
        case subtitle::FontSource::Family: {
            NSString *fam = NS(want);
            NSArray *members = [NSFontManager.sharedFontManager availableMembersOfFontFamily:fam];
            return members.count ? members[0][0] : nil;
        }
    }
    return nil;
}

// FontStatusFor, said of the Mac: the overlay runs as the signed-in person, so a family
// installed for that person is one it can use, and no file needs to be administrators only.
static std::wstring FontStatusText(const std::string &want, const std::string &factory, const std::string &custom) {
    switch (subtitle::FontSourceOf(want)) {
        case subtitle::FontSource::Factory: {
            const FontFile f = InspectFont(factory);
            if (!f.loads) return Tr(L"安装目录中缺少出厂字体，将使用系统界面字体");
            return Tr(L"随软件附带：") + ShortFamily(f.family);
        }
        case subtitle::FontSource::System:
            return Tr(L"macOS 系统界面字体");
        case subtitle::FontSource::Custom: {
            const FontFile f = InspectFont(custom);
            return f.loads ? Tr(L"已导入：") + ShortFamily(f.family)
                           : std::wstring(Tr(L"尚未导入字体文件，锁定屏幕将使用出厂字体"));
        }
        case subtitle::FontSource::File: {
            if (!Exists(want)) return Tr(L"文件不存在");
            const FontFile f = InspectFont(want);
            if (!f.loads) return Tr(L"无法加载该字体文件");
            return Tr(L"字体文件：") + ShortFamily(f.family);
        }
        case subtitle::FontSource::Family:
        default:
            return [NSFontManager.sharedFontManager availableMembersOfFontFamily:NS(want)].count
                       ? Tr(L"本机已安装该字体")
                       : Tr(L"本机未安装该字体");
    }
}

// FontCopyStatus: the line under 字体文件.
static std::wstring FontCopyText(const std::string &slot) {
    const FontFile f = InspectFont(slot);
    if (f.loads) return ShortFamily(f.family) + L" · " + MegaBytes(f.bytes) + L" MB";   // i18n: no
    if (!f.exists) return Tr(L"尚未导入，点击「浏览…」选择字体文件");
    return Tr(L"已导入的文件无法读取，请重新选择");
}

struct FontImport { bool ok = false; std::wstring problem, family; double bytes = 0; };

// fontimport::CopyFont: the picked file read as a font, copied into the slot, and read
// again from there.
static FontImport ImportFontFile(NSString *source, const std::string &slot) {
    FontImport r;
    NSString *a = [source stringByResolvingSymlinksInPath], *b = [NS(slot) stringByResolvingSymlinksInPath];
    if ([a isEqualToString:b]) { r.problem = Tr(L"所选文件即为当前使用的自定义字体。"); return r; }
    const double size = FileBytes(U(source));
    if (size <= 0) { r.problem = Tr(L"无法读取所选文件。"); return r; }
    if (size > 256.0 * 1024 * 1024) { r.problem = Tr(L"字体文件超过 256 MB，无法导入。"); return r; }
    const FontFile picked = InspectFont(U(source));
    if (!picked.loads) { r.problem = Tr(L"无法识别所选文件中的字体。"); return r; }
    NSData *data = [NSData dataWithContentsOfFile:source];
    if (!data) { r.problem = Tr(L"无法读取所选文件。"); return r; }
    std::vector<uint8_t> bytes((const uint8_t *)data.bytes, (const uint8_t *)data.bytes + data.length);
    if (!WriteInPlace(slot, bytes)) { r.problem = Tr(L"写入字体文件失败，请重新选择文件。"); return r; }
    const FontFile copied = InspectFont(slot);
    if (!copied.loads) { r.problem = Tr(L"复制后的字体文件无法读取，请重新选择文件。"); return r; }
    r.ok = true;
    r.family = Wide(copied.family);
    r.bytes = copied.bytes;
    return r;
}

// configwindow.h Bytes: one decimal below ten, none above, never a bare byte count past 1 KB.
static std::wstring Bytes(double n) {
    char b[48];
    if (n < 1024) return Fill(Tr(L"%llu 字节"), std::to_wstring((unsigned long long)n));
    const double kb = n / 1024.0;
    if (kb < 1024.0) std::snprintf(b, sizeof b, kb < 10.0 ? "%.1f KB" : "%.0f KB", kb);
    else std::snprintf(b, sizeof b, kb / 1024.0 < 10.0 ? "%.1f MB" : "%.0f MB", kb / 1024.0);
    return Wide(b);
}

// ---------------------------------------------------------------- the log export

// logexport.h's cap: each file read to at most 20 MiB, from the end, saying so on its
// first line when it was cut.
static constexpr unsigned long long kPerFile = 20ull << 20;

static NSData *Tail(const std::string &path, bool *cut) {
    *cut = false;
    NSFileHandle *h = [NSFileHandle fileHandleForReadingAtPath:NS(path)];
    if (!h) return nil;
    const unsigned long long size = [h seekToEndOfFile];
    if (size > kPerFile) {
        *cut = true;
        [h seekToFileOffset:size - kPerFile];
    } else {
        [h seekToFileOffset:0];
    }
    NSData *d = [h readDataToEndOfFile];
    [h closeFile];
    if (!*cut) return d;
    NSMutableData *m = [[NSString stringWithFormat:@"[only the last %llu bytes of %llu are included]\n", kPerFile, size]
                           dataUsingEncoding:NSUTF8StringEncoding].mutableCopy;
    [m appendData:d];
    return m;
}

struct LogExport { bool ok = false, truncated = false; std::wstring why; std::string path; double bytes = 0; };

// Run: the files staged into a folder, the folder zipped by ditto into Downloads.
static LogExport ExportLogFiles(const std::string &logPath, const std::string &settingsPath, const std::string &report) {
    LogExport r;
    NSFileManager *fm = NSFileManager.defaultManager;
    NSDateFormatter *df = [NSDateFormatter new];
    df.dateFormat = @"yyyyMMdd-HHmm";
    df.locale = [NSLocale localeWithLocaleIdentifier:@"en_US_POSIX"];
    NSString *stem = [@"ShittimLogon-logs-" stringByAppendingString:[df stringFromDate:NSDate.date]];
    NSURL *downloads = [fm URLsForDirectory:NSDownloadsDirectory inDomains:NSUserDomainMask].firstObject;
    NSString *stage = [NSTemporaryDirectory() stringByAppendingPathComponent:stem];
    [fm removeItemAtPath:stage error:nil];
    if (!downloads) {
        r.why = Tr(L"找不到「下载」文件夹");
        return r;
    }
    if (![fm createDirectoryAtPath:[stage stringByAppendingPathComponent:@"logs"] withIntermediateDirectories:YES
                        attributes:nil error:nil]) {
        r.why = Tr(L"无法创建临时目录。");
        return r;
    }
    [[NSData dataWithBytes:report.data() length:report.size()] writeToFile:[stage stringByAppendingPathComponent:@"report.txt"]
                                                                 atomically:YES];
    // The log and the generation before it (host.h rolls at 1.5 MiB, so the cap on each
    // here is never reached in practice).
    bool cut = false;
    for (const std::string &each : { logPath + ".1", logPath }) {
        bool one = false;
        if (NSData *log = Tail(each, &one))
            [log writeToFile:[stage stringByAppendingPathComponent:[@"logs/" stringByAppendingString:NS(each).lastPathComponent]]
                  atomically:YES];
        cut = cut || one;
    }
    r.truncated = cut;
    [fm copyItemAtPath:NS(settingsPath) toPath:[stage stringByAppendingPathComponent:@"settings.txt"] error:nil];

    NSString *zip = [downloads.path stringByAppendingPathComponent:[stem stringByAppendingPathExtension:@"zip"]];
    [fm removeItemAtPath:zip error:nil];
    NSTask *t = [NSTask new];
    t.executableURL = [NSURL fileURLWithPath:@"/usr/bin/ditto"];
    // No ._ files and no extended attributes: somebody on Windows opens this.
    t.arguments = @[ @"-c", @"-k", @"--norsrc", @"--noextattr", @"--noacl", @"--keepParent", stage, zip ];
    NSError *e = nil;
    const bool ran = [t launchAndReturnError:&e];
    if (ran) [t waitUntilExit];
    [fm removeItemAtPath:stage error:nil];
    if (!ran || t.terminationStatus != 0 || !Exists(U(zip))) {
        r.why = Tr(L"无法在「下载」文件夹创建文件");
        return r;
    }
    r.ok = true;
    r.path = U(zip);
    r.bytes = FileBytes(r.path);
    return r;
}
