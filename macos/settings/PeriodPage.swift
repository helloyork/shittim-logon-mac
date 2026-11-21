// shittim-logon / macos / settings / PeriodPage.swift
//
// configwindow.h BuildPeriod: the bar with this period in colour, its name, start and
// end, its scenes, its music (BuildPeriodMusic), and removing it.

import SwiftUI

struct PeriodPage: View {
    @EnvironmentObject var store: Store
    let index: Int
    let close: () -> Void
    let open: (Int) -> Void
    @State private var name = ""
    @State private var held: HeldBoundary? = nil

    var body: some View {
        let _ = store.revision
        if index >= store.periodCount {
            Color.clear.onAppear(perform: close)
        } else {
            Form {
                Section { PeriodBar(focus: index, held: $held, open: open) }
                time
                scenes
                music
                if store.periodCount >= 2 { remove }
            }
            .formStyle(.grouped)
            .navigationTitle(store.s.periodLabel(index))
            .onAppear { name = store.s.periodName(index) }
        }
    }

    // ---- 时间

    private var time: some View {
        Section(T("时间")) {
            LabeledContent {
                TextField("", text: $name, prompt: Text(SLSettings.clockText(store.s.periodStart(index))))
                    .textFieldStyle(.roundedBorder)
                    .multilineTextAlignment(.leading)
                    .frame(width: 168)
                    .onSubmit { store.edit { $0.setPeriodName(name, at: index) } }
            } label: {
                Label2(title: T("名称"), detail: T("显示于时段条与时段列表；留空时显示开始时间"))
            }
            if store.periodCount >= 2 {
                boundary(end: false)
                boundary(end: true)
            } else {
                Note(text: T("此时段为全天。添加时段后，可分别设置各时段的开始与结束时间。"))
            }
        }
    }

    /// AddBoundaryChoice: a period's start is the previous one's end, and its end the
    /// next one's start, so both are moved through the neighbour's start.
    private func boundary(end: Bool) -> some View {
        let which = end ? store.s.periodNext(index) : index
        let times = store.s.startChoices(which).map { $0.intValue }
        let start = store.s.periodStart(index)
        let labels = times.map { t -> String in
            let s = SLSettings.clockText(t)
            return end && t <= start ? T("次日 %s", s) : s
        }
        let detail = end ? T("同时为「%s」的开始时间", store.s.periodLabel(store.s.periodNext(index)))
                         : T("同时为「%s」的结束时间", store.s.periodLabel(store.s.periodPrev(index)))
        let selected = Binding<Int>(
            get: { times.firstIndex(of: store.s.periodStart(which)) ?? 0 },
            set: { k in if k < times.count { store.edit { $0.setPeriodStart(times[k], at: which) } } })
        return ChoiceRow(title: end ? T("结束时间") : T("开始时间"), detail: detail, options: labels, selected: selected)
    }

    // ---- 场景

    private var scenes: some View {
        Section {
            LazyVGrid(columns: [GridItem(.adaptive(minimum: 150), spacing: 12)], spacing: 12) {
                ForEach(store.s.sceneNames(), id: \.self) { scene in
                    SceneTile(scene: scene, index: index)
                }
            }
            .padding(.vertical, 4)
        } header: {
            Text(T("场景"))
        } footer: {
            Note(text: T("此时段未启用任何场景时，从其他时段的场景中选择。"))
        }
    }

    // ---- 背景音乐: 使用默认曲目, and when that is off, every track with a box to tick.

    private var music: some View {
        let useDefault = store.s.periodUsesDefaultMusic(index)
        return Section {
            SwitchRow(title: T("使用默认曲目"), detail: store.s.defaultMusicLabel(),
                      on: Binding(get: { useDefault },
                                  set: { v in store.edit { $0.setPeriodUsesDefaultMusic(v, at: index) } }))
            if !useDefault {
                ForEach(store.s.tracks(forPeriod: index), id: \.self) { t in
                    Toggle(isOn: Binding(get: { store.s.track(t[0], inPeriod: index) },
                                         set: { on in
                                             if !store.s.setTrack(t[0], inPeriod: index, on: on) {
                                                 store.say(T("至少选择一首曲目；如需使用默认曲目，请打开「使用默认曲目」"))
                                             }
                                             store.edit { _ in }
                                         })) {
                        Label2(title: t[1], detail: t[2])
                    }
                    .toggleStyle(.checkbox)
                }
            }
        } header: {
            Text(T("背景音乐"))
        } footer: {
            if !useDefault { Note(text: T("选择多首曲目时随机播放，播放结束后切换至另一首，不连续重复同一首。")) }
        }
    }

    // ---- 移除

    private var remove: some View {
        Section {
            HStack {
                Button(T("移除此时段")) {
                    store.edit { $0.removePeriod(index) }
                    close()
                }
                Spacer()
            }
        } footer: {
            Note(text: T("移除后，此时段的时间并入「%s」。", store.s.periodLabel(store.s.periodPrev(index))))
        }
    }
}

/// ThumbTile: the scene's thumbnail, its name, and whether this period offers it.
struct SceneTile: View {
    @EnvironmentObject var store: Store
    let scene: String
    let index: Int

    var body: some View {
        let on = store.s.scene(scene, inPeriod: index)
        Button {
            store.edit { $0.setScene(scene, inPeriod: index, active: !on) }
        } label: {
            VStack(alignment: .leading, spacing: 6) {
                ZStack(alignment: .topTrailing) {
                    thumbnail
                        .aspectRatio(16.0 / 9.0, contentMode: .fit)
                        .clipShape(RoundedRectangle(cornerRadius: 6))
                        .overlay(RoundedRectangle(cornerRadius: 6)
                            .strokeBorder(on ? Color.accentColor : Color.secondary.opacity(0.3), lineWidth: on ? 2 : 1))
                    Image(systemName: on ? "checkmark.circle.fill" : "circle")
                        .font(.title3)
                        .foregroundStyle(on ? Color.accentColor : Color.white.opacity(0.9))
                        .shadow(radius: 1)
                        .padding(6)
                }
                Text(scene).font(.callout)
            }
        }
        .buttonStyle(.plain)
    }

    @ViewBuilder private var thumbnail: some View {
        if let path = store.s.thumbnail(for: scene), let image = NSImage(contentsOfFile: path) {
            Image(nsImage: image).resizable()
        } else {
            Rectangle().fill(Color.secondary.opacity(0.15))
        }
    }
}
