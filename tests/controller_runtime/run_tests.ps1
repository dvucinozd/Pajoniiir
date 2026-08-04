$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Runtime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_runtime"
$Codec = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Control = Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/include"
$Reconciler = Join-Path $RepoRoot "firmware/common/control_state_reconciler/include"
$Stubs = Join-Path $PSScriptRoot "stubs"

$CommonArgs = @(
    "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-I$(Join-Path $Runtime 'include')",
    "-I$(Join-Path $Codec 'include')",
    "-I$Control",
    "-I$Reconciler",
    "-I$Stubs"
)

$BufferExe = Join-Path $BuildDir "test_controller_event_buffer"
$RuntimeExe = Join-Path $BuildDir "test_controller_runtime"
if ($IsWindows) {
    $BufferExe += ".exe"
    $RuntimeExe += ".exe"
}

gcc @CommonArgs `
    (Join-Path $Runtime "controller_event_buffer.c") `
    (Join-Path $PSScriptRoot "test_controller_event_buffer.c") `
    -o $BufferExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $BufferExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc @CommonArgs `
    (Join-Path $Runtime "flx4_map.c") `
    (Join-Path $Runtime "controller_event_buffer.c") `
    (Join-Path $Runtime "controller_runtime.c") `
    (Join-Path $PSScriptRoot "test_controller_runtime.c") `
    -o $RuntimeExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $RuntimeExe
exit $LASTEXITCODE
