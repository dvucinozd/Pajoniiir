$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Exe = Join-Path $BuildDir "test_controller_runtime"
if ($IsWindows) { $Exe += ".exe" }

$Runtime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_runtime"
$Codec = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Control = Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/include"
$Stubs = Join-Path $PSScriptRoot "stubs"
$Test = Join-Path $PSScriptRoot "test_controller_runtime.c"

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror `
    "-I$(Join-Path $Runtime 'include')" `
    "-I$(Join-Path $Codec 'include')" `
    "-I$Control" `
    "-I$Stubs" `
    (Join-Path $Runtime "flx4_map.c") `
    (Join-Path $Runtime "controller_runtime.c") `
    $Test `
    -o $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe
exit $LASTEXITCODE
