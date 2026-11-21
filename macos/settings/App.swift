// shittim-logon / macos / settings / App.swift
//
// The window: a sidebar of the Windows app's pages (configwindow.h Section) less the
// ones that are only Windows' -- 「高级」 is its sign-in options and Windows Hello -- and
// the page on the right.
//
//   ShittimSettings                      the app
//   ShittimSettings --shot <page> <png> [--lang 0..3] [--dark] [--width px]
//                                        one page drawn offscreen into a PNG, the Mac
//                                        counterpart of installer\shots.ps1 -What config;
//                                        `setup:0..3` the first-run pages (SetupView).
//                                        Run it with SHITTIM_SETTINGS_DIR pointing at a
//                                        copy: it must never read or write the person's own file.

import SwiftUI

enum Page: String, CaseIterable, Identifiable {
    case scenes, sound, subtitle, display, system, notifications, about
    var id: String { rawValue }

    var title: String {
        switch self {
        case .scenes: return T("场景")
        case .sound: return T("声音")
        case .subtitle: return T("字幕外观")
        case .display: return T("显示")
        case .system: return T("系统")
        case .notifications: return T("通知")
        case .about: return T("关于")
        }
    }
    var symbol: String {
        switch self {
        case .scenes: return "photo.on.rectangle"
        case .sound: return "speaker.wave.2"
        case .subtitle: return "captions.bubble"
        case .display: return "display"
        case .system: return "gearshape"
        case .notifications: return "bell"
        case .about: return "info.circle"
        }
    }
}

struct PageView: View {
    let page: Page
    @Binding var path: [Int]

    var body: some View {
        NavigationStack(path: $path) {
            Group {
                switch page {
                case .scenes: ScenesPage { i in path.append(i) }
                case .sound: SoundPage()
                case .subtitle: SubtitlePage()
                case .display: DisplayPage()
                case .system: SystemPage()
                case .notifications: NotificationsPage()
                case .about: AboutPage()
                }
            }
            .navigationDestination(for: Int.self) { i in
                PeriodPage(index: i, close: { if !path.isEmpty { path.removeLast() } },
                           open: { j in if j != i { path = [j] } })
            }
        }
    }
}

/// A page whose rows are not in this build yet.
struct Unfinished: View {
    let title: String
    var body: some View {
        Form { Note(text: T("此页面的设置将在后续版本中提供。")) }
            .formStyle(.grouped)
            .navigationTitle(title)
    }
}

struct RootView: View {
    @EnvironmentObject var store: Store
    @State var page: Page? = .scenes
    @State private var path: [Int] = []
    @State private var setup = Setup.needed

    var body: some View {
        let _ = store.language
        NavigationSplitView {
            List(Page.allCases, selection: $page) { p in
                Label(p.title, systemImage: p.symbol).tag(p)
            }
            .navigationSplitViewColumnWidth(min: 180, ideal: 200)
        } detail: {
            // Rebuilt on a new language too: every T() is read when a view is made.
            PageView(page: page ?? .scenes, path: $path)
                .id("\(page?.rawValue ?? "")-\(store.language)")
        }
        .onChange(of: page) { _ in path = [] }
        .overlay(alignment: .bottom) {
            if let e = store.s.saveError {
                Text(e).padding(8).background(.red.opacity(0.85), in: RoundedRectangle(cornerRadius: 6))
                    .foregroundStyle(.white).padding()
            } else if let n = store.note {
                Text(n).padding(.horizontal, 12).padding(.vertical, 8)
                    .background(.regularMaterial, in: RoundedRectangle(cornerRadius: 8))
                    .shadow(radius: 4).padding()
                    .transition(.opacity)
            }
        }
        .animation(.easeInOut(duration: 0.2), value: store.note)
        .sheet(isPresented: $setup) {
            SetupView(shown: $setup).environmentObject(store).interactiveDismissDisabled()
        }
    }
}

struct SettingsApp: App {
    @StateObject private var store = Store.shared

    var body: some Scene {
        WindowGroup(T("Shittim Logon 设置")) {
            RootView()
                .environmentObject(store)
                .frame(minWidth: 760, minHeight: 520)
                .onReceive(NotificationCenter.default.publisher(for: NSApplication.didBecomeActiveNotification)) { _ in
                    store.reload()   // the file may have been edited by hand meanwhile
                }
        }
        .windowResizability(.contentMinSize)
    }
}

@main
enum Main {
    static func main() {
        let args = CommandLine.arguments
        if let i = args.firstIndex(of: "--shot"), i + 2 < args.count {
            exit(Shot.run(page: args[i + 1], out: args[i + 2], args: args))
        }
        // A test of the write path: one setting through settingsfile.h, as a control
        // would write it. Like --shot, run it against SHITTIM_SETTINGS_DIR.
        if let i = args.firstIndex(of: "--set"), i + 2 < args.count {
            let s = SLSettings.shared()
            s.setString(args[i + 2], for: args[i + 1])
            print(s.saveError ?? "set \(args[i + 1])=\(args[i + 2])")
            exit(s.saveError == nil ? 0 : 1)
        }
        // The imports and the export without the window, the way config.exe takes
        // --import-voice-pack / --import-font: for tests, against SHITTIM_SETTINGS_DIR.
        let s = SLSettings.shared()
        let calls: [String: (String) -> String] = [
            "--import-track": { s.importTrack($0) },
            "--remove-track": { s.removeTrack($0) },
            "--import-voice-pack": { s.importVoicePack($0) },
            "--import-font": { s.importFont($0, clock: false) },
            "--import-clock-font": { s.importFont($0, clock: true) },
        ]
        for (flag, call) in calls {
            if let i = args.firstIndex(of: flag), i + 1 < args.count {
                print(call(args[i + 1]))
                print(s.voicePackStatus())
                exit(0)
            }
        }
        // The login item without the window. SMAppService answers for the GUI session, so
        // from SSH this goes through `open -a ... --args --login-item enable --out f`.
        if let i = args.firstIndex(of: "--login-item"), i + 1 < args.count {
            let said: String
            switch args[i + 1] {
            case "enable": said = Overlay.enable().text
            case "disable": said = Overlay.disable().text
            // The 卸载 button without its two alerts. Run it with SHITTIM_SETTINGS_DIR on
            // a copy in tests: it deletes the imported files beside settings.txt.
            case "uninstall": said = Overlay.uninstall(Store.shared).text
            // What every start of the app does first (Overlay.refreshAfterUpdate).
            case "refresh": Overlay.refreshAfterUpdate(); said = "\(Overlay.state) running=\(Overlay.running)"
            default: said = "\(Overlay.state) running=\(Overlay.running) placement=\(Overlay.placementProblem ?? "ok")"
            }
            print(said)
            if let o = args.firstIndex(of: "--out"), o + 1 < args.count {
                try? said.write(toFile: args[o + 1], atomically: true, encoding: .utf8)
            }
            exit(0)
        }
        if args.contains("--export-logs") {
            var path: NSString? = nil
            print(s.exportLogs(&path), path ?? "")
            exit(path == nil ? 1 : 0)
        }
        Overlay.refreshAfterUpdate()
        SettingsApp.main()
    }
}

/// One page offscreen into a PNG: an NSHostingView in a window that is never shown,
/// drawn with cacheDisplay (AppKit controls included, unlike ImageRenderer).
enum Shot {
    /// Set while a shot is drawn: the first-run sheet stays away.
    static var active = false

    static func run(page: String, out: String, args: [String]) -> Int32 {
        active = true
        let app = NSApplication.shared
        app.setActivationPolicy(.prohibited)
        if let i = args.firstIndex(of: "--lang"), i + 1 < args.count, let l = Int(args[i + 1]) { SLSettings.useLanguage(l) }
        let dark = args.contains("--dark")
        var width: CGFloat = 900
        if let i = args.firstIndex(of: "--width"), i + 1 < args.count, let w = Double(args[i + 1]) { width = CGFloat(w) }

        let store = Store.shared
        let content: AnyView
        if page == "window" {
            content = AnyView(RootView().environmentObject(store).frame(width: width, height: 640))
        } else if page.hasPrefix("period:"), let n = Int(page.dropFirst(7)) {
            content = AnyView(NavigationStack { PeriodPage(index: n, close: {}, open: { _ in }) }.environmentObject(store)
                .frame(width: width, height: 1500))
        } else if page.hasPrefix("setup:"), let n = Int(page.dropFirst(6)), let sp = SetupView.Page(rawValue: n) {
            content = AnyView(SetupView(shown: .constant(true), page: sp).environmentObject(store))
        } else if let p = Page(rawValue: page) {
            content = AnyView(PageView(page: p, path: .constant([])).environmentObject(store)
                .frame(width: width, height: 1700))
        } else {
            FileHandle.standardError.write("unknown page \(page)\n".data(using: .utf8)!)
            return 2
        }
        let host = NSHostingView(rootView: content)
        host.frame.size = host.fittingSize
        let window = NSWindow(contentRect: NSRect(origin: .zero, size: host.frame.size), styleMask: [.borderless],
                              backing: .buffered, defer: false)
        window.appearance = NSAppearance(named: dark ? .darkAqua : .aqua)
        window.contentView = host
        host.layoutSubtreeIfNeeded()
        RunLoop.main.run(until: Date().addingTimeInterval(0.6))   // let SwiftUI lay out and load images
        host.layoutSubtreeIfNeeded()
        guard let rep = host.bitmapImageRepForCachingDisplay(in: host.bounds) else { return 1 }
        host.cacheDisplay(in: host.bounds, to: rep)
        guard let png = rep.representation(using: .png, properties: [:]) else { return 1 }
        do { try png.write(to: URL(fileURLWithPath: out)) } catch { return 1 }
        print("shot \(page) -> \(out) (\(Int(host.bounds.width))x\(Int(host.bounds.height)))")
        return 0
    }
}
