// shittim-logon / macos / settings / Distribution.swift
//
// The Mac counterpart of installer/src/onoff.h and uninstall.cpp.
//
// The overlay is a login item: the app carries Contents/Library/LaunchAgents/
// local.shittim.lockscreen.plist (build.sh `app`), whose BundleProgram is the helper
// at Contents/Library/LoginItems/ShittimLock.app, and SMAppService registers it. That
// is the whole of "installed": launchd starts it in every Aqua session at login, keeps
// it alive if it fails (KeepAlive SuccessfulExit=false), and System Settings → General →
// Login Items lists it under this app's name, where it can be turned off too. Nothing
// needs administrator rights -- the overlay runs as the person, unlike Windows' SYSTEM.
//
// The app must be registered from where it will stay: not from the disk image and not
// from App Translocation (an app opened from Downloads runs from a random read-only
// copy), or the login item points at a path that vanishes.

import AppKit
import ServiceManagement

enum Overlay {
    static let bundleId = "local.shittim.lockscreen"
    static let plist = bundleId + ".plist"
    static var service: SMAppService { SMAppService.agent(plistName: plist) }

    enum State { case on, off, needsApproval, notBundled }

    /// The login item's state. `notBundled` is a development build (build.sh `settings`)
    /// with no helper or plist inside it.
    static var state: State {
        let plistURL = Bundle.main.bundleURL.appendingPathComponent("Contents/Library/LaunchAgents/" + plist)
        if !FileManager.default.fileExists(atPath: plistURL.path) { return .notBundled }
        switch service.status {
        case .enabled: return rescued ? .off : .on
        case .requiresApproval: return .needsApproval
        default: return .off
        }
    }

    /// lockscreen.mm's RescuePath: Control+Option+F11 on the lock screen took the overlay
    /// away and keeps it away (rescue.h's `disabled` on Windows). The login item is still
    /// registered, but the overlay leaves at every start, so the switch shows off; turning
    /// it on deletes the marker.
    static var settingsDir: String {
        if let e = ProcessInfo.processInfo.environment["SHITTIM_SETTINGS_DIR"], !e.isEmpty { return e }
        return NSHomeDirectory() + "/Library/Application Support/ShittimLogon"
    }
    static var rescueMarker: String { settingsDir + "/disabled" }
    static var rescued: Bool { FileManager.default.fileExists(atPath: rescueMarker) }

    static var running: Bool {
        !NSRunningApplication.runningApplications(withBundleIdentifier: bundleId).isEmpty
    }

    /// Why the app cannot register from where it is, or nil.
    static var placementProblem: String? {
        let url = Bundle.main.bundleURL
        let readOnly = (try? url.resourceValues(forKeys: [.volumeIsReadOnlyKey]))?.volumeIsReadOnly ?? false
        if readOnly || url.path.contains("/AppTranslocation/") {
            return T("请先将 Shittim Logon 拖入「应用程序」文件夹，再从该文件夹打开")
        }
        return nil
    }

    /// The build the login item was last registered from (UserDefaults).
    static let registeredBuildKey = "registeredBuild"
    static var build: String { Bundle.main.infoDictionary?["CFBundleVersion"] as? String ?? "" }

    /// onoff.h --enable. The sentence for the note; `ok` false when it did not happen.
    ///
    /// Measured 2026-11-20 (a first install: the overlay never appeared).
    /// launchd starts a login item against the code requirement taken when it was
    /// registered (`has LWCR`), and an ad hoc signature is the code's own hash, so a new
    /// build does not meet the old one: `spawn failed`, EX_CONFIG or OS_REASON_CODESIGNING,
    /// `needs LWCR update` in `launchctl print`. What renews it:
    ///   - registering over a live registration: no;
    ///   - register() straight after unregister() returns: no -- the removal is not done;
    ///   - unregister, wait for it to finish, register: yes, except that the first such
    ///     registration of a new build after an earlier one also failed (builds 7 and 8),
    ///     and the next one started it.
    /// Another registration 5 s after such a failure failed too; one 30 s later started it,
    /// so launchd renews the requirement on its own some seconds after the failed spawn.
    /// So: unregister and wait, register, and if no overlay is running five seconds later,
    /// wait and do it again -- after 10 s, then after 15 s.
    static func enable() -> (ok: Bool, text: String) {
        if let p = placementProblem { return (false, p) }
        try? FileManager.default.removeItem(atPath: rescueMarker)
        releaseHelper()
        let pauses: [Double] = [0, 10, 15]   // before each attempt after the first
        for (attempt, pause) in pauses.enumerated() {
            if pause > 0 { RunLoop.current.run(until: Date().addingTimeInterval(pause)) }
            if service.status == .enabled || service.status == .requiresApproval { unregisterAndWait() }
            do {
                try service.register()
            } catch {
                if service.status != .requiresApproval {
                    NSLog("Shittim Logon: register failed: \(error)")
                    return (false, T("启用失败：%s", error.localizedDescription))
                }
            }
            if service.status == .requiresApproval {
                SMAppService.openSystemSettingsLoginItems()
                return (false, T("需要在「系统设置」的「登录项」中允许 Shittim Logon"))
            }
            if waitForOverlay(seconds: 5) { break }
            NSLog("Shittim Logon: registered, but launchd did not start the overlay (attempt \(attempt + 1))")
        }
        UserDefaults.standard.set(build, forKey: registeredBuildKey)
        if !running { return (false, T("已启用，但锁屏程序未能启动；请停用后重新启用，或导出日志反馈")) }
        return (true, T("已启用，按 Control + Command + Q 锁定屏幕即可查看"))
    }

    /// unregister(), and back only once it has finished (or ten seconds have passed).
    static func unregisterAndWait() {
        var done = false
        service.unregister { _ in DispatchQueue.main.async { done = true } }
        let until = Date().addingTimeInterval(10)
        while Date() < until && (!done || service.status == .enabled) {
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }
        stopRunning()
    }

    static func waitForOverlay(seconds: Double) -> Bool {
        let until = Date().addingTimeInterval(seconds)
        while Date() < until && !running { RunLoop.current.run(until: Date().addingTimeInterval(0.2)) }
        return running
    }

    /// The app is not signed with a Developer ID (a deliberate choice), so a
    /// downloaded copy carries the quarantine flag, and Gatekeeper's 「仍要打开」 releases
    /// only the app that was opened -- this one. The overlay inside it is a second app
    /// that launchd starts, where nobody can be asked, and a quarantined one is refused
    /// at exec: OS_REASON_CODESIGNING, the overlay never appears (measured 2026-11-20,
    /// a never-run build; the same build without the flag started). So the app the
    /// person has just let run takes the flag off its own helper before registering it.
    static func releaseHelper() {
        let helper = Bundle.main.bundleURL.appendingPathComponent("Contents/Library/LoginItems/ShittimLock.app")
        var paths = [helper.path]
        if let e = FileManager.default.enumerator(atPath: helper.path) {
            for case let rel as String in e { paths.append(helper.path + "/" + rel) }
        }
        for p in paths where getxattr(p, "com.apple.quarantine", nil, 0, 0, XATTR_NOFOLLOW) >= 0 {
            if removexattr(p, "com.apple.quarantine", XATTR_NOFOLLOW) != 0 {
                NSLog("Shittim Logon: could not clear quarantine on \(p): errno \(errno)")
            }
        }
    }

    /// At every start of the app: a login item that is on but was registered from another
    /// build is registered again, for enable()'s reason -- after an update the overlay
    /// would otherwise not start at the next login. So an update is: replace the app,
    /// open it once.
    static func refreshAfterUpdate() {
        guard state == .on, UserDefaults.standard.string(forKey: registeredBuildKey) != build else { return }
        let r = enable()
        NSLog("Shittim Logon: registered again for build \(build): \(r.text)")
    }

    /// onoff.h --disable: the login item goes, and the overlay running now is stopped
    /// (launchd's SIGTERM; one started by hand is asked to quit as well).
    static func disable() -> (ok: Bool, text: String) {
        var failed: Error? = nil
        if service.status != .notRegistered && service.status != .notFound {
            do { try service.unregister() } catch { failed = error }
        }
        stopRunning()
        if let e = failed {
            NSLog("Shittim Logon: unregister failed: \(e)")
            return (false, T("停用失败：%s", e.localizedDescription))
        }
        return (true, T("已停用，锁定屏幕恢复系统默认"))
    }

    /// Asks every running overlay to quit -- its applicationWillTerminate puts the desktop
    /// picture back -- and forces the ones still there after three seconds.
    static func stopRunning() {
        let apps = NSRunningApplication.runningApplications(withBundleIdentifier: bundleId)
        apps.forEach { $0.terminate() }
        let until = Date().addingTimeInterval(3)
        while Date() < until && apps.contains(where: { !$0.isTerminated }) {
            RunLoop.current.run(until: Date().addingTimeInterval(0.1))
        }
        apps.filter { !$0.isTerminated }.forEach { $0.forceTerminate() }
    }

    /// uninstall.cpp without --purge: the login item, the logs, the state files and the
    /// imported copies go; settings.txt stays (somebody reinstalling keeps their choices,
    /// and `bgm_track=` / `voice_pack=custom` in it ask for the file again in one click).
    /// Then the app itself goes to the Trash. Returns the sentences to show.
    static func uninstall(_ store: Store) -> (ok: Bool, text: String) {
        _ = disable()
        store.s.restoreWallpaperIfLeft()
        let fm = FileManager.default
        try? fm.removeItem(atPath: NSHomeDirectory() + "/Library/Logs/ShittimLogon")
        let dir = (store.s.filePath as NSString).deletingLastPathComponent
        let state = ["next-scene", "shown-scene", "shown-track", "lockscreen.lock", "wallpaper-backup.txt", "disabled",
                     "clock-font-custom.ttf", "subtitle-font-custom.ttf",
                     "voice-custom", "voice-custom.partial", "voice-custom.old"]
        for name in state { try? fm.removeItem(atPath: (dir as NSString).appendingPathComponent(name)) }
        // The imported tracks, bgm-<id>.pak (bridge_files.h ImportTrackFile), and the room
        // stills the experimental lock wallpaper bakes, lock-room-<scene>-<w>x<h>.png
        // (lockscreen.mm roomStill).
        for name in (try? fm.contentsOfDirectory(atPath: dir)) ?? []
            where (name.hasPrefix("bgm-") && name.hasSuffix(".pak")) || (name.hasPrefix("lock-room-") && name.hasSuffix(".png")) {
            try? fm.removeItem(atPath: (dir as NSString).appendingPathComponent(name))
        }
        var lines = [T("已保留 %s", store.s.filePath)]
        do {
            try fm.trashItem(at: Bundle.main.bundleURL, resultingItemURL: nil)
        } catch {
            lines.append(T("无法将本程序移到废纸篓，请手动删除：%s", Bundle.main.bundlePath))
            return (false, lines.joined(separator: "\n"))
        }
        return (true, lines.joined(separator: "\n"))
    }

    /// CFBundleShortVersionString (build CFBundleVersion).
    static var version: String {
        let i = Bundle.main.infoDictionary ?? [:]
        let v = i["CFBundleShortVersionString"] as? String ?? "0"
        let b = i["CFBundleVersion"] as? String ?? "0"
        return "\(v) (\(b))"   // i18n: no
    }
}
