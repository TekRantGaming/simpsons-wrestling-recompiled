#!/usr/bin/env bash
# Packages build-linux/ into dist/SimpsonsWrestling-v<VERSION>-linux-x64.tar.gz
# with the release layout (the launcher at the top, the game in game/; see
# package_windows.ps1). Run ./build_linux.sh first. No game data is included.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
version="$(tr -d '[:space:]' < "$root/VERSION")"
bin="$root/build-linux"
name="SimpsonsWrestling-v$version-linux-x64"
stage="$root/dist/$name"
rm -rf "$stage" && mkdir -p "$stage/game/saves" "$stage/docs"
cp "$bin/SimpsonsWrestling" "$root/README.md" "$stage/"
cp -r "$root/docs/images" "$stage/docs/"
cp "$bin/SimpsonsWrestling_Recompiled" "$root/game.toml" "$bin/game_options.toml" "$bin/psx_game_version.txt" "$stage/game/"
for d in assets bios mods; do cp -r "$bin/$d" "$stage/game/"; done
rm -f "$stage/game/mods/state.toml"
tar -C "$root/dist" -czf "$root/dist/$name.tar.gz" "$name"
echo "$root/dist/$name.tar.gz"
