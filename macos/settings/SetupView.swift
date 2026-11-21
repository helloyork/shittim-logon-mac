// shittim-logon / macos / settings / SetupView.swift
//
// installer/src/wizard.h on the Mac: the pages with a decision on them, shown the first
// time the distributed app is opened. The Mac has no installer to run -- the app is
// dragged into Applications from the disk image -- so "installing" is this sheet:
//
//   版权声明   who owns this and what may not be done with it, gated on a checkbox,
//              with the language picker (wizard.h: the page somebody has to be able
//              to read, so every page after it is in what is picked here)
//   声音       the output device and the two switches, both off at the factory --
//              written only when 开始安装 is pressed, as on Windows
//   环境检查   what can stop it: where the app is, and the assets
//   完成       the login item registered, verified rather than asserted
//
// The Windows 位置 page is the drag into Applications. Shown while setup has not been
// finished and the login item is not on; 退出 on the first page quits.

import SwiftUI

enum Setup {
    static let doneKey = "setupDone"
    static var needed: Bool {
        if Shot.active { return false }
        if UserDefaults.standard.bool(forKey: doneKey) { return false }
        let s = Overlay.state
        return s != .on && s != .notBundled
    }
}

struct SetupView: View {
    @EnvironmentObject var store: Store
    @Binding var shown: Bool

    enum Page: Int { case license, sound, check, done }
    @State var page: Page = .license
    @State private var accepted = false
    // 声音, held here until 开始安装 (wizard.h: nothing on this page takes effect before).
    @State private var device = 0
    @State private var voice = false
    @State private var music = false
    @State private var result: (ok: Bool, text: String)? = nil
    @State private var recheck = 0
    // 开始安装 can take twenty seconds (Overlay.enable's retries): said, not frozen.
    @State private var installing = false

    init(shown: Binding<Bool>, page: Page = .license) {
        _shown = shown
        _page = State(initialValue: page)
    }

    var body: some View {
        let _ = store.language
        VStack(alignment: .leading, spacing: 0) {
            HStack {
                Text(title).font(.title2.weight(.semibold))
                Spacer()
                if page == .license {
                    Picker("", selection: Binding(get: { store.language }, set: { store.setLanguage($0) })) {
                        ForEach(0..<4) { i in Text(["简体中文", "繁體中文", "English", "日本語"][i]).tag(i) }
                    }
                    .labelsHidden().fixedSize()
                }
            }
            .padding(.bottom, 16)

            Group {
                switch page {
                case .license: license
                case .sound: sound
                case .check: check
                case .done: done
                }
            }
            .frame(maxWidth: .infinity, maxHeight: .infinity, alignment: .topLeading)

            HStack {
                Spacer()
                if installing { ProgressView().controlSize(.small).padding(.trailing, 6) }
                if let s = secondary { Button(s.0, action: s.1).keyboardShortcut(.cancelAction).disabled(installing) }
                Button(primaryLabel, action: primary)
                    .keyboardShortcut(.defaultAction)
                    .disabled(!primaryEnabled)
            }
        }
        .padding(24)
        .frame(width: 560, height: 420)
        .id(store.language)
        .onAppear(perform: loadSound)
    }

    private var title: String {
        switch page {
        case .license: return T("版权声明")
        case .sound: return T("声音")
        case .check: return T("环境检查")
        case .done: return result?.ok == true ? T("安装完成") : T("安装未完成")
        }
    }

    // ---- 版权声明 (wizard.h PaintLicense, setup.h's lines)

    private var license: some View {
        VStack(alignment: .leading, spacing: 8) {
            Text("Shittim Logon © 2026 Nomen").fontWeight(.semibold)   // setup.h kCopyrightLine
            Text(T("著作权由作者保留；本软件可自由使用。")).foregroundStyle(.secondary)
            Divider().padding(.vertical, 6)
            Text(T("下列行为未获许可")).fontWeight(.semibold)
            ForEach(["再分发本软件", "出售本软件", "将本软件随收费的服务或整机一同分发",
                     "二次打包，或去除署名", "对本软件进行逆向"], id: \.self) { k in
                HStack(alignment: .firstTextBaseline, spacing: 8) {
                    Circle().frame(width: 4, height: 4).foregroundStyle(.secondary)
                    Text(T(k)).foregroundStyle(.secondary)
                }
            }
            Spacer().frame(height: 12)
            Toggle(T("我已阅读并接受以上条款"), isOn: $accepted).toggleStyle(.checkbox)
        }
    }

    // ---- 声音 (wizard.h LayoutSound / PaintSound)

    private var devices: [[String]] { store.s.outputDevices() }

    private var sound: some View {
        let labels = [T("自动")] + devices.map { $0[1] }
        return VStack(alignment: .leading, spacing: 10) {
            Text(T("输出设备")).fontWeight(.semibold)
            Picker("", selection: $device) {
                ForEach(labels.indices, id: \.self) { i in Text(labels[i]).tag(i) }
            }
            .labelsHidden().frame(maxWidth: 320)
            Text(device == 0 ? T("跟随系统默认设备") : T("优先使用该设备，未连接时跟随系统默认设备"))
                .font(.callout).foregroundStyle(.secondary)
            Text(T("播放")).fontWeight(.semibold).padding(.top, 8)
            Toggle(isOn: $voice) { Label2(title: T("角色语音"), detail: T("入场、待机与点击角色时的语音，附字幕")) }
                .toggleStyle(.checkbox)
            Toggle(isOn: $music) { Label2(title: T("背景音乐"), detail: T("锁定屏幕显示时淡入并循环播放")) }
                .toggleStyle(.checkbox)
            Text(T("两项出厂默认关闭；安装后可在设置程序的「声音」页调整音量、字幕与曲目"))
                .font(.callout).foregroundStyle(.secondary).padding(.top, 4)
        }
    }

    /// What the file says now -- a reinstall shows the earlier choices.
    private func loadSound() {
        voice = store.on("voice")
        music = store.on("bgm")
        let uid = store.value("voice_device")
        device = (devices.firstIndex { $0[0] == uid }).map { $0 + 1 } ?? 0
    }

    // ---- 环境检查 (selfcheck.h, the checks the Mac has)

    private struct Check: Identifiable { let id: String; let ok: Bool; let detail: String }

    private var checks: [Check] {
        _ = recheck
        let placement = Overlay.placementProblem
        let assets = Bundle.main.resourceURL?.appendingPathComponent("assets")
        let haveAssets = assets.map { FileManager.default.fileExists(atPath: $0.appendingPathComponent("bgm.pak").path) } ?? false
        return [
            Check(id: T("安装位置"), ok: placement == nil, detail: placement ?? Bundle.main.bundlePath),
            Check(id: T("随包素材"), ok: haveAssets, detail: haveAssets ? T("完整") : T("缺失，请重新下载安装包")),
        ]
    }

    private var check: some View {
        VStack(alignment: .leading, spacing: 12) {
            ForEach(checks) { c in
                HStack(alignment: .firstTextBaseline, spacing: 10) {
                    Image(systemName: c.ok ? "checkmark.circle.fill" : "xmark.octagon.fill")
                        .foregroundStyle(c.ok ? .green : .red)
                    Label2(title: c.id, detail: c.detail)
                }
            }
            Spacer().frame(height: 8)
            Note(text: T("安装后，Shittim Logon 将在登录时自动启动，并出现在「系统设置」的「登录项」中。"))
        }
    }

    // ---- 完成

    private var done: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text(result?.text ?? "")
            if result?.ok == true {
                Note(text: T("设置可随时在本程序中更改；停用或卸载位于「系统」页。"))
            }
        }
    }

    // ---- the buttons (wizard.h: 同意并继续 / 下一步 / 开始安装, 退出 / 上一步)

    private var primaryLabel: String {
        switch page {
        case .license: return T("同意并继续")
        case .sound: return T("下一步")
        case .check: return installing ? T("正在安装…") : T("开始安装")
        case .done: return T("完成")
        }
    }

    private var primaryEnabled: Bool {
        switch page {
        case .license: return accepted
        case .check: return !installing && checks.allSatisfy { $0.ok }
        default: return true
        }
    }

    // wizard.h's: 退出 on the first page, 取消 (setup comes back at the next start),
    // 重新检查 -- no 上一步, as there is none on Windows.
    private var secondary: (String, () -> Void)? {
        switch page {
        case .license: return (T("退出"), { NSApp.terminate(nil) })
        case .sound: return (T("取消"), { shown = false })
        case .check: return (T("重新检查"), { recheck += 1 })
        case .done: return result?.ok == true ? nil : (T("重新检查"), { page = .check })
        }
    }

    private func primary() {
        switch page {
        case .license: page = .sound
        case .sound: page = .check
        case .check: install()
        case .done:
            if result?.ok == true { UserDefaults.standard.set(true, forKey: Setup.doneKey) }
            shown = false
        }
    }

    /// 开始安装: the sound choices into the file, then the login item.
    private func install() {
        store.bool("voice").wrappedValue = voice
        store.bool("bgm").wrappedValue = music
        if device == 0 {
            store.string("voice_device").wrappedValue = "auto"
        } else {
            store.string("voice_device").wrappedValue = devices[device - 1][0]
            // Preferred, not pinned (wizard.h): the default device while it is not connected.
            store.bool("voice_device_fallback").wrappedValue = true
        }
        installing = true
        // After this frame, so the button already says 正在安装….
        DispatchQueue.main.async {
            result = Overlay.enable()
            installing = false
            page = .done
        }
    }
}
