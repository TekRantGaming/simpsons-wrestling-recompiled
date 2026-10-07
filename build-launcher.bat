@echo off
rem Build the launcher (SimpsonsWrestling.exe) with Visual Studio 2022 Build Tools.
setlocal
set "PATH=C:\Program Files (x86)\Microsoft Visual Studio\Installer;%PATH%"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
cd /d "%~dp0"
if not exist build-launcher\build.ninja (
  cmake -S launcher -B build-launcher -G Ninja -DCMAKE_BUILD_TYPE=Release || exit /b 1
)
cmake --build build-launcher --target SimpsonsWrestling || exit /b 1
for %%D in (build build-dev) do if exist %%D copy /y build-launcher\SimpsonsWrestling.exe %%D\ >nul
echo Built build-launcher\SimpsonsWrestling.exe
