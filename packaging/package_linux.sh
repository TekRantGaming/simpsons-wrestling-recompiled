#!/usr/bin/env bash
# Packages build-linux/ into dist/TheSimpsonsWrestling-Recompiled-v<VERSION>-linux-x64.tar.gz.
# Run ./build_linux.sh first. No game data is included.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"
bin="$root/build-linux"
name="TheSimpsonsWrestling-Recompiled-v$version-linux-x64"
stage="$root/dist/$name"
rm -rf "$stage" && mkdir -p "$stage/saves"
cp "$bin/SimpsonsWrestling" "$bin/SimpsonsWrestling_Recompiled" "$root/game.toml" "$bin/game_options.toml" "$stage/"
[ -f "$bin/psx_game_version.txt" ] && cp "$bin/psx_game_version.txt" "$stage/"
for d in assets bios mods; do cp -r "$bin/$d" "$stage/"; done
cp "$root/README.md" "$stage/"
mkdir -p "$stage/docs" && cp -r "$root/docs/images" "$stage/docs/"
tar -C "$root/dist" -czf "$root/dist/$name.tar.gz" "$name"
echo "$root/dist/$name.tar.gz"
