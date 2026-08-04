$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Exe = Join-Path $BuildDir "test_usb_midi_codec"
if ($IsWindows) { $Exe += ".exe" }

$Include = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host/include"
$Source = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host/usb_midi_codec.c"
$Test = Join-Path $PSScriptRoot "test_usb_midi_codec.c"

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror "-I$Include" $Source $Test -o $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe
exit $LASTEXITCODE
