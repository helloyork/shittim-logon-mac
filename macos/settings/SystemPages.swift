// shittim-logon / macos / settings / SystemPages.swift
//
// configwindow.h BuildSystem, BuildNotifications and BuildAbout, with what the Mac has.
// The master switch and uninstall are Distribution.swift's; the 「高级」 page is Windows'
// sign-in options and Hello.

import AppKit
import ServiceManagement
import SwiftUI

struct SystemPage: View {
    @EnvironmentObject var store: Store
    /// Moves when the switch has done something, so the rows read the machine again.
    @State private var tick = 0
    /// Enabling can take twenty seconds (Overlay.enable's retries).
    @State private var busy = false

    private var logPath: String { NSHomeDirectory() + "/Library/Logs/ShittimLogon/lockscreen.log" }

    var body: some View {
        let languages = ["简体中文", "繁體中文", "English", "日本語"]   // each in itself, as i18n::NativeName
        Form {
            master
            Section(T("语言")) {
                ChoiceRow(title: T("界面语言"), detail: T("设置程序使用的语言；不影响锁定屏幕"),
                          options: languages,
                          selected: Binding(get: { store.language }, set: { store.setLanguage($0) }))
            }
            Section(T("疑难解答")) {
                LabeledContent {
                    Button(T("打开")) {
                        let fm = FileManager.default
                        let at = fm.fileExists(atPath: logPath) ? logPath : (logPath as NSString).deletingLastPathComponent
                        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: at)])
                    }
                } label: { Label2(title: T("日志"), detail: logPath) }
                LabeledContent {
                    Button(T("打开")) {
                        NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: store.s.filePath)])
                    }
                } label: { Label2(title: T("配置文件"), detail: store.s.filePath) }
                // A second row, because these are two different people: 打开 is for somebody
                // who wants to read a log; this is for somebody who has been asked for one.
                LabeledContent {
                    Button(T("导出")) {
                        var path: NSString? = nil
                        store.say(store.s.exportLogs(&path))
                        if let p = path { NSWorkspace.shared.activateFileViewerSelecting([URL(fileURLWithPath: p as String)]) }
                    }
                } label: { Label2(title: T("导出日志"), detail: T("打包日志与当前设置，用于反馈问题")) }
            }
            // Uninstall is the end of the page in every settings app that has one.
            if Overlay.state != .notBundled {
                Section(T("卸载")) {
                    LabeledContent {
                        Button(T("卸载")) { uninstall() }
                    } label: { Label2(title: T("卸载 Shittim Logon"), detail: T("移除本软件并还原其更改的系统设置")) }
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle(T("系统"))
    }

    /// BuildSystem's master switch, read off the machine (the login item), not the file.
    @ViewBuilder private var master: some View {
        let _ = tick
        let state = Overlay.state
        Section {
            if state == .notBundled {
                // A development build: no login item inside it to switch.
                Label2(title: "Shittim Logon", detail: Overlay.running ? T("正在运行") : T("未在运行"))
            } else {
                Toggle(isOn: Binding(get: { state == .on }, set: { on in
                    busy = true
                    DispatchQueue.main.async {
                        let r = on ? Overlay.enable() : Overlay.disable()
                        busy = false
                        store.say(r.text)
                        tick += 1
                    }
                })) {
                    Label2(title: T("启用 Shittim Logon"),
                           detail: state == .on ? T("关闭后锁定屏幕恢复系统默认，设置与文件保留")
                                 : state == .needsApproval ? T("需要在「系统设置」的「登录项」中允许 Shittim Logon")
                                 : T("当前已停用，锁定屏幕为系统默认"))
                }
                .toggleStyle(.switch)
                .disabled(busy)
                .overlay(alignment: .trailing) {
                    if busy { ProgressView().controlSize(.small).padding(.trailing, 52) }
                }
                if state == .needsApproval {
                    LabeledContent {
                        Button(T("打开")) { SMAppService.openSystemSettingsLoginItems() }
                    } label: { Label2(title: T("登录项")) }
                }
            }
        }
    }

    /// The 卸载 button: asked once, then uninstall.cpp's steps, then the app quits.
    private func uninstall() {
        let ask = NSAlert()
        ask.messageText = T("卸载 Shittim Logon？")
        ask.informativeText = T("将停用 Shittim Logon，删除日志与导入的文件，并将本程序移到废纸篓；设置文件保留。")
        ask.addButton(withTitle: T("卸载"))
        ask.addButton(withTitle: T("取消"))
        ask.alertStyle = .warning
        guard ask.runModal() == .alertFirstButtonReturn else { return }
        let r = Overlay.uninstall(store)
        let done = NSAlert()
        done.messageText = r.ok ? T("卸载完成。") : T("卸载未完成")
        done.informativeText = r.text
        done.runModal()
        NSApp.terminate(nil)
    }
}

/// BuildNotifications: only what needs attention (configwindow.h Rows, the ones the Mac
/// can check). Never a system notification -- the Windows rule, kept here.
struct NotificationsPage: View {
    @EnvironmentObject var store: Store

    var body: some View {
        let _ = store.revision
        var state = 0
        let output = store.s.outputStatus(&state)
        Form {
            Section {
                if state == 2 {
                    LabeledContent {
                        Image(systemName: "exclamationmark.triangle.fill").foregroundStyle(.yellow)
                    } label: { Label2(title: T("输出设备"), detail: output) }
                } else {
                    Note(text: T("没有需要处理的项目"))
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle(T("通知"))
    }
}

struct AboutPage: View {
    var body: some View {
        Form {
            Section {
                Label2(title: "Shittim Logon", detail: "macOS · " + Overlay.version)   // i18n: no
            }
            // Windows' line before this one says the software is open source; the Mac
            // app says only the rights line here.
            Section(T("版权")) {
                Note(text: T("著作权由作者保留；本软件可自由使用。"))
            }
            // The files that ship beside the program (setup.h kDocFiles), in the app's Resources.
            if let res = Bundle.main.resourceURL, FileManager.default.fileExists(atPath: res.appendingPathComponent("LICENSE.txt").path) {
                Section(T("文件与来源")) {
                    ForEach([(T("使用条款"), T("LICENSE.txt · 本页文字以该文件为准"), "LICENSE.txt"),
                             (T("第三方声明"), T("Spine 运行时要求每位使用者自备 Spine Editor 授权"), "NOTICE.txt")], id: \.2) { d in
                        LabeledContent {
                            Button(T("打开")) { NSWorkspace.shared.open(res.appendingPathComponent(d.2)) }
                        } label: { Label2(title: d.0, detail: d.1) }
                    }
                }
            }
            Section(T("致谢")) {
                Note(text: T("感谢以下成员对本项目的支持："))
                ForEach(["WangZixu_旭", "拉菲", "魚_U0", "Origami Fighters", T("全体测试成员")], id: \.self) { who in
                    Text(who)
                }
            }
        }
        .formStyle(.grouped)
        .navigationTitle(T("关于"))
    }
}
