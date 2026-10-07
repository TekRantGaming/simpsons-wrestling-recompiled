# Packages build\ into dist\SimpsonsWrestling-v<VERSION>-windows-x64.zip.
# Run build.bat and build-launcher.bat first. No game data is included: players
# select their own disc in the launcher.
param([string]$Version = (Get-Content (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\VERSION")).Trim())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$bin = Join-Path $root 'build'
$name = "SimpsonsWrestling-v$Version-windows-x64"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\saves" | Out-Null
Copy-Item "$bin\SimpsonsWrestling.exe", "$bin\SimpsonsWrestling_Recompiled.exe", "$root\game.toml", "$bin\game_options.toml" $stage
if (Test-Path "$bin\psx_game_version.txt") { Copy-Item "$bin\psx_game_version.txt" $stage }
foreach ($d in 'assets', 'bios', 'mods') { Copy-Item "$bin\$d" $stage -Recurse }
# mods\state.toml belongs to the player's copy, not the release
Remove-Item -Force -ErrorAction SilentlyContinue "$stage\mods\state.toml"
Copy-Item "$root\README.md" "$stage\README.md"
# the README's pictures, so it renders in a Markdown viewer
New-Item -ItemType Directory -Force "$stage\docs" | Out-Null
Copy-Item "$root\docs\images" "$stage\docs" -Recurse
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
"$zip"
