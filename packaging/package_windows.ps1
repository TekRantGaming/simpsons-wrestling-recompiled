# Packages build\ into dist\TheSimpsonsWrestling-Recompiled-v<VERSION>-windows-x64.zip.
# Run build.bat and build-launcher.bat first. No game data is included: players
# select their own disc in the launcher.
param([string]$Version = (Get-Content (Join-Path (Split-Path -Parent $MyInvocation.MyCommand.Path) "..\VERSION")).Trim())
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$bin = Join-Path $root 'build'
$name = "TheSimpsonsWrestling-Recompiled-v$Version-windows-x64"
$stage = Join-Path $root "dist\$name"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage }
New-Item -ItemType Directory -Force "$stage\saves" | Out-Null
Copy-Item "$bin\SimpsonsWrestling.exe", "$bin\SimpsonsWrestling_Recompiled.exe", "$root\game.toml", "$bin\game_options.toml" $stage
if (Test-Path "$bin\psx_game_version.txt") { Copy-Item "$bin\psx_game_version.txt" $stage }
foreach ($d in 'assets', 'bios', 'mods') { Copy-Item "$bin\$d" $stage -Recurse }
Copy-Item "$root\README.md" "$stage\README.md"
$zip = Join-Path $root "dist\$name.zip"
if (Test-Path $zip) { Remove-Item -Force $zip }
Compress-Archive -Path $stage -DestinationPath $zip
"$zip"
