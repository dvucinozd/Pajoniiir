$ErrorActionPreference = "Stop"
$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$Component = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_audio"
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Target = Join-Path $BuildDir "test_controller_usb_audio_stream"
if ($env:OS -eq "Windows_NT") { $Target += ".exe" }
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror `
    "-I$PSScriptRoot/stubs" "-I$Component" "-I$Component/include" `
    (Join-Path $PSScriptRoot "test_controller_usb_audio_stream.c") `
    (Join-Path $Component "controller_audio_ring.c") `
    (Join-Path $Component "controller_audio_resampler.c") `
    (Join-Path $Component "flx4_uac_packetizer.c") `
    (Join-Path $Component "flx4_uac_descriptors.c") -o $Target
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Target
exit $LASTEXITCODE
