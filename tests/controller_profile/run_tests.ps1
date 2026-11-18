$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
& python (Join-Path $PSScriptRoot "test_capture_midi.py")
if ($LASTEXITCODE -ne 0) { throw "MIDI capture integrity tests failed" }
& python (Join-Path $PSScriptRoot "test_output_validation.py")
if ($LASTEXITCODE -ne 0) { throw "Profile output validation tests failed" }
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Components = Join-Path $RepoRoot "firmware/p4-core/components"
$Includes = @("controller_profile", "controller_runtime", "controller_led_runtime", "controller_usb_host", "control_link") |
    ForEach-Object { "-I$(Join-Path $Components "$_/include")" }
$Common = @("-std=c11", "-Wall", "-Wextra", "-Werror", "-I$(Join-Path $RepoRoot 'tests/controller_runtime/stubs')") + $Includes
$ProfileSource = Join-Path $Components "controller_profile/controller_profile.c"
Push-Location $PSScriptRoot
try {
    foreach ($name in @("test_controller_profile", "test_controller_profile_parity")) {
        $exe = Join-Path $BuildDir $name
        if ($env:OS -eq "Windows_NT") { $exe += ".exe" }
        $sources = @("$name.c", $ProfileSource)
        if ($name.EndsWith("_parity")) {
            $sources += Join-Path $Components "controller_runtime/flx4_map.c"
            $sources += Join-Path $Components "controller_led_runtime/flx4_led_midi.c"
        }
        & gcc @Common @sources -o $exe
        if ($LASTEXITCODE -ne 0) { throw "Build failed: $name" }
        & $exe
        if ($LASTEXITCODE -ne 0) { throw "Test failed: $name" }
    }
    $FullExe = Join-Path $BuildDir "test_s2_full.exe"
    $Manager = Join-Path $Components "controller_profile_manager"
    & gcc @Common -DCONTROLLER_PROFILE_MANAGER_PC_TEST "-I$Manager/include" test_s2_full.c $ProfileSource `
        (Join-Path $Manager "controller_profile_manager.c") -o $FullExe
    if ($LASTEXITCODE -ne 0) { throw "S2 full profile build failed" }
    & $FullExe
    if ($LASTEXITCODE -ne 0) { throw "S2 full profile test failed" }
    $Ddj400Exe = Join-Path $BuildDir "test_ddj400.exe"
    & gcc @Common -DCONTROLLER_PROFILE_MANAGER_PC_TEST "-I$Manager/include" test_ddj400.c $ProfileSource `
        (Join-Path $Manager "controller_profile_manager.c") -o $Ddj400Exe
    if ($LASTEXITCODE -ne 0) { throw "DDJ-400 profile build failed" }
    & $Ddj400Exe
    if ($LASTEXITCODE -ne 0) { throw "DDJ-400 profile test failed" }
    $NumarkExe = Join-Path $BuildDir "test_numark_dj2go2_touch.exe"
    & gcc @Common -DCONTROLLER_PROFILE_MANAGER_PC_TEST "-I$Manager/include" test_numark_dj2go2_touch.c $ProfileSource `
        (Join-Path $Manager "controller_profile_manager.c") -o $NumarkExe
    if ($LASTEXITCODE -ne 0) { throw "Numark profile build failed" }
    & $NumarkExe
    if ($LASTEXITCODE -ne 0) { throw "Numark profile test failed" }
    $HerculesExe = Join-Path $BuildDir "test_hercules_native.exe"
    $Runtime = Join-Path $Components "controller_profile_runtime"
    & gcc @Common -DCONTROLLER_PROFILE_MANAGER_PC_TEST -DCONTROLLER_PROFILE_RUNTIME_PC_TEST `
        "-I$Manager/include" "-I$Runtime/include" test_hercules_native.c $ProfileSource `
        (Join-Path $Manager "controller_profile_manager.c") `
        (Join-Path $Runtime "controller_profile_runtime.c") -o $HerculesExe
    if ($LASTEXITCODE -ne 0) { throw "Hercules native profile build failed" }
    & $HerculesExe
    if ($LASTEXITCODE -ne 0) { throw "Hercules native profile test failed" }
    $Sb3Exe = Join-Path $BuildDir "test_ddj_sb3.exe"
    & gcc @Common -DCONTROLLER_PROFILE_MANAGER_PC_TEST -DCONTROLLER_PROFILE_RUNTIME_PC_TEST `
        "-I$Manager/include" "-I$Runtime/include" test_ddj_sb3.c $ProfileSource `
        (Join-Path $Manager "controller_profile_manager.c") `
        (Join-Path $Runtime "controller_profile_runtime.c") -o $Sb3Exe
    if ($LASTEXITCODE -ne 0) { throw "DDJ-SB3 profile build failed" }
    & $Sb3Exe
    if ($LASTEXITCODE -ne 0) { throw "DDJ-SB3 profile test failed" }
} finally { Pop-Location }
exit 0
