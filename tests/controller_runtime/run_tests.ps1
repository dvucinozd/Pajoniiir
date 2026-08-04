$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Runtime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_runtime"
$Codec = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Profile = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile"
$ProfileRuntime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile_runtime"
$Control = Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/include"
$Reconciler = Join-Path $RepoRoot "firmware/common/control_state_reconciler/include"
$Stubs = Join-Path $PSScriptRoot "stubs"

$CommonArgs = @(
    "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-I$(Join-Path $Runtime 'include')",
    "-I$(Join-Path $Codec 'include')",
    "-I$(Join-Path $Profile 'include')",
    "-I$(Join-Path $ProfileRuntime 'include')",
    "-I$Control",
    "-I$Reconciler",
    "-I$Stubs"
)

$BufferExe = Join-Path $BuildDir "test_controller_event_buffer"
$RuntimeExe = Join-Path $BuildDir "test_controller_runtime"
$ProfileExe = Join-Path $BuildDir "test_controller_runtime_profile"
if ($IsWindows) {
    $BufferExe += ".exe"
    $RuntimeExe += ".exe"
    $ProfileExe += ".exe"
}

gcc @CommonArgs `
    (Join-Path $Runtime "controller_event_buffer.c") `
    (Join-Path $PSScriptRoot "test_controller_event_buffer.c") `
    -o $BufferExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $BufferExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$RuntimeSources = @(
    (Join-Path $Profile "controller_profile.c"),
    (Join-Path $ProfileRuntime "controller_profile_runtime.c"),
    (Join-Path $Runtime "flx4_map.c"),
    (Join-Path $Runtime "controller_event_buffer.c"),
    (Join-Path $Runtime "controller_runtime.c")
)

gcc @CommonArgs -DCONTROLLER_PROFILE_RUNTIME_PC_TEST `
    @RuntimeSources `
    (Join-Path $PSScriptRoot "test_controller_runtime.c") `
    -o $RuntimeExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $RuntimeExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc @CommonArgs -DCONTROLLER_PROFILE_RUNTIME_PC_TEST `
    @RuntimeSources `
    (Join-Path $PSScriptRoot "test_controller_runtime_profile.c") `
    -o $ProfileExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ProfileExe
exit $LASTEXITCODE
