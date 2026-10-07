@echo off
rem Build SimpsonsWrestling_Recompiled.exe with Visual Studio 2022 Build Tools + clang-cl.
rem (Plain MSVC cl.exe fails on the runtime's C11 atomics, so clang-cl is used.)
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build\build.ninja (
  cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl || exit /b 1
)
cmake --build build --target psx-runtime || exit /b 1
echo.
echo Built build\SimpsonsWrestling_Recompiled.exe  (run with --no-launcher to skip the launcher)
