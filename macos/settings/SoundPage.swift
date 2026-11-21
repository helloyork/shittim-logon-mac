// shittim-logon / macos / settings / SoundPage.swift
//
// configwindow.h BuildSound and BuildVoice: the output device, the music and its
// tracks (importing and removing them included), then the voice and its pack. Not here:
// the boot-time wait lines (a Windows sign-in screen waits for USB audio; a Mac lock
// screen has nothing to wait for) and the link to 「字幕外观」, which is in the sidebar.

import SwiftUI

/// PercentSlider: 0 to 100 in whole steps, written when the drag ends.
struct PercentRow: View {
    let title: String
    var detail: String = ""
    @Binding var value: Int
    @State private var live: Double? = nil
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                Slider(value: Binding(get: { live ?? Double(value) }, set: { live = $0.rounded() }), in: 0...100) { editing in
                    if !editing, let v = live { value = Int(v); live = nil }
                }
                .frame(width: 180)
                Text("\(Int(live ?? Double(value)))").monospacedDigit().foregroundStyle(.secondary)
                    .frame(width: 32, alignment: .trailing)
            }
        } label: { Label2(title: title, detail: detail) }
    }
}

struct SoundPage: View {
    @EnvironmentObject var store: Store

    var body: some View {
        let _ = store.revision
        Form {
            output
            music
            tracks
            voice
        }
        .formStyle(.grouped)
        .navigationTitle(T("声音"))
    }

    // ---- 输出设备

    private var output: some View {
        let devices = store.s.outputDevices()
        let chosen = store.value("voice_device")
        var labels = [T("自动")]
        var uids = [""]
        for d in devices { uids.append(d[0]); labels.append(d[1]) }
        if !chosen.isEmpty && !uids.contains(chosen) {
            uids.append(chosen); labels.append(chosen + T("（缺失）"))
        }
        var state = 0
        let status = store.s.outputStatus(&state)
        let sel = Binding<Int>(get: { uids.firstIndex(of: chosen) ?? 0 },
                               set: { store.string("voice_device").wrappedValue = $0 == 0 ? "auto" : uids[$0] })
        return Section {
            ChoiceRow(title: T("输出设备"), detail: status, options: labels, selected: sel)
            if !chosen.isEmpty {
                SwitchRow(title: T("未连接时改用默认设备"), detail: T("关闭则保持静音，直到该设备重新连接"),
                          on: store.bool("voice_device_fallback"))
            }
        } footer: {
            Note(text: T("角色语音与背景音乐共用此输出设备。"))
        }
    }

    // ---- 背景音乐

    private var music: some View {
        Section(T("背景音乐")) {
            SwitchRow(title: T("背景音乐"), detail: T("锁定屏幕显示时淡入并持续播放，出厂默认关闭"), on: store.bool("bgm"))
            PercentRow(title: T("音量"), detail: T("仅影响背景音乐，与角色语音音量相互独立"), value: store.int("bgm_volume"))
            PercentRow(title: T("语音播放时的音量"), detail: T("角色语音期间背景音乐降低到的比例，100 为不降低"),
                       value: store.int("bgm_duck"))
        }
    }

    private var tracks: some View {
        Section {
            Note(text: T("勾选的曲目为默认曲目，时段未单独指定曲目时使用；勾选多首时随机播放。"))
            ForEach(store.s.tracks(), id: \.self) { t in
                HStack {
                    Toggle(isOn: Binding(get: { store.s.isDefaultTrack(t[0]) },
                                         set: { on in
                                             if !store.s.setDefaultTrack(t[0], on: on) { store.say(T("至少选择一首默认曲目")) }
                                             store.edit { _ in }
                                         })) {
                        Label2(title: t[1], detail: t[2])
                    }
                    .toggleStyle(.checkbox)
                    Spacer()
                    if store.s.isImportedTrack(t[0]) {
                        Button(T("移除")) { store.say(store.s.removeTrack(t[0])) }
                    }
                }
            }
            LabeledContent {
                Button(T("导入…")) {
                    if let path = pickFile(["mp3", "wav", "flac", "m4a", "aac", "aif", "aiff", "caf"]) {
                        store.say(store.s.importTrack(path))
                    }
                }
            } label: { Label2(title: T("导入曲目"), detail: T("导入的曲目可设为默认曲目，或在任一时段中选择")) }
        } header: {
            Text(T("曲目"))
        } footer: {
            Note(text: T("支持 mp3 / wav / flac / m4a / aiff 格式。"))
        }
    }

    // ---- 语音互动

    private static let intervals = [15, 30, 60, 120, 300, 600, 1800, 3600, 10000]

    private func intervalLabel(_ n: Int) -> String {
        if n < 60 { return T("%d 秒", String(n)) }
        if n < 3600 { return SLSettings.tr("%d 分钟", count: n / 60) }
        return T("%.1f 小时", String(format: "%.1f", Double(n) / 3600))
    }

    private var voice: some View {
        let now = store.int("voice_idle_interval").wrappedValue
        var values = Self.intervals
        if !values.contains(now) { values.append(now); values.sort() }
        let interval = Binding<Int>(get: { values.firstIndex(of: store.int("voice_idle_interval").wrappedValue) ?? 0 },
                                    set: { store.int("voice_idle_interval").wrappedValue = values[$0] })

        // AddVoicePack: the packs there are, then 自定义… -- not a pack but a way of choosing one.
        let packs = store.s.voicePacks() + [["custom", T("自定义…")]]
        let current = store.value("voice_pack")
        let custom = current == "custom"
        let pack = Binding<Int>(get: { packs.firstIndex { $0[0] == current } ?? 0 },
                                set: { store.string("voice_pack").wrappedValue = packs[$0][0] })
        let imported = custom && store.s.hasImportedVoicePack()
        let packMissing = !custom && (packs.first { $0[0] == current }?[1].hasSuffix(T("（缺失）")) ?? false)
        let packDetail = packMissing ? T("此安装包不含该语音包，锁定屏幕将使用出厂语音包")
                       : custom && !imported ? T("尚未导入语音包，锁定屏幕将使用出厂语音包")
                       : !custom && packs.count < 3 ? T("此安装包仅附带一个语音包，也可导入其他语言的语音包")
                       : T("仅更换语音录音，台词内容与字幕不变")

        let langs = ["zh", "zh-TW", "ja", "ko", "en"]
        let lang = Binding<Int>(get: { langs.firstIndex(of: store.value("voice_subtitle_language")) ?? 0 },
                                set: { store.string("voice_subtitle_language").wrappedValue = langs[$0] })
        let subs = store.on("voice_subtitle")
        let off = T("「显示字幕」关闭时不起作用")

        return Section(T("语音互动")) {
            SwitchRow(title: T("角色语音"), detail: T("待机语音与点击应答"), on: store.bool("voice"))
            PercentRow(title: T("音量"), detail: T("仅影响语音"), value: store.int("voice_volume"))
            ChoiceRow(title: T("待机语音间隔"), detail: T("屏幕关闭时暂停计时"),
                      options: values.map(intervalLabel), selected: interval)
            ChoiceRow(title: T("语音包"), detail: packDetail, options: packs.map { $0[1] }, selected: pack)
            // The file row exists only while 自定义 is the answer.
            if custom {
                LabeledContent {
                    HStack {
                        if imported { Button(T("移除")) { store.say(store.s.clearVoicePack()) } }
                        Button(T("浏览…")) {
                            if let path = pickFile(["slvp"]) { store.say(store.s.importVoicePack(path)) }
                        }
                    }
                } label: { Label2(title: T("语音包文件"), detail: store.s.voicePackStatus()) }
                Note(text: T("支持语音包文件（.slvp）。"))
                Note(text: T("导入时文件将被复制至配置目录，此后原文件移动或删除均不影响锁定屏幕。"))
            }
            SwitchRow(title: T("显示字幕"), detail: T("位于角色下方"), on: store.bool("voice_subtitle"))
            SwitchRow(title: T("背景场景的字幕"), detail: subs ? T("房间里的入场、待机与唤醒惊呼") : off,
                      on: store.bool("voice_subtitle_scene")).disabled(!subs)
            SwitchRow(title: T("角色前景的字幕"), detail: subs ? T("角色站定后的问候与点击应答") : off,
                      on: store.bool("voice_subtitle_character")).disabled(!subs)
            ChoiceRow(title: T("字幕语言"), detail: subs ? T("只更换字幕文字，不影响所选语音包") : off,
                      options: [T("中文（简体）"), T("中文（繁體）"), T("日文"), T("韩文"), T("英文")], selected: lang)
                .disabled(!subs)
        }
    }
}
