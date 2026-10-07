@echo off
rem Retrain the profile that build.bat optimises with (pgo\windows.profdata).
rem   build-pgo.bat "path\to\Simpsons Wrestling, The (USA).cue"
rem Builds an instrumented game in build-pgo-gen\, plays the attract demo for a
rem few minutes (a game window opens; leave it alone) and writes the profile.
rem Only needed after large changes to the game C or the runtime.
setlocal
if "%~1"=="" (
  echo usage: build-pgo.bat "path\to\game.cue"
  exit /b 1
)
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
rem clang-cl compiles -fprofile-instr-generate, but link.exe needs the profile runtime named.
set "LIB=%VCToolsInstallDir%..\..\Llvm\x64\lib\clang\19\lib\windows;%LIB%"
if exist build-pgo-gen\build.ninja goto build
cmake -S . -B build-pgo-gen -G Ninja -DCMAKE_BUILD_TYPE=Release -DPSX_PGO=generate -DCMAKE_EXE_LINKER_FLAGS=clang_rt.profile-x86_64.lib -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON || exit /b 1
:build
cmake --build build-pgo-gen --target psx-runtime || exit /b 1
python tools\pgo_train.py --disc "%~1" || exit /b 1
echo.
echo Wrote pgo\windows.profdata. Run build.bat to build the optimised game.
