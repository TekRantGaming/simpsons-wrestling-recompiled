# Packages build\ into dist\SimpsonsWrestling-v<VERSION>-windows-x64.zip.
# Run build.bat and build-launcher.bat first. No game data is included: players
# select their own disc in the launcher.
#
# Layout: the launcher (SimpsonsWrestling.exe) is the only program at the top;
# the game program, its data and the settings and saves it creates live in
# game\. The launcher finds the game there (and its own launcher.txt, which
# the game's plugin reads too).
param([string]$Version = (Get-Content (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\VERSION")).Trim())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$bin = Join-Path $root 'build'
$name = "SimpsonsWrestling-v$Version-windows-x64"
$stage = Join-Path $root "dist\$name"
$game = Join-Path $stage 'game'
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$game\saves" | Out-Null
Copy-Item "$bin\SimpsonsWrestling.exe", "$root\README.md" $stage
# the README's pictures, so it renders in a Markdown viewer
New-Item -ItemType Directory -Force "$stage\docs" | Out-Null
Copy-Item "$root\docs\images" "$stage\docs" -Recurse
Copy-Item "$bin\SimpsonsWrestling_Recompiled.exe", "$root\game.toml", "$bin\game_options.toml", "$bin\psx_game_version.txt" $game
foreach ($d in 'assets', 'bios', 'mods') { Copy-Item "$bin\$d" $game -Recurse }
# mods\state.toml belongs to the player's copy, not the release
Remove-Item -Force -ErrorAction SilentlyContinue "$game\mods\state.toml"
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
"$zip"
