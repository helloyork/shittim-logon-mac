// shittim-logon / macos / src / defaults_mac.h
//
// The Mac settings file's paragraphs: one per key, its description and its default.
// overlay/src/sceneconfig.h's DefaultText and LateSettings, said of the Mac. The
// wording is the Windows file's wherever the behaviour is the same (the project's
// rule for user-facing text applies to it), and says what the Mac does where that differs:
// paths, macOS's lock screen, the display going to sleep, Core Audio device ids.
//
// **One key per paragraph.** config_mac.h's Load appends the paragraph of every key the
// file does not set, and a paragraph carrying two keys would append a second line for
// the one the file does set -- the later line wins, so it would quietly reset that
// setting to its default.
//
// Defaults are written from the code (scenes::Settings, subtitle::Style), never typed a
// second time, as sceneconfig.h does: a default that exists twice eventually disagrees
// with itself.
//
// Not translated: the file's descriptions are Chinese only, as scenes.txt's are.

#pragma once

#include "../../render/src/scenes.h"

#include <string>
#include <utility>
#include <vector>

namespace macconfig {

inline std::string B(bool v) { return v ? "true" : "false"; }
inline std::string N(int v) { return std::to_string(v); }

// In the order a new file has them; an older file gets the ones it lacks in this order,
// in front of its first period.
inline const std::vector<std::pair<std::string, std::string>> &Blocks() {
    static const scenes::Settings D;
    static const subtitle::Style S;
    static const std::vector<std::pair<std::string, std::string>> b = {
        { "experimental_lock_wallpaper",
          "\n"
          "# 【实验性】锁定时使用场景作为锁屏背景\n"
          "# 开启后，屏幕锁定期间桌面壁纸临时替换为当前场景的房间图像，密码界面的背景与场景保持一致；\n"
          "# 解锁后恢复原壁纸。已知限制：解锁后原壁纸会延迟约一帧恢复；当前壁纸为 HEIC 格式\n"
          "# （可随外观或时间变化的动态壁纸）时不进行替换，以免恢复后丢失其外观设置。\n"
          "# 可选值：true / false。默认：false\n"
          "experimental_lock_wallpaper=false\n" },

        // ---- 时钟
        { "clock_language",
          "\n"
          "# ---- 时钟 -----------------------------------------------------------\n"
          "\n"
          "# 时钟与日期使用的语言。\n"
          "#\n"
          "#   auto   跟随 macOS 的区域格式（默认）。\n"
          "#   zh-CN  中文（简体）\n"
          "#   zh-TW  中文（繁體）\n"
          "#   ja-JP  日文\n"
          "#   ko-KR  韩文\n"
          "#   en-US  英文\n"
          "#\n"
          "# 选择具体语言后，日期与时间按该语言的标准格式显示。所选语言与当前区域格式的\n"
          "# 语言相同时（简体与繁體视为不同），仍按该区域格式显示。\n"
          "#\n"
          "# 本项仅影响本软件绘制的时钟与日期，不改变 macOS 的界面语言。\n"
          "# force_24_hour 开启时，时间固定按 24 小时格式显示，本项仅影响日期。\n"
          "clock_language=" + std::string(scenes::ClockLanguageText(D.clockLanguage)) + "\n" },
        { "force_24_hour",
          "\n"
          "# 强制 24 小时制。\n"
          "# 关闭后按上方时钟语言所对应的时间格式显示，此时可能显示上午 / 下午。\n"
          "force_24_hour=" + B(D.force24Hour) + "\n" },
        { "date_style",
          "\n"
          "# 日期格式。\n"
          "#\n"
          "#   windows  按 macOS 区域格式中的长日期格式显示（默认），是否带星期取决于\n"
          "#            该格式。键值沿用 Windows 版的写法。\n"
          "#   full     完整日期与星期，例如「2026年9月21日 星期一」「Monday, September 21, 2026」。\n"
          "#   compact  月日与星期，不含年份，例如「9月21日 周一」「Mon, Sep 21」。\n"
          "#\n"
          "# full 与 compact 适用于中文、日文、韩文与英文，其他语言始终按 windows 显示。\n"
          "date_style=" + std::string(scenes::DateStyleText(D.dateStyle)) + "\n" },
        { "clock_font",
          "\n"
          "# 时钟与日期所用的字体。此处填写字体来源。\n"
          "#\n"
          "#   factory  随软件附带的出厂字体 LINE Seed KR Bold（默认）。\n"
          "#   system   macOS 系统界面字体。\n"
          "#   custom   导入的自定义字体，位于本文件所在目录的 clock-font-custom.ttf。\n"
          "#\n"
          "# 也可填写已安装字体的字体族名，或以 .ttf .otf .ttc .otc 结尾的完整路径。\n"
          "# 所选字体无法使用时回退至 factory，并在日志中记录原因。\n"
          "clock_font=" + D.clockFont + "\n" },
        { "clock_over_character",
          "\n"
          "# 时钟绘制在角色上层，不被遮挡。\n"
          "# 默认关闭：时钟位于场景内部，角色站在其前方为设计意图，被遮挡时文字仍可辨认。\n"
          "clock_over_character=" + B(D.clockOverCharacter) + "\n" },
        { "clock_offset_x",
          "\n"
          "# 时钟水平偏移，单位为屏幕宽度的比例。\n"
          "# -0.05 左移 5%，0.05 右移 5%，0 为屏幕正中。限制在 -0.5 至 0.5 之间。\n"
          "# 时间与日期两行一起移动，字号与行距不变。\n"
          "clock_offset_x=0\n" },
        { "clock_offset_y",
          "\n"
          "# 时钟垂直偏移，单位为屏幕高度的比例。-0.05 上移 5%，0.05 下移 5%。\n"
          "clock_offset_y=0\n" },
        { "date_shift_x",
          "\n"
          "# 日期相对于时间的水平偏移，单位为屏幕宽度的比例。正值向右，限制在 -0.25 至 0.25 之间。\n"
          "date_shift_x=0\n" },
        { "date_shift_y",
          "\n"
          "# 日期相对于时间的垂直偏移，单位为屏幕高度的比例，正值向下。\n"
          "date_shift_y=0\n" },

        // ---- 角色
        { "character_antialias",
          "\n"
          "# ---- 角色 -----------------------------------------------------------\n"
          "\n"
          "# 角色边缘的抗锯齿。\n"
          "#\n"
          "#   auto   按屏幕分辨率决定（默认）。4K 及更高的屏幕关闭，低于此开启。\n"
          "#   on     始终开启：角色以两倍分辨率绘制后平均缩回。\n"
          "#   off    始终关闭。\n"
          "character_antialias=auto\n" },
        { "character_offset_x",
          "\n"
          "# 角色的水平位置，单位为屏幕宽度的比例，正值向右。限制在 -0.25 至 0.25 之间，\n"
          "# 只移动位置，不改变大小。向右移动过多会与密码框重叠。\n"
          "character_offset_x=0\n" },
        { "character_offset_y",
          "\n"
          "# 角色的垂直位置，单位为屏幕高度的比例，正值向下。限制在 -0.25 至 0.25 之间。\n"
          "character_offset_y=0\n" },
        { "headpat",
          "\n"
          "# 摸头。\n"
          "# 角色出现在前台后，在其头部按住鼠标左键并左右移动即可摸头，松开结束。\n"
          "# 开启时，角色身体范围内的点击由本软件接收；关闭后点击一律交给密码界面。\n"
          "headpat=" + B(D.headpat) + "\n" },
        { "eye_tracking",
          "\n"
          "# 密码界面上角色的视线跟随指针。\n"
          "# 默认关闭。开启后，角色出现在密码界面前方时，头部与视线朝向指针所在的位置。\n"
          "# 只读取指针位置，不接收点击。\n"
          "eye_tracking=" + B(D.eyeTracking) + "\n" },
        { "voice_lipsync",
          "\n"
          "# 口型同步。\n"
          "# 开启后，前景角色播放自身语音时，嘴部随语音连续开合，口型按当前表情选取。\n"
          "# 需同时开启角色语音（voice），关闭时此项不起作用。\n"
          "voice_lipsync=" + B(D.voiceLipsync) + "\n" },

        // ---- 语音互动
        { "voice",
          "\n"
          "# ---- 语音互动 -------------------------------------------------------\n"
          "\n"
          "# 角色语音。\n"
          "#\n"
          "# 开启后，场景出现时播放入场语音，此后按下方间隔播放待机语音；触发唤醒时\n"
          "# 播放应答语音；角色进入前台后播放问候语音，此后单击角色即播放应答语音。\n"
          "#\n"
          "# 出厂默认关闭。锁定屏幕与设备所在环境共用，因此不采用默认开启。\n"
          "voice=" + B(D.voice) + "\n" },
        { "voice_volume",
          "\n"
          "# 语音音量，取值 0 至 100。仅影响语音。\n"
          "voice_volume=" + N(D.voiceVolume) + "\n" },
        { "voice_subtitle",
          "\n"
          "# 显示字幕。字幕绘制于角色下方，随角色位置移动。\n"
          "voice_subtitle=" + B(D.voiceSubtitle) + "\n" },
        { "voice_subtitle_scene",
          "\n"
          "# 背景场景（房间中的角色）的语音是否显示字幕：入场、待机与唤醒时的语音。\n"
          "# voice_subtitle 关闭时不起作用。\n"
          "voice_subtitle_scene=" + B(D.voiceSubtitleScene) + "\n" },
        { "voice_subtitle_character",
          "\n"
          "# 角色前景（走到前台的角色）的语音是否显示字幕：问候与单击应答。\n"
          "# voice_subtitle 关闭时不起作用。\n"
          "voice_subtitle_character=" + B(D.voiceSubtitleCharacter) + "\n" },
        { "voice_subtitle_language",
          "\n"
          "# 字幕使用的语言。只更换字幕文字，听到的语言由下方 voice_pack 决定。\n"
          "#\n"
          "#   zh      简体中文（默认）\n"
          "#   zh-TW   繁体中文\n"
          "#   ja      日文，即游戏原版台词\n"
          "#   ko      韩文\n"
          "#   en      英文\n"
          "#\n"
          "# 日文、韩文与英文台词普遍较长，下方 voice_subtitle_scale 的上限分别按 " +
          subtitle::ToText(subtitle::kScaleMaxJapanese) + "、" + subtitle::ToText(subtitle::kScaleMaxKorean) +
          " 与\n# " + subtitle::ToText(subtitle::kScaleMaxEnglish) + " 计算，以免字幕遮挡角色面部。\n"
          "voice_subtitle_language=" + std::string(subtitle::LanguageKey(D.voiceSubtitleLanguage)) + "\n" },
        { "voice_idle_interval",
          "\n"
          "# 待机语音间隔，单位为秒，取值 15 至 10000。\n"
          "# 计时仅在场景实际绘制时推进，显示器睡眠期间暂停，因此长时间锁定不会持续发声。\n"
          "voice_idle_interval=" + N(D.voiceIdleInterval) + "\n" },
        { "voice_pack",
          "\n"
          "# 角色语音使用的语音包。此处填写语音包名称，不是文件路径。\n"
          "#\n"
          "#   factory  随软件附带的出厂语音（默认），即素材目录中的 voice。\n"
          "#   其他名称 素材目录中的 voice-<名称>，例如 ko 对应 voice-ko。\n"
          "#\n"
          "# 名称对应的语音包不存在时回退至 factory。语音包只改变听到的声音，不改变台词、\n"
          "# 播放时机与字幕。\n"
          "voice_pack=" + D.voicePack + "\n" },
        { "voice_device",
          "\n"
          "# 语音与背景音乐从哪个输出设备播放。\n"
          "#\n"
          "#   auto   跟随当前默认输出设备（默认）。\n"
          "#   其他   设备的唯一标识（Core Audio UID），建议在设置程序的「声音」页选择。\n"
          "#\n"
          "# 指定了具体设备时，该设备未连接期间不会改从其他设备发声，只保持静音，字幕照常。\n"
          "voice_device=auto\n" },
        { "voice_device_fallback",
          "\n"
          "# 上面指定的设备未连接时，是否改用默认输出设备。\n"
          "#\n"
          "#   false  不改用，保持静音，字幕照常（默认）。\n"
          "#   true   改用默认输出设备；指定的设备重新连接后，下次锁定时切回。\n"
          "#\n"
          "# voice_device 为 auto 时此项不起作用。\n"
          "voice_device_fallback=" + B(D.voiceDeviceFallback) + "\n" },

        // ---- 字幕外观
        { "voice_subtitle_scale",
          "\n"
          "# ---- 字幕外观 -------------------------------------------------------\n"
          "\n"
          "# 字号缩放，取值 0.75 至 1.25。上限以字幕不遮挡角色面部为准。\n"
          "voice_subtitle_scale=" + subtitle::ToText(S.scale) + "\n" },
        { "voice_subtitle_font",
          "\n"
          "# 字幕字体。此处填写字体来源。\n"
          "#\n"
          "#   factory  随软件附带的出厂字体（默认）。字幕语言为英文时改用时钟的出厂\n"
          "#            字体 LINE Seed KR。\n"
          "#   system   macOS 系统界面字体。\n"
          "#   custom   导入的自定义字体，位于本文件所在目录的 subtitle-font-custom.ttf。\n"
          "#\n"
          "# 也可填写已安装字体的字体族名或字体文件的完整路径。无法使用时回退至系统界面\n"
          "# 字体，并在日志中记录原因。字体中缺少的字符由系统回退字体补足。\n"
          "voice_subtitle_font=" + S.font + "\n" },
        { "voice_subtitle_background",
          "\n"
          "# 字幕底色，#RRGGBBAA。默认纯黑、55% 不透明。\n"
          "voice_subtitle_background=" + subtitle::ToHex(S.background) + "\n" },
        { "voice_subtitle_border",
          "\n"
          "# 边框颜色，#RRGGBBAA。默认全透明，即不绘制边框。\n"
          "voice_subtitle_border=" + subtitle::ToHex(S.border) + "\n" },
        { "voice_subtitle_text",
          "\n"
          "# 文字颜色，#RRGGBBAA。默认接近纯白、96% 不透明。\n"
          "voice_subtitle_text=" + subtitle::ToHex(S.text) + "\n" },
        { "voice_subtitle_border_width",
          "\n"
          "# 边框粗细，取值 0 至 4，单位为点。边框颜色全透明时此项不起作用。\n"
          "voice_subtitle_border_width=" + N((int)S.borderWidth) + "\n" },
        { "voice_subtitle_corner",
          "\n"
          "# 圆角半径，取值 0 至 16，单位为点。0 为直角。\n"
          "voice_subtitle_corner=" + N((int)S.corner) + "\n" },

        // ---- 背景音乐
        { "bgm",
          "\n"
          "# ---- 背景音乐 -------------------------------------------------------\n"
          "\n"
          "# 背景音乐。\n"
          "# 开启后，屏幕锁定时淡入并循环播放，解锁时淡出。显示器进入睡眠时同样淡出，\n"
          "# 唤醒后再淡入，因此长时间锁定不会持续播放。\n"
          "# 可选值：true / false。默认：false\n"
          "bgm=" + B(D.bgm) + "\n" },
        { "bgm_volume",
          "\n"
          "# 背景音乐音量，取值 0 至 100。与角色语音的音量相互独立。默认：" + N(scenes::kBgmVolumeDefault) + "\n"
          "bgm_volume=" + N(scenes::kBgmVolumeDefault) + "\n" },
        { "bgm_duck",
          "\n"
          "# 角色语音播放期间，背景音乐降低到的音量比例，取值 0 至 100。\n"
          "# 此项为 bgm_volume 的百分比：0 表示语音播放期间完全静音，100 表示不降低。\n"
          "# 降低与恢复均为渐变，约 0.25 秒。默认：" + N(scenes::kBgmDuckDefault) + "\n"
          "bgm_duck=" + N(scenes::kBgmDuckDefault) + "\n" },
        { "bgm_track",
          "\n"
          "# 使用哪一首曲目。此处填写曲目名称，不是文件路径。\n"
          "#\n"
          "#   factory  随软件附带的出厂曲目，即素材目录中的 bgm.pak（默认）。\n"
          "#   custom   导入的自定义曲目，位于本文件所在目录的 bgm-custom.pak。\n"
          "#   其他名称 素材目录中的 bgm-<名称>.pak。\n"
          "#\n"
          "# 可填写多首，以英文逗号分隔：每次锁定时随机选择一首，播放结束后随机切换至\n"
          "# 另一首，不连续重复同一首。仅填写一首时循环播放。名称对应的文件不存在时\n"
          "# 回退至 factory。各时段可另以 music= 指定曲目，见下方时段说明。\n"
          "bgm_track=" + D.bgmTrack + "\n" },

        // ---- 显示
        { "credential_scene",
          "\n"
          "# ---- 显示 -----------------------------------------------------------\n"
          "\n"
          "# 在密码界面绘制角色与特效。\n"
          "#\n"
          "# 开启时（默认），唤醒后角色站在密码界面的前台。关闭后，锁屏的房间场景与唤醒\n"
          "# 反应照常显示，但房间淡出后角色不再进场，整块屏幕均为 macOS 的密码界面，\n"
          "# 此时 fullscreen_fx 不起作用。\n"
          "credential_scene=" + B(D.credentialScene) + "\n" },
        { "fullscreen_fx",
          "\n"
          "# 全屏特效：飘浮的三角形粒子覆盖整个屏幕。关闭后不绘制粒子。\n"
          "fullscreen_fx=" + B(D.fullscreenFx) + "\n" },
        { "keep_display_on",
          "\n"
          "# 锁定屏幕期间保持显示器开启。\n"
          "#\n"
          "#   false  按系统设置让显示器进入睡眠（默认）。\n"
          "#   true   场景显示期间阻止显示器因闲置而睡眠。\n"
          "#\n"
          "# 代价：显示器保持开启期间场景持续绘制，耗电高于睡眠状态。合上上盖与手动\n"
          "# 使设备睡眠不受本项影响。\n"
          "keep_display_on=" + B(D.keepDisplayOn) + "\n" },
        { "keep_display_on_battery",
          "\n"
          "# 使用电池供电时是否同样保持显示器开启。\n"
          "#\n"
          "#   false  仅在接通电源时保持（默认）。\n"
          "#   true   电池供电时同样保持。\n"
          "#\n"
          "# 上一项关闭时，本项不起作用。按锁定时的供电状态判断。\n"
          "keep_display_on_battery=" + B(D.keepDisplayOnBattery) + "\n" },
        { "frame_rate_limit",
          "\n"
          "# 帧率上限。\n"
          "#\n"
          "#   0        不限制（默认），最高每秒 120 帧。\n"
          "#   30 / 60  按指定帧率绘制。低于 24 的取值按 24 处理，高于 120 按 120 处理。\n"
          "frame_rate_limit=" + N(D.frameRateLimit) + "\n" },
        { "render_scale",
          "\n"
          "# 场景的渲染比例，以屏幕分辨率的比例表示。\n"
          "#\n"
          "#   1      按屏幕分辨率绘制（默认）。\n"
          "#   " + scenes::RenderScaleText(scenes::kRenderScaleBalanced) + "    按较低分辨率绘制后放大至全屏，绘制开销随之下降。\n"
          "#   " + scenes::RenderScaleText(scenes::kRenderScalePerformance) + "   同上，更低。\n"
          "#\n"
          "# 可填写 0.25 至 1 之间的任意小数。低于 1 时画面随之变糊，时钟数字同样变糊。\n"
          "render_scale=1\n" },
    };
    return b;
}

} // namespace macconfig
