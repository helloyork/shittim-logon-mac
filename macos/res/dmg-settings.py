# shittim-logon / macos / res / dmg-settings.py
#
# dmgbuild's settings for the disk image (build.sh `dmg`): the app, an Applications
# link, and make-dmg-background.py's background -- the window size and the two icon
# centres are that script's W, H, APP and APPS.
#
#   dmgbuild -s dmg-settings.py -D app=<Shittim Logon.app> -D background=<tiff> \
#            "Shittim Logon" <out.dmg>

import os.path

# dmgbuild 1.6.5 -- the newest pip offers macOS's Python 3.9 -- also writes a `pBBk`
# bookmark for the background, and Finder (macOS 15) prefers it, fails to resolve it and
# shows no background at all, while the window size and icon places still apply
# (seen 2026-11-20). 1.6.7, the one electron-builder bundles and whose
# windows do show their background, no longer writes it. Same here: no bookmark, only
# the alias in the view options.
try:
    import dmgbuild.core as _core
    if hasattr(_core, 'Bookmark'):
        _core.Bookmark.for_file = staticmethod(lambda path: None)
except ImportError:
    pass

app = defines['app']                       # noqa: F821 (dmgbuild provides `defines`)
name = os.path.basename(app)

format = 'UDZO'
filesystem = 'HFS+'
files = [app]
symlinks = {'Applications': '/Applications'}

background = defines['background']         # noqa: F821
# The whole window frame: the 660 x 430 background plus the title bar and borders
# (30 pt, measured on macOS 14), so the icon view
# shows the image exactly when no path or status bar is on.
window_rect = ((200, 140), (660, 460))
default_view = 'icon-view'
show_status_bar = False
show_tab_view = False
show_toolbar = False
show_pathbar = False
show_sidebar = False
icon_size = 104
text_size = 13
icon_locations = {name: (170, 176), 'Applications': (490, 176)}
