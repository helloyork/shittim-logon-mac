"""The Mac app icon, out of the one icon this project owns.

`assets/Tran_Shitim_Icon.png` is the project's own drawing (res/make-icon.ps1 makes the
Windows .ico from the same file; neither is in this repository): a white mark with a
translucent ring, on nothing.
macOS 11 and later draw every app icon as a tile -- a rounded square of 824 px on a
1024 px canvas -- so the mark is set on one here: a soft gradient, no gloss or shadow,
the mark at 70% of the tile. The colour is a violet: the hue of the peak of one of
the flash ramps in render/src/fx_layer.h (0.985, 0.282, 1.0), about 299 degrees -- at that
ramp's full saturation the white mark would barely stand out, so the tile takes the hue
at less saturation and full brightness, lighter at the top.

Written at sync time, never checked in, for make-icon.ps1's reason: *.png is blocked
along with every other binary product. sync.sh runs this on the Windows host and ships
the PNG; build.sh turns it into AppIcon.icns with sips and iconutil.

    python macos/res/make-icon.py <out.png> [violet|violet-deep|navy|sky|light]
"""
import os
import sys

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(HERE, '..', '..', 'assets', 'Tran_Shitim_Icon.png')

S = 1024
TILE = 824          # the macOS icon grid: the tile is 824 of 1024, centred
MARK = 0.70         # the mark's longer side, as a share of the tile

# top colour, bottom colour, the mark's colour (None: as drawn, white)
VARIANTS = {
    # HSV (299, .30, .99) to (295, .44, .88) -- the first try, kept as violet-deep,
    # read too dark
    'violet': ((251, 177, 252), (216, 126, 224), None),
    'violet-deep': ((233, 129, 235), (145, 60, 158), None),
    'navy': ((52, 78, 140), (30, 46, 92), None),
    'sky': ((86, 178, 240), (40, 112, 210), None),
    'light': ((250, 251, 253), (226, 232, 242), (40, 70, 140)),
}


def squircle(size, n=5.0, ss=4):
    """The tile's outline, |x|^n + |y|^n <= 1, drawn 4x and scaled down for the edge."""
    big = size * ss
    m = Image.new('L', (big, big), 0)
    px = m.load()
    r = big / 2
    for y in range(big):
        t = 1 - abs((y + 0.5 - r) / r) ** n
        if t <= 0:
            continue
        dx = t ** (1 / n) * r
        for x in range(max(0, int(round(r - dx))), min(big, int(round(r + dx)))):
            px[x, y] = 255
    return m.resize((size, size), Image.LANCZOS)


def gradient(size, top, bottom):
    g = Image.new('RGBA', (1, size))
    for y in range(size):
        t = y / (size - 1)
        g.putpixel((0, y), tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3)) + (255,))
    return g.resize((size, size))


def mark(size, tint):
    im = Image.open(SRC).convert('RGBA')
    im = im.crop(im.getchannel('A').getbbox())
    w, h = im.size
    k = size / max(w, h)
    im = im.resize((round(w * k), round(h * k)), Image.LANCZOS)
    if tint:
        solid = Image.new('RGBA', im.size, tint + (255,))
        solid.putalpha(im.getchannel('A'))
        im = solid
    return im


def main():
    out = sys.argv[1]
    top, bottom, tint = VARIANTS[sys.argv[2] if len(sys.argv) > 2 else 'violet']
    icon = Image.new('RGBA', (S, S), (0, 0, 0, 0))
    tile = gradient(TILE, top, bottom)
    tile.putalpha(squircle(TILE))
    off = (S - TILE) // 2
    icon.alpha_composite(tile, (off, off))
    m = mark(int(TILE * MARK), tint)
    icon.alpha_composite(m, ((S - m.width) // 2, (S - m.height) // 2))
    icon.save(out)


if __name__ == '__main__':
    main()
