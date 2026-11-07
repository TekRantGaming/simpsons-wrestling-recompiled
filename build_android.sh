#!/usr/bin/env bash
# Build The Simpsons Wrestling for Android TV: android/app/build/outputs/apk/
# release/app-release.apk, for armeabi-v7a (32-bit ARM: runs on 32- and 64-bit
# TV boxes). Add 64-bit with -Pabis=armeabi-v7a,arm64-v8a.
#
# Needs the Android SDK with NDK 27.2.12479018 and CMake 3.31.6 (Gradle installs
# missing SDK parts itself when ANDROID_HOME points at an SDK whose licences
# are accepted), Java 17+, and generated/ -- the game translated from your own
# disc first:
#
#   python3 psxrecomp/psxrecomp_cli.py generate --config game.toml --project-root . \
#       --disc "path/to/Simpsons Wrestling, The (USA).cue"
#
# Extra arguments go to Gradle, e.g.  ./build_android.sh -Pabis=armeabi-v7a,arm64-v8a
set -euo pipefail
cd "$(dirname "$0")"

if [ -z "${ANDROID_HOME:-}" ] && [ -z "${ANDROID_SDK_ROOT:-}" ] && [ ! -f android/local.properties ]; then
  echo "Set ANDROID_HOME to your Android SDK (or write sdk.dir=... into android/local.properties)." >&2
  exit 1
fi
git submodule update --init --recursive

cd android
./gradlew assembleRelease "$@"

echo
echo "Built android/app/build/outputs/apk/release/app-release.apk"
echo "Install on the TV:  adb connect <tv-ip> && adb install -r android/app/build/outputs/apk/release/app-release.apk"
