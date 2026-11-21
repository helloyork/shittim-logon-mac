// shittim-logon / macos / settings / LookPages.swift
//
// configwindow.h BuildSubtitle (「字幕外观」) and BuildScreen (「显示与登录」, here
// 「显示」: the sign-in half of it is Windows'), with the subtitle preview and the
// imported typeface. Left out because the Mac has no counterpart: other
// displays (untested), the account corner, Windows' own background blur and baked
// picture, the dismiss button, Windows Hello.

import AppKit
import SwiftUI

// ---- colours: #RRGGBBAA, subtitle::ToHex's spelling, both ways

func colorFromHex(_ s: String) -> Color {
    var h = s.trimmingCharacters(in: .whitespaces)
    if h.hasPrefix("#") { h.removeFirst() }
    guard h.count == 8 || h.count == 6, let v = UInt64(h, radix: 16) else { return .clear }
    let n = h.count == 6 ? (v << 8) | 0xFF : v
    return Color(.sRGB, red: Double((n >> 24) & 0xFF) / 255, green: Double((n >> 16) & 0xFF) / 255,
                 blue: Double((n >> 8) & 0xFF) / 255, opacity: Double(n & 0xFF) / 255)
}

func hexFromColor(_ c: Color) -> String {
    let ns = NSColor(c).usingColorSpace(.sRGB) ?? .black
    let r = Int((ns.redComponent * 255).rounded()), g = Int((ns.greenComponent * 255).rounded())
    let b = Int((ns.blueComponent * 255).rounded()), a = Int((ns.alphaComponent * 255).rounded())
    return String(format: "#%02X%02X%02X%02X", r, g, b, a)
}

/// AddColorField: a colour with its opacity, written when the picker settles.
struct ColorRow: View {
    @EnvironmentObject var store: Store
    let title: String
    var detail: String = ""
    let key: String
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                Text(store.value(key)).monospaced().foregroundStyle(.secondary)
                ColorPicker("", selection: Binding(get: { colorFromHex(store.value(key)) },
                                                   set: { store.string(key).wrappedValue = hexFromColor($0) }),
                            supportsOpacity: true)
                    .labelsHidden()
            }
        } label: { Label2(title: title, detail: detail) }
    }
}

/// AddCountSlider: whole steps in a small range.
struct CountRow: View {
    let title: String
    var detail: String = ""
    let range: ClosedRange<Int>
    @Binding var value: Int
    @State private var live: Double? = nil
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                Slider(value: Binding(get: { live ?? Double(value) }, set: { live = $0.rounded() }),
                       in: Double(range.lowerBound)...Double(range.upperBound)) { editing in
                    if !editing, let v = live { value = Int(v); live = nil }
                }
                .frame(width: 180)
                Text("\(Int(live ?? Double(value)))").monospacedDigit().foregroundStyle(.secondary)
                    .frame(width: 32, alignment: .trailing)
            }
        } label: { Label2(title: title, detail: detail) }
    }
}

/// AddFontRows, for the subtitle or (`clock`) the clock: the shipped face, the system font,
/// a family or file written by hand when the file names one, and 自定义… -- an imported
/// copy, whose file row is shown only while 自定义 is the answer.
struct FontRows: View {
    @EnvironmentObject var store: Store
    let clock: Bool

    private var key: String { clock ? "clock_font" : "voice_subtitle_font" }

    var body: some View {
        let want = store.value(key)
        let lower = want.lowercased()
        let custom = ["custom", "own", "user"].contains(lower)
        let legacy = !want.isEmpty && !custom && !["factory", "vendor", "default", "system"].contains(lower)
        var labels = [T("出厂字体"), T("系统界面字体")]
        var values = ["factory", "system"]
        if legacy { labels.append(want); values.append(want) }
        labels.append(T("自定义…")); values.append("custom")
        let selected = Binding<Int>(get: {
            if ["", "factory", "vendor", "default"].contains(lower) { return 0 }
            if lower == "system" { return 1 }
            if custom { return values.count - 1 }
            return 2
        }, set: { store.string(key).wrappedValue = values[$0] })
        return Group {
            ChoiceRow(title: T("字体"), detail: store.s.fontStatus(clock), options: labels, selected: selected)
            if custom {
                LabeledContent {
                    HStack {
                        if store.s.hasImportedFont(clock) {
                            Button(T("移除")) { store.say(store.s.clearFont(clock)) }
                        }
                        Button(T("浏览…")) {
                            if let path = pickFile(["ttf", "otf", "ttc", "otc"]) {
                                store.say(store.s.importFont(path, clock: clock))
                            }
                        }
                    }
                } label: { Label2(title: T("字体文件"), detail: store.s.fontCopyStatus(clock)) }
            }
        }
    }
}

/// PreviewCard + PaintSubtitlePreview: the panel as it will look at 1920x1080 and 100%,
/// over the two grounds a subtitle ever sits on (the day room's lightest, the night
/// room's darkest), in the face the overlay will use.
struct SubtitlePreview: View {
    @EnvironmentObject var store: Store
    let scale: Float

    var body: some View {
        let _ = store.revision
        let g = store.s.previewGeometry(scale).map { CGFloat($0.doubleValue) }
        let fileG = store.s.previewGeometry(store.float("voice_subtitle_scale").wrappedValue).map { CGFloat($0.doubleValue) }
        let bg = colorFromHex(store.value("voice_subtitle_background"))
        let border = colorFromHex(store.value("voice_subtitle_border"))
        let text = colorFromHex(store.value("voice_subtitle_text"))
        let stroke = CGFloat(store.int("voice_subtitle_border_width").wrappedValue)
        let corner = CGFloat(store.int("voice_subtitle_corner").wrappedValue)
        let face = store.s.previewFontName()
        let line = store.s.previewLine()
        return GeometryReader { geo in
            let gw = geo.size.width
            let k = gw < g[0] ? gw / g[0] : 1
            ZStack {
                HStack(spacing: 0) { rgb(0xBEDDEE); rgb(0x29405C) }
                ZStack {
                    RoundedRectangle(cornerRadius: corner * k).fill(bg)
                    if stroke > 0 {
                        RoundedRectangle(cornerRadius: corner * k).strokeBorder(border, lineWidth: stroke * k)
                    }
                    Text(line)
                        .font(face.map { Font.custom($0, fixedSize: g[2] * k) } ?? .system(size: g[2] * k))
                        .lineSpacing(max(0, g[5] - g[2] * 1.2) * k)
                        .foregroundStyle(text)
                        .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .leading)
                        .padding(.horizontal, g[3] * k)
                        .padding(.vertical, g[4] * k)
                }
                .frame(width: g[0] * k, height: g[1] * k)
            }
            .clipped()
        }
        // As tall as the panel at the size the file says, plus air: the card does not grow
        // while the 字号 slider under it is being dragged.
        .frame(height: fileG[1] + 40)
        .padding(.vertical, 4)
    }
}

struct SubtitlePage: View {
    @EnvironmentObject var store: Store
    @State private var liveScale: Float? = nil

    var body: some View {
        let _ = store.revision
        let lang = store.value("voice_subtitle_language")
        let scaleDetail = lang == "ja" ? T("日文台词较长，上限比中文低")
                        : lang == "ko" ? T("韩文台词较长，上限比中文低")
                        : lang == "en" ? T("英文台词较长，上限低于默认字号")
                                       : T("超过上限时字幕将遮挡角色面部")
        let fileScale = min(store.float("voice_subtitle_scale").wrappedValue, store.s.scaleMax())
        Form {
            Section {
                SubtitlePreview(scale: liveScale ?? fileScale)
            } header: {
                Text(T("预览：按 1920x1080、100% 缩放时的实际大小绘制")).font(.callout).foregroundStyle(.secondary)
            }
            Section {
                ColorRow(title: T("底色"), detail: T("默认纯黑、55% 不透明"), key: "voice_subtitle_background")
                ColorRow(title: T("边框颜色"), detail: T("不透明度为 0 时不画边框"), key: "voice_subtitle_border")
                CountRow(title: T("边框粗细"), detail: T("边框透明时此项无效"), range: 0...4,
                         value: store.int("voice_subtitle_border_width"))
                ColorRow(title: T("文字颜色"), key: "voice_subtitle_text")
                FontRows(clock: false)
                CountRow(title: T("圆角"), detail: T("0 为直角"), range: 0...16, value: store.int("voice_subtitle_corner"))
                ScaleRow(title: T("字号"), detail: scaleDetail, high: store.s.scaleMax(),
                         value: Binding(get: { fileScale }, set: { store.float("voice_subtitle_scale").wrappedValue = $0 }),
                         live: $liveScale)
            }
        }
        .formStyle(.grouped)
        .navigationTitle(T("字幕外观"))
    }
}

/// The 字号 slider: 0.75 to the language's ceiling (subtitle::ScaleMax) in steps of 0.05,
/// shown as a percentage. A larger value in the file is shown at the ceiling and not
/// rewritten. `live` is the value under the knob while it is dragged, for the preview.
struct ScaleRow: View {
    let title: String
    var detail: String = ""
    var high: Float = 1.25
    @Binding var value: Float
    @Binding var live: Float?
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                Slider(value: Binding(get: { live ?? value }, set: { live = ($0 * 20).rounded() / 20 }),
                       in: 0.75...max(0.8, high)) { e in
                    if !e, let v = live { value = v; live = nil }
                }
                .frame(width: 180)
                Text("\(Int(((live ?? value) * 100).rounded()))%").monospacedDigit().foregroundStyle(.secondary)
                    .frame(width: 44, alignment: .trailing)
            }
        } label: { Label2(title: title, detail: detail) }
    }
}

struct DisplayPage: View {
    @EnvironmentObject var store: Store
    @State private var customScale = false

    /// AddFrameRate's list, capped at the 120 the Mac loop runs at, plus a value written
    /// by hand that is not in it.
    private static func rateList(_ limit: Int) -> [Int] {
        var r = [0, 30, 60, 90, 120]
        if !r.contains(limit) { r.append(limit); r.sort() }
        return r
    }

    var body: some View {
        let _ = store.revision
        let keep = store.on("keep_display_on")
        let scene = store.on("credential_scene")

        let rates = Self.rateList(store.int("frame_rate_limit").wrappedValue)
        let rate = Binding<Int>(get: { rates.firstIndex(of: store.int("frame_rate_limit").wrappedValue) ?? 0 },
                                set: { store.int("frame_rate_limit").wrappedValue = rates[$0] })

        let scale = store.float("render_scale").wrappedValue
        let tier = abs(scale - 1) < 0.0005 ? 0 : abs(scale - 0.9) < 0.0005 ? 1 : abs(scale - 0.75) < 0.0005 ? 2 : 3
        let quality = Binding<Int>(get: { customScale ? 3 : tier }, set: { i in
            if i == 3 { customScale = true; return }
            customScale = false
            store.float("render_scale").wrappedValue = [1, 0.9, 0.75][i]
        })

        Form {
            Section(T("显示器")) {
                SwitchRow(title: T("锁定时屏幕常亮"), detail: T("场景显示期间保持屏幕开启，耗电高于熄屏"),
                          on: store.bool("keep_display_on"))
                SwitchRow(title: T("电池供电时保持常亮"),
                          detail: keep ? T("关闭时仅在接通电源时保持") : T("「锁定时屏幕常亮」关闭时不起作用"),
                          on: store.bool("keep_display_on_battery")).disabled(!keep)
            }
            Section(T("密码界面")) {
                SwitchRow(title: T("密码界面绘制角色与特效"), detail: T("关闭后密码界面完全交还 macOS，不绘制角色与特效"),
                          on: store.bool("credential_scene"))
                SwitchRow(title: T("全屏特效"),
                          detail: scene ? T("关闭后不绘制飘浮粒子") : T("「密码界面绘制角色与特效」关闭时不起作用"),
                          on: store.bool("fullscreen_fx")).disabled(!scene)
            }
            Section {
                ChoiceRow(title: T("帧率上限"), detail: T("不限制时最高每秒 120 帧"),
                          options: rates.map { $0 == 0 ? T("不限制") : "\($0) FPS" }, selected: rate)
                SegmentRow(title: T("渲染质量"), detail: T("降低可减少绘制开销，画面随之变糊"),
                           options: [T("质量"), T("均衡"), T("性能"), T("自定义")], selected: quality)
                if customScale || tier == 3 {
                    RenderScaleRow(value: store.float("render_scale"))
                }
            }
            Section(T("实验性")) {
                SwitchRow(title: T("锁定时使用场景作为锁屏背景"),
                          detail: T("锁定期间将桌面壁纸临时替换为场景的房间图像，解锁后恢复；恢复会延迟约一帧，动态壁纸不替换"),
                          on: store.bool("experimental_lock_wallpaper"))
            }
        }
        .formStyle(.grouped)
        .navigationTitle(T("显示"))
    }
}

/// 渲染比例: 50% to 100%, written as render_scale when the drag ends.
struct RenderScaleRow: View {
    @Binding var value: Float
    @State private var live: Float? = nil
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                Slider(value: Binding(get: { live ?? min(1, max(0.5, value)) }, set: { live = ($0 * 100).rounded() / 100 }),
                       in: 0.5...1) { e in
                    if !e, let v = live { value = v; live = nil }
                }
                .frame(width: 180)
                Text("\(Int(((live ?? value) * 100).rounded()))%").monospacedDigit().foregroundStyle(.secondary)
                    .frame(width: 44, alignment: .trailing)
            }
        } label: { Label2(title: T("渲染比例"), detail: T("以屏幕分辨率的百分比绘制场景后放大至全屏")) }
    }
}
