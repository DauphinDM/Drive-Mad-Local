#!/bin/sh
set -eu
# Build a self-contained unsigned .app and .dmg from the native Mach-O binary.
BINARY="${BINARY:-native/build/drive-mad}"
VERSION="${VERSION:-1.0.0}"
OUT="${OUT:-native/build/release/macos}"
ICON="${ICON:-packaging/icon.png}"
ARCH="$(uname -m)"
APP="$OUT/Drive Mad.app"
CONTENTS="$APP/Contents"
FRAMEWORKS="$CONTENTS/Frameworks"

rm -rf "$OUT"
mkdir -p "$CONTENTS/MacOS" "$CONTENTS/Resources" "$FRAMEWORKS"
cp "$BINARY" "$CONTENTS/MacOS/drive-mad"
chmod 755 "$CONTENTS/MacOS/drive-mad"
cp "$ICON" "$OUT/icon.png"

# Collect Homebrew dylibs referenced by the executable and its dependencies.
collect() {
  otool -L "$1" 2>/dev/null | awk 'NR>1 {print $1}' | while read -r lib; do
    case "$lib" in
      /opt/homebrew/*|/usr/local/*)
        name="$(basename "$lib")"
        if [ ! -f "$FRAMEWORKS/$name" ]; then
          cp "$lib" "$FRAMEWORKS/$name"
          chmod 755 "$FRAMEWORKS/$name"
          install_name_tool -id "@executable_path/../Frameworks/$name" "$FRAMEWORKS/$name" 2>/dev/null || true
          collect "$FRAMEWORKS/$name"
        fi
        ;;
    esac
  done
}
collect "$CONTENTS/MacOS/drive-mad"
for lib in "$FRAMEWORKS"/*.dylib; do
  [ -f "$lib" ] || continue
  otool -L "$lib" 2>/dev/null | awk 'NR>1 {print $1}' | while read -r dep; do
    case "$dep" in
      /opt/homebrew/*|/usr/local/*)
        name="$(basename "$dep")"
        if [ -f "$FRAMEWORKS/$name" ]; then
          install_name_tool -change "$dep" "@executable_path/../Frameworks/$name" "$lib" 2>/dev/null || true
        fi
        ;;
    esac
  done
done
# Point the executable at bundled copies of every Homebrew dependency.
otool -L "$CONTENTS/MacOS/drive-mad" | awk 'NR>1 {print $1}' | while read -r dep; do
  case "$dep" in
    /opt/homebrew/*|/usr/local/*)
      name="$(basename "$dep")"
      install_name_tool -change "$dep" "@executable_path/../Frameworks/$name" "$CONTENTS/MacOS/drive-mad" 2>/dev/null || true
      ;;
  esac
done

# Build an .icns from the 512px source.
ICONSET="$OUT/DriveMad.iconset"
mkdir -p "$ICONSET"
for size in 16 32 64 128 256 512; do
  sips -z "$size" "$size" "$ICON" --out "$ICONSET/icon_${size}x${size}.png" >/dev/null
done
sips -z 32 32 "$ICON" --out "$ICONSET/icon_16x16@2x.png" >/dev/null
sips -z 64 64 "$ICON" --out "$ICONSET/icon_32x32@2x.png" >/dev/null
sips -z 256 256 "$ICON" --out "$ICONSET/icon_128x128@2x.png" >/dev/null
sips -z 512 512 "$ICON" --out "$ICONSET/icon_256x256@2x.png" >/dev/null
sips -z 1024 1024 "$ICON" --out "$ICONSET/icon_512x512@2x.png" >/dev/null
iconutil -c icns "$ICONSET" -o "$CONTENTS/Resources/DriveMad.icns"
rm -rf "$ICONSET"

cat > "$CONTENTS/Info.plist" <<'PLIST'
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>Drive Mad</string>
  <key>CFBundleExecutable</key><string>drive-mad</string>
  <key>CFBundleIconFile</key><string>DriveMad.icns</string>
  <key>CFBundleIdentifier</key><string>com.dauphindm.drivemad</string>
  <key>CFBundleName</key><string>Drive Mad</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>__VERSION__</string>
  <key>CFBundleVersion</key><string>__VERSION__</string>
  <key>LSMinimumSystemVersion</key><string>10.15</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
PLIST
sed -i '' "s/__VERSION__/$VERSION/g" "$CONTENTS/Info.plist"

# Ad-hoc signing lets the app run locally; it is not notarized.
codesign --force --deep --sign - "$APP" >/dev/null 2>&1 || true

DMG="$OUT/DriveMad-${VERSION}-macos-${ARCH}.dmg"
STAGE="$OUT/dmg"
rm -rf "$STAGE"
mkdir -p "$STAGE"
cp -R "$APP" "$STAGE/"
ln -s /Applications "$STAGE/Applications"
hdiutil create -volname "Drive Mad" -srcfolder "$STAGE" -ov -format UDZO "$DMG" >/dev/null
rm -rf "$STAGE"
echo "$DMG"
