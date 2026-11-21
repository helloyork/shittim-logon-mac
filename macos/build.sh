#!/bin/bash
# Run on the Mac: sh ~/shittim-mac/src/macos/build.sh [target...]
#
#   dist     everything a release is: lockscreen settings app dmg
#   app      bin/Shittim Logon.app -- the settings app as the program, the overlay
#            inside it as a login item (Contents/Library/LoginItems), the assets, the
#            icon and the text files. Needs lockscreen and settings built first.
#   dmg      bin/ShittimLogon-<version>-mac.dmg: the app and an Applications link in a
#            window that says what to do (res/make-dmg-background.py, res/dmg-settings.py).
#   SIGN_ID="Developer ID Application: ..." signs with that identity (hardened
#   runtime, timestamp); without it everything is signed ad hoc.
#
# The Windows sources are used as they are. They are copied into a stage directory
# with the same layout, and only the files bound to Windows are replaced by the ones
# under macos/replace/. Nothing under render/ or overlay/ is edited for the Mac.
set -e
SRC=~/shittim-mac/src
read -r VERSION BUILD < "$SRC/macos/VERSION"
SIGN=(--force --sign "${SIGN_ID:--}")
[ -n "$SIGN_ID" ] && SIGN+=(--options runtime --timestamp)
ARGS=()
for t in "$@"; do
    if [ "$t" = dist ]; then ARGS+=(lockscreen settings app dmg); else ARGS+=("$t"); fi
done
set -- "${ARGS[@]}"
STAGE=~/shittim-mac/stage
OUT=~/shittim-mac/bin
mkdir -p "$OUT"

rm -rf "$STAGE"
mkdir -p "$STAGE/render" "$STAGE/overlay"
cp -R "$SRC/render/src" "$STAGE/render/src"
cp -R "$SRC/overlay/src" "$STAGE/overlay/src"
cp -R "$SRC/overlay/shaders" "$STAGE/overlay/shaders"
# the settings app compiles the Windows settings app's file editor and string table
mkdir -p "$STAGE/config" "$STAGE/ui"
cp -R "$SRC/config/src" "$STAGE/config/src"
cp "$SRC/ui/strings.h" "$STAGE/ui/strings.h"
cp -R "$SRC/macos/replace/." "$STAGE/"
mkdir -p "$STAGE/macos"
cp -R "$SRC/macos/src" "$STAGE/macos/src"
cp -R "$SRC/macos/settings" "$STAGE/macos/settings"

SPINE="$SRC/render/vendor/spine-cpp"
CXX="clang++ -std=c++17 -O2 -arch arm64 -mmacosx-version-min=13.0"
INC="-I$SPINE/include -I$SRC/macos/compat"

# spine-cpp, once per source change
LIB=~/shittim-mac/obj/libspine.a
if [ ! -f "$LIB" ] || [ -n "$(find "$SPINE/src" -newer "$LIB" -name '*.cpp' | head -1)" ]; then
    mkdir -p ~/shittim-mac/obj/spine
    for f in "$SPINE"/src/spine/*.cpp; do
        $CXX $INC -c "$f" -o ~/shittim-mac/obj/spine/$(basename "$f" .cpp).o &
    done
    wait
    rm -f "$LIB"; ar rcs "$LIB" ~/shittim-mac/obj/spine/*.o
    echo "built $LIB"
fi

FW="-framework CoreFoundation -framework CoreGraphics -framework ImageIO"
MFW="$FW -framework Foundation -framework Metal -framework QuartzCore -framework AppKit -framework CoreText -framework AVFoundation -framework AudioToolbox -framework CoreAudio"
OBJC="-x objective-c++ -fobjc-arc -Wno-deprecated-declarations"
for t in "${@:-list_animations}"; do
    case "$t" in
        list_animations) $CXX $INC "$STAGE/render/src/list_animations.cpp" "$LIB" $FW -o "$OUT/list_animations" ;;
        still)           $CXX $OBJC $INC "$STAGE/macos/src/still.mm" -x none "$LIB" $MFW -o "$OUT/still" ;;
        voicetest)       $CXX $OBJC $INC "$STAGE/macos/src/voicetest.mm" -x none "$LIB" $MFW -o "$OUT/voicetest" ;;
        clocktest)       $CXX $OBJC $INC "$STAGE/macos/src/clocktest.mm" -x none "$LIB" $MFW -o "$OUT/clocktest" ;;
        logtest)         $CXX $OBJC $INC "$STAGE/macos/src/logtest.mm" -x none "$LIB" $MFW -o "$OUT/logtest" ;;
        lockscreen)
            # An app bundle, so `open` can start it in the GUI session from SSH.
            APP="$OUT/ShittimLock.app"
            mkdir -p "$APP/Contents/MacOS"
            $CXX $OBJC $INC "$STAGE/macos/src/lockscreen.mm" -x none "$LIB" $MFW -framework IOKit -o "$APP/Contents/MacOS/lockscreen"
            cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.shittim.lockscreen</string>
<key>CFBundleName</key><string>ShittimLock</string>
<key>CFBundleExecutable</key><string>lockscreen</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>$BUILD</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>LSUIElement</key><true/>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
            codesign "${SIGN[@]}" "$APP" ;;
        settings)
            # SwiftUI over an Objective-C++ bridge (macos/settings/bridge.h).
            APP="$OUT/ShittimSettings.app"
            mkdir -p "$APP/Contents/MacOS" ~/shittim-mac/obj
            $CXX $OBJC $INC -c "$STAGE/macos/settings/bridge.mm" -o ~/shittim-mac/obj/settings-bridge.o
            swiftc -O -parse-as-library -target arm64-apple-macos13.0 \
                -import-objc-header "$STAGE/macos/settings/bridge.h" \
                "$STAGE"/macos/settings/*.swift ~/shittim-mac/obj/settings-bridge.o \
                -lc++ -framework SwiftUI -framework AppKit -framework CoreAudio -framework AudioToolbox -framework CoreText \
                -o "$APP/Contents/MacOS/settings"
            cat > "$APP/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.shittim.settings</string>
<key>CFBundleName</key><string>Shittim Logon</string>
<key>CFBundleExecutable</key><string>settings</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>$BUILD</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
            codesign "${SIGN[@]}" "$APP" ;;
        app)
            APP="$OUT/Shittim Logon.app"
            C="$APP/Contents"
            rm -rf "$APP"
            mkdir -p "$C/MacOS" "$C/Resources" "$C/Library/LoginItems" "$C/Library/LaunchAgents"
            cp "$OUT/ShittimSettings.app/Contents/MacOS/settings" "$C/MacOS/settings"
            cp -R "$OUT/ShittimLock.app" "$C/Library/LoginItems/ShittimLock.app"
            # the icon: res/make-icon.py's PNG (sync.sh ships it) at every size iconutil wants
            ICONSET=~/shittim-mac/obj/AppIcon.iconset
            rm -rf "$ICONSET"; mkdir -p "$ICONSET"
            for s in 16 32 128 256 512; do
                sips -z $s $s "$SRC/macos/res/appicon-1024.png" --out "$ICONSET/icon_${s}x${s}.png" >/dev/null
                sips -z $((s * 2)) $((s * 2)) "$SRC/macos/res/appicon-1024.png" --out "$ICONSET/icon_${s}x${s}@2x.png" >/dev/null
            done
            iconutil -c icns "$ICONSET" -o "$C/Resources/AppIcon.icns"
            cp "$C/Resources/AppIcon.icns" "$C/Library/LoginItems/ShittimLock.app/Contents/Resources/AppIcon.icns" 2>/dev/null || {
                mkdir -p "$C/Library/LoginItems/ShittimLock.app/Contents/Resources"
                cp "$C/Resources/AppIcon.icns" "$C/Library/LoginItems/ShittimLock.app/Contents/Resources/AppIcon.icns"; }
            /usr/libexec/PlistBuddy -c "Add :CFBundleIconFile string AppIcon" "$C/Library/LoginItems/ShittimLock.app/Contents/Info.plist"
            # the assets as the overlay reads them, and the files that ship beside the program (setup.h kDocFiles)
            # voice.pak is the Windows form of the clips; the Mac reads voice/ itself (voice_mac.h)
            ditto ~/shittim-mac/assets "$C/Resources/assets"
            rm -f "$C/Resources/assets/voice.pak"
            cp "$SRC/LICENSE.txt" "$SRC/NOTICE.txt" "$C/Resources/"
            cp "$SPINE/LICENSE" "$C/Resources/SPINE-LICENSE.txt"
            cat > "$C/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>CFBundleIdentifier</key><string>local.shittim.settings</string>
<key>CFBundleName</key><string>Shittim Logon</string>
<key>CFBundleDisplayName</key><string>Shittim Logon</string>
<key>CFBundleExecutable</key><string>settings</string>
<key>CFBundleIconFile</key><string>AppIcon</string>
<key>CFBundlePackageType</key><string>APPL</string>
<key>CFBundleShortVersionString</key><string>$VERSION</string>
<key>CFBundleVersion</key><string>$BUILD</string>
<key>LSMinimumSystemVersion</key><string>13.0</string>
<key>LSApplicationCategoryType</key><string>public.app-category.utilities</string>
<key>NSHumanReadableCopyright</key><string>© 2026 Nomen</string>
<key>NSHighResolutionCapable</key><true/>
</dict></plist>
EOF
            # The login item (Distribution.swift registers it through SMAppService):
            # started in every Aqua session at login, restarted if it fails -- a clean
            # exit (a second instance leaving) is not restarted. Interactive, so the
            # frame loop is never throttled as background work.
            cat > "$C/Library/LaunchAgents/local.shittim.lockscreen.plist" <<'EOF'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
<key>Label</key><string>local.shittim.lockscreen</string>
<key>BundleProgram</key><string>Contents/Library/LoginItems/ShittimLock.app/Contents/MacOS/lockscreen</string>
<key>AssociatedBundleIdentifiers</key><array><string>local.shittim.settings</string></array>
<key>RunAtLoad</key><true/>
<key>KeepAlive</key><dict><key>SuccessfulExit</key><false/></dict>
<key>LimitLoadToSessionType</key><string>Aqua</string>
<key>ProcessType</key><string>Interactive</string>
</dict></plist>
EOF
            # inside out: the helper, then the app that seals it
            codesign "${SIGN[@]}" "$C/Library/LoginItems/ShittimLock.app"
            codesign "${SIGN[@]}" "$APP"
            codesign --verify --deep --strict "$APP" ;;
        dmg)
            APP="$OUT/Shittim Logon.app"
            DMG="$OUT/ShittimLogon-$VERSION-mac.dmg"
            # The window: the background res/make-dmg-background.py draws (sync.sh ships
            # it, 1x and 2x, joined here into one TIFF), the icons where it expects them.
            # dmgbuild writes the window's .DS_Store itself -- no Finder, no AppleScript.
            # It lives in ~/shittim-mac/venv: python3 -m venv ~/shittim-mac/venv &&
            # ~/shittim-mac/venv/bin/pip install "dmgbuild>=1.6,<2".
            BG=~/shittim-mac/obj/dmg-background.tiff
            tiffutil -cathidpicheck "$SRC/macos/res/dmg-background.png" "$SRC/macos/res/dmg-background@2x.png" -out "$BG" 2>/dev/null
            rm -f "$DMG"
            ~/shittim-mac/venv/bin/dmgbuild -s "$SRC/macos/res/dmg-settings.py" -D app="$APP" -D background="$BG"                 "Shittim Logon" "$DMG" >/dev/null
            [ -n "$SIGN_ID" ] && codesign --force --sign "$SIGN_ID" --timestamp "$DMG"
            ls -l "$DMG" ;;
        *) echo "unknown target $t"; exit 1 ;;
    esac
    echo "built $OUT/$t"
done
