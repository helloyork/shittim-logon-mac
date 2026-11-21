# shittim-logon for macOS

The macOS port of [shittim-logon](https://github.com/helloyork/shittim-logon): a
live-rendered Spine scene on the macOS lock screen.

**This repository is the Mac port's own code. It is not the product and it will not
build on its own.** The Mac build compiles the Windows sources it was ported from, and
of those only the adapter is public. There are no game assets in it and no Spine
runtime.

---

## 中文说明

这个仓库是 Mac 版**自己写的那部分代码**，不是可运行的产物。

- Mac 版的构建要用到 Windows 版的源码，而 Windows 版公开的只有适配器
  （[helloyork/shittim-logon](https://github.com/helloyork/shittim-logon)），
  所以这个仓库**单独编译不起来**。
- 仓库里**没有任何游戏资产**，也没有 Spine 运行时。

---

## What it does

macOS draws the lock screen. This does not replace it: it draws *over* it, in a
borderless window, and steps aside so that the password field is always the system's.

- **Above the lock screen.** The window is moved into a SkyLight space (a private
  framework, loaded at run time) at absolute level 400; the lock screen is at 300. The
  window is shown only while the session is locked.
- **Never key.** The window cannot become key, so every keystroke goes to the system's
  password field. While the room covers the screen the window takes clicks (a click or a
  key wakes the scene); once the room has faded it takes them only over the character's
  body, for the head pat, and passes the rest through.
- **The same sequence as Windows.** Locked: the room, and the arrival flash bringing its
  people in. Key or click: the reaction, the exit flash, the foreground character's
  entrance. Then the room fades, and only the character and the drift are drawn over the
  password screen.
- **A login item.** The distributed app is the settings app, with the overlay inside it
  as a helper that `SMAppService` registers. launchd starts it in every session at login
  and restarts it if it fails. Nothing needs administrator rights: the overlay runs as
  the signed-in person.

## How it is built

**The Windows sources are used as they are.** The build copies `render/src`,
`overlay/src`, `overlay/shaders`, `config/src` and `ui/strings.h` into a stage
directory with the same layout and replaces only the files bound to Windows:

| | |
|---|---|
| `macos/replace/render/src/image.h` | PNG load/save through ImageIO instead of WIC, straight alpha kept |
| `macos/replace/render/src/pathio.h`, `spine_io.h` | paths are UTF-8 already |
| `macos/replace/overlay/src/gpu.h` | **the D3D11 layer on Metal**: same types and member names, the HLSL shaders transcribed to MSL, the blend modes, the silhouette readback |
| `macos/replace/overlay/src/sceneconfig.h` | where the settings file is, for the settings app's file editor |
| `macos/compat/windows.h`, `d2d1_1.h`, `wrl/client.h` | the few Win32 / D2D / ComPtr names the shared headers touch |

So the Windows overlay's scene layer -- the wake sequence, the flashes, the drift, the
head pat, the gaze, the mouth, the clock's place in the room -- compiles and runs
unchanged, and a fix on Windows reaches the Mac on the next build.

Mac-only code is in `macos/src/`:

| | |
|---|---|
| `host.h` | the spine extension, the log (MSVC `%s`/`%hs` translated, rolling at 1.5 MiB in two generations), applying a scene as the Windows overlay does |
| `lockscreen.mm` | the app: the window in the level-400 space, lock and unlock, the frame loop, input, voice, music, clock, subtitle, scene rotation, quiet while the display sleeps |
| `config_mac.h`, `defaults_mac.h` | `~/Library/Application Support/ShittimLogon/settings.txt` in the Windows `scenes.txt` grammar, parsed by the shared parser; the scene rotation ported decision for decision |
| `bgm_mac.h` | the background music: the Windows fades and duck, with an AVAudioPlayer on its own serial queue |
| `voice_mac.h`, `audiodev_mac.h` | the voice run with the Windows half swapped: AVAudioPlayer on a serial queue, clips decoded from `.ogg` with stb_vorbis, Core Audio devices; director, lip sync, expressions and subtitles are the shared code |
| `ui_mac.h`, `clocktext_mac.h` | the scene clock (drawn inside the room, cut by the characters) and the subtitle panel, with CoreText |
| `wallpaper_mac.h` | the experimental lock wallpaper (below) |
| `still.mm` | offscreen: plays the wake and writes PNGs -- the check against the Windows frames |
| `voicetest.mm`, `clocktest.mm`, `logtest.mm` | the voice run, the clock text and the rolling log, without a screen |

The settings app is in `macos/settings/`: SwiftUI over an Objective-C++ bridge
(`bridge.h`) that compiles the Windows settings app's line-by-line file editor
unchanged and looks strings up in the Windows four-language table, then in
`strings_mac.h` for the Mac's own rows (`check_strings.py` checks every lookup has a
row).

`macos/build.sh` builds on the Mac (Apple silicon, macOS 13 or later):

```
sh ~/shittim-mac/src/macos/build.sh lockscreen      # also: settings, still, voicetest, clocktest, list_animations
sh ~/shittim-mac/src/macos/build.sh dist            # lockscreen, settings, the app, the disk image
```

`macos/sync.sh` ships a full source tree and, optionally, a directory of assets to the
Mac over SSH. `macos/res/` holds the scripts that draw the app icon and the disk image
background; their output is never checked in. The app is signed ad hoc unless
`SIGN_ID` names a Developer ID.

## Tried and not kept as a default: the room behind the password field

On Windows the lock-screen picture is set to the scene's own still, so when the room
fades out the credential screen stands in the same room. macOS has no lock-screen
picture of its own since Sonoma -- the lock screen shows the desktop picture. So on
lock the desktop picture was set (`NSWorkspace setDesktopImageURL`) to an empty-room
still, and put back on unlock.

- It works: the password field stands in the room after the fade.
- The restore lands **one frame late** -- the desktop shows the room for a frame on
  unlock. The unlock is only known afterwards, so this cannot be fixed from here.
- **The restore is not a restore.** The public API writes a plain image choice. An
  appearance-aware or dynamic `.heic` chosen in System Settings, put back through the
  API, loses that and shows its light variant, which reads as the system theme turning
  light.
- A crash between lock and unlock would leave the room as the desktop picture.

It stays as an **experimental switch, off by default** (`experimental_lock_wallpaper`,
`macos/src/wallpaper_mac.h`). With it on, a `.heic` desktop picture is never swapped,
and the original's path is written to a file before the swap so the next start puts it
back if a crash left the room behind.

## Rules learned the hard way

- **Nothing that can block runs on the frame thread.** AVAudioEngine blocked `play`
  for 5-6 s on the lock screen and then threw; the frame froze on the arrival flash.
  Audio is on a serial queue and the main thread never touches a player object.
- The frame loop's own log line is the last thing in a frame. An exception mid-frame
  shows up as the fps lines stopping, not as an error.
- An ad hoc signature is the code's own hash, and launchd checks a login item against
  the requirement taken when it was registered. A new build has to be registered again.
- Gatekeeper's "Open Anyway" releases only the app that was opened. The helper inside
  it is a second app that launchd starts, and a quarantined one is refused at exec, so
  the app takes the quarantine flag off its own helper before registering it.

## What is not in here

- **Any game asset.** No skeletons, atlases, textures, fonts, voice clips or music.
- **The Windows sources** the Mac build compiles, beyond what the public adapter
  repository already has.
- **The Spine runtime.** Third-party, separately licensed.
- **Builds.** No app, no disk image.
- **Screenshots.**

## Licence

MIT, see `LICENSE`. That covers this project's own code and nothing else; see
`NOTICE.md`.
