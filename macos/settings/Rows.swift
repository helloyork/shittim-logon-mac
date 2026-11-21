// shittim-logon / macos / settings / Rows.swift
//
// The Windows app's cards (config/src/pagekit.h: SettingCard, SwitchCard, OffsetSlider,
// ChoiceBox, Segmented) as rows of a grouped Form -- the shape of macOS's own System
// Settings: a title, a description under it, the control on the right.

import SwiftUI

/// Title and, when there is one, the description under it.
struct Label2: View {
    let title: String
    var detail: String = ""
    var body: some View {
        VStack(alignment: .leading, spacing: 2) {
            Text(title)
            if !detail.isEmpty {
                Text(detail).font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            }
        }
    }
}

/// SwitchCard.
struct SwitchRow: View {
    let title: String
    var detail: String = ""
    @Binding var on: Bool
    var body: some View {
        Toggle(isOn: $on) { Label2(title: title, detail: detail) }.toggleStyle(.switch)
    }
}

/// ChoiceBox: a pop-up menu of labels, chosen by index.
struct ChoiceRow: View {
    let title: String
    var detail: String = ""
    let options: [String]
    @Binding var selected: Int
    var body: some View {
        Picker(selection: $selected) {
            ForEach(options.indices, id: \.self) { i in Text(options[i]).tag(i) }
        } label: { Label2(title: title, detail: detail) }
    }
}

/// Segmented.
struct SegmentRow: View {
    let title: String
    var detail: String = ""
    let options: [String]
    @Binding var selected: Int
    var body: some View {
        LabeledContent {
            Picker("", selection: $selected) {
                ForEach(options.indices, id: \.self) { i in Text(options[i]).tag(i) }
            }
            .pickerStyle(.segmented).labelsHidden().fixedSize()
        } label: { Label2(title: title, detail: detail) }
    }
}

/// OffsetSlider: ±range in steps of 0.01, its value written as "%+.2f". The file is
/// written when the drag ends (the Windows slider's onCommit), not on every step.
struct OffsetRow: View {
    let title: String
    var detail: String = ""
    let range: ClosedRange<Float>
    @Binding var value: Float
    @State private var live: Float? = nil
    var body: some View {
        LabeledContent {
            HStack(spacing: 10) {
                // Rounded to 0.01 here rather than with `step:`, which draws a tick per step.
                Slider(value: Binding(get: { live ?? value }, set: { live = ($0 * 100).rounded() / 100 }),
                       in: range) { editing in
                    if !editing, let v = live { value = v; live = nil }
                }
                .frame(width: 180)
                Text(String(format: "%+.2f", Double(live ?? value)))
                    .monospacedDigit().foregroundStyle(.secondary).frame(width: 44, alignment: .trailing)
            }
        } label: { Label2(title: title, detail: detail) }
    }
}

/// A line of explanation under a group (NoteLine).
struct Note: View {
    let text: String
    var body: some View {
        Text(text).font(.callout).foregroundStyle(.secondary).fixedSize(horizontal: false, vertical: true)
            .multilineTextAlignment(.leading)
            .frame(maxWidth: .infinity, alignment: .leading)
    }
}
