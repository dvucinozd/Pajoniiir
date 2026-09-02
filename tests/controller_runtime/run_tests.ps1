$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null

$Runtime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_runtime"
$Codec = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Profile = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile"
$ProfileRuntime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile_runtime"
$HostManager = Join-Path $RepoRoot "firmware/main-deck-p4/components/usb_host_manager"
$Control = Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/include"
$Reconciler = Join-Path $RepoRoot "firmware/common/control_state_reconciler/include"
$Stubs = Join-Path $PSScriptRoot "stubs"

$CommonArgs = @(
    "-std=c11", "-Wall", "-Wextra", "-Wpedantic", "-Werror",
    "-I$(Join-Path $Runtime 'include')",
    "-I$(Join-Path $Codec 'include')",
    "-I$(Join-Path $Profile 'include')",
    "-I$(Join-Path $ProfileRuntime 'include')",
    "-I$(Join-Path $HostManager 'include')",
    "-I$Control",
    "-I$Reconciler",
    "-I$Stubs"
)

$BufferExe = Join-Path $BuildDir "test_controller_event_buffer"
$RuntimeExe = Join-Path $BuildDir "test_controller_runtime"
$ProfileExe = Join-Path $BuildDir "test_controller_runtime_profile"
$RecoveryExe = Join-Path $BuildDir "test_usb_host_recovery_arbiter"
$ControllerRecoveryExe = Join-Path $BuildDir "test_controller_usb_recovery_gate"
$TopologyExe = Join-Path $BuildDir "test_usb_host_topology"
if ($IsWindows) {
    $BufferExe += ".exe"
    $RuntimeExe += ".exe"
    $ProfileExe += ".exe"
    $RecoveryExe += ".exe"
    $ControllerRecoveryExe += ".exe"
    $TopologyExe += ".exe"
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
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc @CommonArgs `
    (Join-Path $HostManager "usb_host_recovery_arbiter.c") `
    (Join-Path $PSScriptRoot "test_usb_host_recovery_arbiter.c") `
    -o $RecoveryExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $RecoveryExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc @CommonArgs `
    (Join-Path $Codec "controller_usb_recovery_gate.c") `
    (Join-Path $PSScriptRoot "test_controller_usb_recovery_gate.c") `
    -o $ControllerRecoveryExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ControllerRecoveryExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

gcc @CommonArgs `
    (Join-Path $HostManager "usb_host_topology.c") `
    (Join-Path $PSScriptRoot "test_usb_host_topology.c") `
    -o $TopologyExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $TopologyExe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

exit 0
