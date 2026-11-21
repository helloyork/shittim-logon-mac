// shittim-logon / macos / settings / ScenesPage.swift
//
// configwindow.h BuildScenes, BuildCharacterGroup and BuildClockGroup: the periods,
// then the character, then the clock, with the Windows rows and wording: the period bar
// (PeriodBar.swift) over the list, each row with its colour on the bar.

import SwiftUI

struct ScenesPage: View {
    @EnvironmentObject var store: Store
    let open: (Int) -> Void
    @State private var held: HeldBoundary? = nil
    @Environment(\.colorScheme) private var scheme

    var body: some View {
        Form {
            periods
            character
            clock
        }
        .formStyle(.grouped)
        .navigationTitle(T("场景"))
    }

    // ---- the periods

    private var periods: some View {
        Section {
            Note(text: T("锁定屏幕按当前时段，从该时段已启用的场景中随机选择一个。"))
            PeriodBar(held: $held, open: open)
            Note(text: store.periodCount >= 2 ? T("拖动分界线可调整时段的开始时间，点击时段可修改其场景与背景音乐。")
                                              : T("点击时段可修改其场景与背景音乐。"))
            // While a boundary is held, the rows it changes say so; the file waits for the release.
            ForEach(store.periodOrder, id: \.self) { i in
                NavigationLink(value: i) {
                    HStack(alignment: .top, spacing: 10) {
                        RoundedRectangle(cornerRadius: 2).fill(periodColour(i, dark: scheme == .dark).fill)
                            .frame(width: 4, height: 32)
                        Label2(title: store.s.periodLabel(i),
                               detail: held.map { store.s.periodSummary(i, withStart: $0.minute, of: $0.index) }
                                   ?? store.s.periodSummary(i))
                    }
                }
            }
            HStack {
                Button(T("添加时段")) { store.edit { $0.addPeriod() } }.disabled(!store.s.canAddPeriod())
                Spacer()
            }
        } footer: {
            Note(text: store.s.canAddPeriod() ? T("新时段由最长的时段对半分出，沿用其场景与背景音乐。")
                                                : T("各时段均已不足一小时，无法再分出新时段。"))
        }
    }

    // ---- the character

    private var aaIndex: Binding<Int> {
        Binding(get: { ["auto", "on", "off"].firstIndex(of: store.value("character_antialias")) ?? 0 },
                set: { store.string("character_antialias").wrappedValue = ["auto", "on", "off"][$0] })
    }

    private var character: some View {
        Section(T("角色")) {
            SegmentRow(title: T("边缘抗锯齿"), detail: T("自动：4K 以下启用"),
                       options: [T("自动"), T("开"), T("关")], selected: aaIndex)
            OffsetRow(title: T("水平位置"), detail: T("正值向右"), range: -0.25...0.25,
                      value: store.float("character_offset_x"))
            OffsetRow(title: T("垂直位置"), detail: T("正值向下"), range: -0.25...0.25,
                      value: store.float("character_offset_y"))
            SwitchRow(title: T("口型同步"),
                      detail: store.on("voice") ? T("前景角色说话时，嘴部随语音开合") : T("「角色语音」关闭时不起作用"),
                      on: store.bool("voice_lipsync"))
            SwitchRow(title: T("视线跟随指针"), detail: T("开启后，密码界面上角色的视线随指针移动"),
                      on: store.bool("eye_tracking"))
        }
    }

    // ---- the clock

    private static let languages = ["auto", "zh-CN", "zh-TW", "ja-JP", "ko-KR", "en-US"]

    private var languageIndex: Binding<Int> {
        Binding(get: { Self.languages.firstIndex(of: store.value("clock_language")) ?? 0 },
                set: { store.string("clock_language").wrappedValue = Self.languages[$0] })
    }
    private var dateIndex: Binding<Int> {
        Binding(get: { ["windows", "full", "compact"].firstIndex(of: store.value("date_style")) ?? 0 },
                set: { store.string("date_style").wrappedValue = ["windows", "full", "compact"][$0] })
    }

    private var clock: some View {
        Section(T("时钟")) {
            ChoiceRow(title: T("语言"), detail: store.s.clockLanguageStatus(),
                      options: [T("自动"), T("中文（简体）"), T("中文（繁體）"), T("日文"), T("韩文"), T("英文")],
                      selected: languageIndex)
            SwitchRow(title: T("24 小时制"),
                      detail: store.value("clock_language") == "auto" ? T("关闭后跟随 macOS 的时间格式")
                                                                       : T("关闭后按所选语言的时间格式显示"),
                      on: store.bool("force_24_hour"))
            ChoiceRow(title: T("日期格式"), detail: store.s.dateExample(),
                      options: [T("macOS 长日期"), T("完整日期与星期"), T("月日与星期")], selected: dateIndex)
            FontRows(clock: true)
            SwitchRow(title: T("时钟绘制于角色前方"), detail: T("默认由角色遮挡"), on: store.bool("clock_over_character"))
            OffsetRow(title: T("水平位置"), detail: T("正值向右，0 为屏幕正中"), range: -0.5...0.5,
                      value: store.float("clock_offset_x"))
            OffsetRow(title: T("垂直位置"), detail: T("负值向上"), range: -0.5...0.5, value: store.float("clock_offset_y"))
            OffsetRow(title: T("日期水平位置"), detail: T("相对于时间，正值向右"), range: -0.25...0.25,
                      value: store.float("date_shift_x"))
            OffsetRow(title: T("日期垂直位置"), detail: T("相对于时间，负值向上"), range: -0.25...0.25,
                      value: store.float("date_shift_y"))
        }
    }
}
