// shittim-logon / macos / settings / Store.swift
//
// The SwiftUI side of bridge.h: bindings that read through SLSettings and write
// through it (each write saves the file, as the Windows app's Touch does), and the
// string lookup every view uses.

import SwiftUI
import UniformTypeIdentifiers

/// A row of ui/strings.h (or strings_mac.h), in the interface language.
func T(_ hans: String) -> String { SLSettings.tr(hans) }
func T(_ hans: String, _ arg: String) -> String { SLSettings.tr(hans, with: arg) }

final class Store: ObservableObject {
    static let shared = Store()
    let s = SLSettings.shared()
    /// Moves on every change, so every view that reads through the store redraws.
    @Published private(set) var revision = 0
    @Published var language = SLSettings.language

    private func changed() { revision = s.revision }

    func reload() { s.reload(); changed() }

    func bool(_ key: String) -> Binding<Bool> {
        Binding(get: { _ = self.revision; return self.s.bool(for: key) },
                set: { self.s.setBool($0, for: key); self.changed() })
    }
    func float(_ key: String) -> Binding<Float> {
        Binding(get: { _ = self.revision; return self.s.float(for: key) },
                set: { self.s.setFloat($0, for: key); self.changed() })
    }
    func int(_ key: String) -> Binding<Int> {
        Binding(get: { _ = self.revision; return self.s.int(for: key) },
                set: { self.s.setInt($0, for: key); self.changed() })
    }
    func string(_ key: String) -> Binding<String> {
        Binding(get: { _ = self.revision; return self.s.string(for: key) },
                set: { self.s.setString($0, for: key); self.changed() })
    }
    func value(_ key: String) -> String { _ = revision; return s.string(for: key) }
    func on(_ key: String) -> Bool { _ = revision; return s.bool(for: key) }

    // ---- periods, read fresh each time

    var periodOrder: [Int] { _ = revision; return s.periodOrder().map { $0.intValue } }
    var periodCount: Int { _ = revision; return s.periodCount() }
    func edit(_ f: (SLSettings) -> Void) { f(s); changed() }

    func setLanguage(_ l: Int) {
        SLSettings.language = l
        language = l
        changed()
    }

    // ---- the Windows app's Note: one line at the foot of the window for four seconds,
    // for an outcome -- an import, a removal, an export, a box that went back.

    @Published private(set) var note: String? = nil
    private var noteGen = 0

    func say(_ text: String) {
        noteGen += 1
        let gen = noteGen
        note = text
        changed()
        DispatchQueue.main.asyncAfter(deadline: .now() + 4) { if self.noteGen == gen { self.note = nil } }
    }
}

/// The Open dialog, started in Downloads as the Windows app's is.
func pickFile(_ extensions: [String]) -> String? {
    let panel = NSOpenPanel()
    panel.allowsMultipleSelection = false
    panel.canChooseDirectories = false
    panel.allowedContentTypes = extensions.compactMap { UTType(filenameExtension: $0) }
    panel.directoryURL = FileManager.default.urls(for: .downloadsDirectory, in: .userDomainMask).first
    return panel.runModal() == .OK ? panel.url?.path : nil
}
