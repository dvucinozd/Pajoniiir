$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Component = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Include = Join-Path $Component "include"

$CodecExe = Join-Path $BuildDir "test_usb_midi_codec"
$GateExe = Join-Path $BuildDir "test_controller_midi_out_gate"
if ($IsWindows) {
    $CodecExe += ".exe"
    $GateExe += ".exe"
}

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror "-I$Include" `
    (Join-Path $Component "usb_midi_codec.c") `
    (Join-Path $PSScriptRoot "test_usb_midi_codec.c") `
    -o $CodecExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $CodecExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror "-I$Include" `
    (Join-Path $Component "controller_midi_out_gate.c") `
    (Join-Path $PSScriptRoot "test_controller_midi_out_gate.c") `
    -o $GateExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $GateExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& (Join-Path $RepoRoot "tests/controller_usb_audio/run_tests.ps1")
exit $LASTEXITCODE
