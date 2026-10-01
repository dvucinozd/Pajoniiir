$ErrorActionPreference = "Stop"
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
$BuildDir = Join-Path $PSScriptRoot "build"
New-Item -ItemType Directory -Force -Path $BuildDir | Out-Null
$Components = Join-Path $RepoRoot "firmware/main-deck-p4/components"
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
} finally { Pop-Location }
exit 0
