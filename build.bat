@echo off
rem Build SimpsonsWrestling_Recompiled.exe with Visual Studio 2022 Build Tools + clang-cl.
rem (Plain MSVC cl.exe fails on the runtime's C11 atomics, so clang-cl is used.)
rem Optimised with ThinLTO (lld-link) and profile-guided optimisation from
rem pgo\windows.profdata; build-pgo.bat retrains that profile.
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
set "PGO=-DPSX_PGO="
if not exist pgo\windows.profdata goto configure
if not exist build\pgo mkdir build\pgo
copy /y pgo\windows.profdata build\pgo\default.profdata >nul || exit /b 1
set "PGO=-DPSX_PGO=use"
:configure
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release %PGO% -DPSX_RUNTIME_IPO=ON -DCMAKE_LINKER=lld-link -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_DISABLE_FIND_PACKAGE_SDL3=ON || exit /b 1
cmake --build build --target psx-runtime || exit /b 1
echo.
echo Built build\SimpsonsWrestling_Recompiled.exe  (run with --no-launcher to skip the launcher)
