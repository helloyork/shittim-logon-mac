// shittim-logon / macos / src / audiodev_mac.h
//
// `voice_device` and `voice_device_fallback` on the Mac: overlay/src/audiodev.h's
// Resolve, decision for decision, over Core Audio instead of MMDevice. The value is a
// Core Audio device UID (kAudioDevicePropertyDeviceUID) where Windows has an endpoint
// id; empty is `auto`, the system's default output. The music uses the same setting,
// as on Windows.
//
//   auto                         the default output
//   a device, connected          that device
//   a device, not connected      silent -- the subtitles carry on (R61: never quietly
//                                play out of another speaker)
//   ...with voice_device_fallback  the default output until it is back
//
// The settings app lists devices with Outputs().

#pragma once

#import <CoreAudio/CoreAudio.h>
#import <Foundation/Foundation.h>

#include <string>
#include <vector>

namespace macaudio {

struct Device {
    std::string uid, name;
};

inline std::string StringProp(AudioObjectID id, AudioObjectPropertySelector sel) {
    AudioObjectPropertyAddress a = { sel, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain };
    CFStringRef s = nullptr;
    UInt32 size = sizeof s;
    if (AudioObjectGetPropertyData(id, &a, 0, nullptr, &size, &s) != noErr || !s) return std::string();
    NSString *ns = CFBridgingRelease(s);
    return ns.UTF8String ? ns.UTF8String : "";
}

// Every device that can play sound, connected now.
inline std::vector<Device> Outputs() {
    std::vector<Device> out;
    AudioObjectPropertyAddress a = { kAudioHardwarePropertyDevices, kAudioObjectPropertyScopeGlobal,
                                     kAudioObjectPropertyElementMain };
    UInt32 size = 0;
    if (AudioObjectGetPropertyDataSize(kAudioObjectSystemObject, &a, 0, nullptr, &size) != noErr) return out;
    std::vector<AudioObjectID> ids(size / sizeof(AudioObjectID));
    if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &a, 0, nullptr, &size, ids.data()) != noErr) return out;
    for (AudioObjectID id : ids) {
        AudioObjectPropertyAddress streams = { kAudioDevicePropertyStreams, kAudioObjectPropertyScopeOutput,
                                               kAudioObjectPropertyElementMain };
        UInt32 n = 0;
        if (AudioObjectGetPropertyDataSize(id, &streams, 0, nullptr, &n) != noErr || n == 0) continue;
        AudioObjectPropertyAddress alive = { kAudioDevicePropertyDeviceIsAlive, kAudioObjectPropertyScopeGlobal,
                                             kAudioObjectPropertyElementMain };
        UInt32 on = 0, sz = sizeof on;
        if (AudioObjectGetPropertyData(id, &alive, 0, nullptr, &sz, &on) == noErr && !on) continue;
        Device d;
        d.uid = StringProp(id, kAudioDevicePropertyDeviceUID);
        d.name = StringProp(id, kAudioObjectPropertyName);
        if (!d.uid.empty()) out.push_back(d);
    }
    return out;
}

struct Choice {
    std::string uid;       // the device to play out of; empty is the default output
    bool silent = false;   // a device was chosen and is not there
};

// audiodev::Resolve.
inline Choice Resolve(const std::string &configured, bool fallback, void (*log)(const wchar_t *, ...)) {
    Choice c;
    if (configured.empty() || configured == "auto") return c;
    const Device *found = nullptr;
    const std::vector<Device> all = Outputs();
    for (const Device &d : all) if (d.uid == configured) { found = &d; break; }
    if (found) {
        c.uid = configured;
        if (log) log(L"voice: output '%hs' -- %hs", found->name.c_str(),
                     fallback ? "preferred in the settings, and connected" : "chosen in the settings");
        return c;
    }
    if (fallback) {
        if (log) log(L"voice: the preferred output '%hs' is not connected -- following the default until it is back",
                     configured.c_str());
        return c;
    }
    c.silent = true;
    if (log) log(L"voice: voice_device names an output that is not connected (%hs) -- she stays silent until it "
                 L"is back, subtitles carry on", configured.c_str());
    return c;
}

} // namespace macaudio
