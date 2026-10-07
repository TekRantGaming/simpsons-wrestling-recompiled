#!/usr/bin/env bash
# Build The Simpsons Wrestling for Linux: the recompiled game (build-linux/)
# and the launcher, which is copied next to it. Needs cmake, ninja, gcc/g++
# and the X11/Wayland/GL/ALSA/PulseAudio development packages (see README).
set -euo pipefail
cd "$(dirname "$0")"
jobs="${JOBS:-$(nproc)}"

if [ ! -f build-linux/build.ninja ]; then
  cmake -S . -B build-linux -G Ninja -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build-linux --target psx-runtime -j "$jobs"

if [ ! -f build-linux-launcher/build.ninja ]; then
  cmake -S launcher -B build-linux-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build-linux-launcher --target SimpsonsWrestling -j "$jobs"
cp build-linux-launcher/SimpsonsWrestling build-linux/

echo
echo "Built build-linux/SimpsonsWrestling (launcher) and build-linux/SimpsonsWrestling_Recompiled"
