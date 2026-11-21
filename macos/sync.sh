#!/bin/bash
# Run on the development host (Git Bash on Windows works) from the root of a full
# source tree -- this directory beside the Windows sources it builds against, which
# are not in this repository (see README.md):
#   bash macos/sync.sh <ssh-host> [--assets <dir>]
# Ships the sources the Mac build needs to <ssh-host>:~/shittim-mac/src, and with
# --assets every file in <dir> -- skeletons, atlases, textures, fonts, voice clips you
# have the right to use -- to <ssh-host>:~/shittim-mac/assets (flat, the way the
# overlay reads them). Assets never enter this repository.
set -e
HOST=${1:?usage: bash macos/sync.sh <ssh-host> [--assets <dir>]}
cd "$(dirname "$0")/.."

tar czf - render/src overlay/src overlay/shaders render/vendor/spine-cpp vendor/stb config/src ui/strings.h macos \
        LICENSE.txt NOTICE.txt \
    | ssh -o BatchMode=yes "$HOST" 'rm -rf ~/shittim-mac/src && mkdir -p ~/shittim-mac/src && tar xzf - -C ~/shittim-mac/src'
# The app icon, made here (Pillow) from the project's own drawing -- a build product,
# so never written into the tree (macos/res/make-icon.py).
icon=$(mktemp -d)/appicon-1024.png
python macos/res/make-icon.py "$icon" "${ICON:-violet}"
ssh -o BatchMode=yes "$HOST" 'mkdir -p ~/shittim-mac/src/macos/res && cat > ~/shittim-mac/src/macos/res/appicon-1024.png' < "$icon"
rm -f "$icon"
# The disk image's background, 1x and 2x (macos/res/make-dmg-background.py), likewise.
bg=$(mktemp -d)
python macos/res/make-dmg-background.py "$bg"
tar czf - -C "$bg" dmg-background.png dmg-background@2x.png | ssh -o BatchMode=yes "$HOST" 'tar xzf - -C ~/shittim-mac/src/macos/res'
rm -rf "$bg"
echo "sources -> $HOST:~/shittim-mac/src"

if [ "$2" = "--assets" ]; then
    dir=${3:?--assets needs a directory}
    tar czf - -C "$dir" . | ssh -o BatchMode=yes "$HOST" 'mkdir -p ~/shittim-mac/assets && tar xzf - -C ~/shittim-mac/assets'
    echo "assets -> $HOST:~/shittim-mac/assets"
fi
