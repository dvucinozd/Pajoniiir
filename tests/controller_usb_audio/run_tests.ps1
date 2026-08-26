$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$P4 = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_audio"
$S3 = Join-Path $RepoRoot "firmware/control-board-s3/components/flx4_usb_audio"
$Exe = Join-Path $BuildDir "test_controller_usb_audio"
if ($IsWindows) { $Exe += ".exe" }

# Pure descriptor and packetizer sources must remain byte-identical to the
# accepted S3 implementation until physical P4 USB Audio validation begins.
if ((Get-FileHash (Join-Path $P4 "flx4_uac_descriptors.c")).Hash -ne
    (Get-FileHash (Join-Path $S3 "flx4_uac_descriptors.c")).Hash) { exit 1 }
if ((Get-FileHash (Join-Path $P4 "flx4_uac_packetizer.c")).Hash -ne
    (Get-FileHash (Join-Path $S3 "flx4_uac_packetizer.c")).Hash) { exit 1 }

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror `
    "-I$(Join-Path $P4 'include')" `
    (Join-Path $P4 "flx4_uac_descriptors.c") `
    (Join-Path $P4 "flx4_uac_packetizer.c") `
    (Join-Path $P4 "controller_audio_ring.c") `
    (Join-Path $P4 "controller_audio_resampler.c") `
    (Join-Path $PSScriptRoot "test_controller_usb_audio.c") `
    -o $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe
exit $LASTEXITCODE
