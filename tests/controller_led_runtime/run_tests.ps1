$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Exe = Join-Path $BuildDir "test_controller_led_runtime"
if ($IsWindows) { $Exe += ".exe" }

$Led = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_led_runtime"
$Profile = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile"
$ProfileRuntime = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile_runtime"
$UsbHost = Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_usb_host"
$Control = Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/include"
$Stubs = Join-Path $RepoRoot "tests/controller_runtime/stubs"
$Test = Join-Path $PSScriptRoot "test_controller_led_runtime.c"

gcc -std=c11 -Wall -Wextra -Wpedantic -Werror `
    "-I$(Join-Path $Led 'include')" `
    "-I$(Join-Path $Profile 'include')" `
    "-I$(Join-Path $ProfileRuntime 'include')" `
    "-I$(Join-Path $UsbHost 'include')" `
    "-I$Control" `
    "-I$Stubs" `
    -DCONTROLLER_PROFILE_RUNTIME_PC_TEST `
    (Join-Path $Profile "controller_profile.c") `
    (Join-Path $ProfileRuntime "controller_profile_runtime.c") `
    (Join-Path $Led "flx4_led_midi.c") `
    (Join-Path $Led "controller_led_runtime.c") `
    $Test `
    -o $Exe
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $Exe
exit $LASTEXITCODE
