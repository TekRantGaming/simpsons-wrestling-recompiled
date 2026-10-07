#!/usr/bin/env bash
# Builds the Linux AppImage from build-linux/ (run build_linux.sh first). Run in
# Linux or WSL from anywhere:
#
#   packaging/build_appimage.sh
#
# Output: dist/SimpsonsWrestling-v<VERSION>-linux-x86_64.AppImage. No game data
# goes in: players pick their own disc image in the launcher.
#
# The launcher and the game keep their settings, saves and caches next to their
# own program files, and an AppImage is read-only. AppRun therefore copies the
# program into a writable folder next to the AppImage, SimpsonsWrestling-data
# (~/.local/share/SimpsonsWrestlingRecompiled when that is not writable), and
# runs it from there. A newer AppImage replaces only the program files in that
# folder; settings and saves stay.
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
VERSION="$(tr -d '\r\n' < "$ROOT/VERSION")"
BIN="$ROOT/build-linux"
OUT="$ROOT/dist/SimpsonsWrestling-v$VERSION-linux-x86_64.AppImage"
TOOL="${APPIMAGETOOL:-$HOME/appimagetool-x86_64.AppImage}"

for f in SimpsonsWrestling SimpsonsWrestling_Recompiled psx_game_version.txt; do
  [ -f "$BIN/$f" ] || { echo "build-linux/$f is missing: run build_linux.sh first." >&2; exit 1; }
done
if [ "$(tr -d '\r\n' < "$BIN/psx_game_version.txt")" != "$VERSION" ]; then
  echo "build-linux is version $(cat "$BIN/psx_game_version.txt"), VERSION says $VERSION: rebuild first." >&2
  exit 1
fi

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT
APPDIR="$WORK/AppDir"
PROG="$APPDIR/usr/share/simpsons-wrestling"
# The Windows zip's layout: the launcher at the top, the game in game/.
mkdir -p "$PROG/game/mods" "$PROG/docs"
cp "$BIN/SimpsonsWrestling" "$ROOT/README.md" "$PROG/"
cp -r "$ROOT/docs/images" "$PROG/docs/"
cp "$BIN/SimpsonsWrestling_Recompiled" "$BIN/psx_game_version.txt" "$BIN/game_options.toml" "$ROOT/game.toml" \
   "$PROG/game/"
cp -r "$BIN/assets" "$BIN/bios" "$PROG/game/"
# Bundled mod packages only: mods/state.toml belongs to the player's folder.
cp -r "$BIN/mods/bundled" "$PROG/game/mods/"
[ -f "$BIN/mods/README.md" ] && cp "$BIN/mods/README.md" "$PROG/game/mods/"
chmod +x "$PROG/SimpsonsWrestling" "$PROG/game/SimpsonsWrestling_Recompiled"

cp "$ROOT/packaging/simpsons-wrestling.png" "$APPDIR/simpsons-wrestling.png"
cat > "$APPDIR/simpsons-wrestling.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=The Simpsons Wrestling
Comment=The Simpsons Wrestling (PlayStation), native PC port
Exec=SimpsonsWrestling
Icon=simpsons-wrestling
Categories=Game;
Terminal=false
EOF
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
# Runs a copy of the program from a writable folder (see build_appimage.sh).
HERE="$(dirname "$(readlink -f "$0")")"
PROG="$HERE/usr/share/simpsons-wrestling"
DATA="$(dirname "${APPIMAGE:-$HERE}")/SimpsonsWrestling-data"
if ! mkdir -p "$DATA" 2>/dev/null || [ ! -w "$DATA" ]; then
  DATA="${XDG_DATA_HOME:-$HOME/.local/share}/SimpsonsWrestlingRecompiled"
  mkdir -p "$DATA" || exit 1
fi
if ! cmp -s "$PROG/game/SimpsonsWrestling_Recompiled" "$DATA/game/SimpsonsWrestling_Recompiled" ||
   ! cmp -s "$PROG/SimpsonsWrestling" "$DATA/SimpsonsWrestling"; then
  rm -rf "$DATA/game/mods/bundled"
  cp -rf "$PROG/." "$DATA/" || exit 1
fi
mkdir -p "$DATA/game/saves"
# The launcher's updater replaces this AppImage file.
[ -n "$APPIMAGE" ] && export SW_APPIMAGE="$APPIMAGE"
# The runtime anchors its files on $APPIMAGE's folder when that is set (for a
# game run from inside the image); this copy lives in $DATA instead.
unset APPIMAGE APPDIR ARGV0 OWD
exec "$DATA/SimpsonsWrestling" "$@"
EOF
chmod +x "$APPDIR/AppRun"

if [ ! -x "$TOOL" ]; then
  echo "appimagetool not found at $TOOL (set APPIMAGETOOL, or download it from" >&2
  echo "https://github.com/AppImage/appimagetool/releases)." >&2
  exit 1
fi
mkdir -p "$ROOT/dist"
rm -f "$OUT"
ARCH=x86_64 "$TOOL" --appimage-extract-and-run --no-appstream "$APPDIR" "$OUT"
ls -la "$OUT"
