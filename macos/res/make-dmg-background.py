"""The disk image window's background: what to drag where, and how to open an
unsigned app the first time -- said in the window, before anybody is stuck.

The look is a plain tablet-style interface, drawn from lines and type
only, no game art: a pale ground with a fine grid and a few faint triangle
shards (the room's particles as plain geometry), a header, plates under the icons, an
arrow, and the steps on a card. Kept plain on purpose: no all-capitals
lettering, no corner brackets, badges, tags or two-colour rules.

Simplified Chinese with English under it: a disk image's background cannot follow
the system language, and the audience is Chinese-speaking.

Written at sync time like make-icon.py (the fonts are on the Windows host): two PNGs,
1x and 2x, that build.sh joins into one TIFF for dmgbuild. Noto Sans SC (SIL OFL,
Windows ships it).

    python macos/res/make-dmg-background.py <out-dir>

The image is W x H points and the icon view shows it from its top left; the two icons
are centred at APP and APPS -- res/dmg-settings.py uses the same numbers.
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

W, H = 660, 430
APP, APPS = (170, 176), (490, 176)
# Finder's window is this image plus its title bar (about 30 pt, res/dmg-settings.py adds
# it); a path bar or status bar the person has switched on takes 27 pt each off the
# bottom of the view. So the steps card ends BAR_SAFE above the image's foot: the path
# bar never reaches it, and with both bars only the card's bottom padding is cut.
BAR_SAFE = 47
ICON = 104

INK = (52, 56, 70)
SUB = (124, 130, 146)
PURPLE = (214, 126, 224)
SKY = (74, 170, 236)
GROUND_TOP, GROUND_BOTTOM = (250, 251, 254), (238, 242, 249)
LINE = (210, 215, 228)
FONT = r'C:\Windows\Fonts\NotoSansSC-VF.ttf'


def font(size, weight=400):
    f = ImageFont.truetype(FONT, size)
    try:
        f.set_variation_by_axes([weight])
    except Exception:
        pass
    return f


def draw(s):
    im = Image.new('RGB', (W * s, H * s))
    g = ImageDraw.Draw(im)
    for y in range(H * s):
        t = y / (H * s - 1)
        g.line([(0, y), (W * s, y)],
               fill=tuple(int(GROUND_TOP[i] + (GROUND_BOTTOM[i] - GROUND_TOP[i]) * t) for i in range(3)))
    over = Image.new('RGBA', im.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(over)
    P = lambda *v: [x * s for x in v]

    # the ground: fine grid and a few shards
    for x in range(0, W, 22):
        d.line(P(x, 52, x, H), fill=(120, 140, 180, 14), width=1)
    for y in range(52, H, 22):
        d.line(P(0, y, W, y), fill=(120, 140, 180, 14), width=1)
    for (x, y, k, rot, c, a) in ((548, 150, 14, 0, PURPLE, 60), (590, 118, 9, 1, SKY, 70), (520, 100, 7, 2, PURPLE, 45),
                                 (88, 262, 10, 1, SKY, 50), (58, 236, 6, 0, PURPLE, 45), (612, 262, 8, 2, SKY, 45)):
        pts = [(x, y - k), (x + k * 0.9, y + k * 0.6), (x - k * 0.9, y + k * 0.6)]
        if rot == 1:
            pts = [(x - k, y - k * 0.5), (x + k, y - k * 0.2), (x - k * 0.2, y + k)]
        if rot == 2:
            pts = [(x, y + k), (x - k * 0.9, y - k * 0.6), (x + k * 0.9, y - k * 0.6)]
        d.polygon([(px * s, py * s) for px, py in pts], fill=c + (a,))

    # header
    d.rectangle(P(0, 0, W, 52), fill=(255, 255, 255, 170))
    d.line(P(0, 52, W, 52), fill=LINE + (255,), width=1 * s)
    title = font(16 * s, 600)
    d.text((24 * s, 26 * s), 'Shittim Logon', font=title, fill=INK + (255,), anchor='lm')
    d.text((24 * s + title.getlength('Shittim Logon') + 10 * s, 27 * s), 'for macOS', font=font(12 * s), fill=SUB + (255,),
           anchor='lm')

    # plates under the icons (room for Finder's label below each)
    for cx, cy in (APP, APPS):
        half = ICON // 2 + 18
        d.rounded_rectangle(P(cx - half, cy - half, cx + half, cy + half + 18), radius=16 * s,
                            fill=(255, 255, 255, 150), outline=LINE + (255,), width=1 * s)

    # the drag: a label and a plain arrow
    mid = (APP[0] + APPS[0]) / 2
    d.text((mid * s, (APP[1] - 18) * s), '拖入安装', font=font(13 * s, 500), fill=INK + (255,), anchor='ms')
    x0, x1, y = APP[0] + ICON // 2 + 34, APPS[0] - ICON // 2 - 34, APP[1] + 4
    d.line(P(x0, y, x1, y), fill=PURPLE + (230,), width=2 * s)
    d.line(P(x1 - 9, y - 8, x1, y, x1 - 9, y + 8), fill=PURPLE + (230,), width=2 * s, joint='curve')

    # the steps
    top = 278
    d.rounded_rectangle(P(24, top, W - 24, H - BAR_SAFE), radius=12 * s, fill=(255, 255, 255, 200),
                        outline=LINE + (255,), width=1 * s)
    steps = [
        ('1. 将 Shittim Logon 拖到「应用程序」文件夹。', 'Drag Shittim Logon into the Applications folder.'),
        ('2. 首次打开若无法打开：前往「系统设置 › 隐私与安全性」，在页面下方点「仍要打开」。',
         "If it won't open: System Settings \u203a Privacy & Security, then Open Anyway."),
    ]
    body, en = font(12 * s, 450), font(10 * s)
    y = top + 14
    for zh, eng in steps:
        d.text((40 * s, y * s), zh, font=body, fill=INK + (255,))
        d.text((40 * s, (y + 19) * s), eng, font=en, fill=SUB + (255,))
        y += 46

    return Image.alpha_composite(im.convert('RGBA'), over).convert('RGB')


def main():
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    draw(1).save(os.path.join(out, 'dmg-background.png'))
    draw(2).save(os.path.join(out, 'dmg-background@2x.png'))


if __name__ == '__main__':
    main()
