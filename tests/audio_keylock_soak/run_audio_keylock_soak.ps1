param(
    [ValidateRange(1, 3600)]
    [int]$VirtualSeconds = 300,
    [switch]$KeepArtifact,
    [switch]$M3Policy,
    [switch]$Wsola2
)

$ErrorActionPreference = "Stop"
$TestDir = $PSScriptRoot
$Target = Join-Path $TestDir "test_audio_keylock_soak.exe"
$QualityTarget = $null
if ($Wsola2) {
    if ($M3Policy) { throw "WSOLA2 is a JC1060 experiment" }
    $Target = Join-Path $TestDir "test_audio_keylock_wsola2_soak.exe"
}

$Gcc = Get-Command gcc -ErrorAction SilentlyContinue
if ($Gcc) {
    $GccPath = $Gcc.Source
} else {
    $FallbackGcc = "C:\msys64\ucrt64\bin\gcc.exe"
    if (Test-Path -LiteralPath $FallbackGcc) {
        $GccPath = $FallbackGcc
    } else {
        throw "gcc not found; install MSYS2 UCRT64 or add it to PATH"
    }
}

$OriginalPath = $env:Path
$GccDirectory = Split-Path -Parent $GccPath
if ($env:Path -notlike "*$GccDirectory*") {
    $env:Path = "$env:Path;$GccDirectory"
}

Push-Location $TestDir
try {
    $GccArgs = @("-O2", "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration",
        "-std=c99", "-I../../firmware/p4-core/components/audio_engine/include",
        "../../firmware/p4-core/components/audio_engine/audio_keylock.c", "-lm")
    if ($M3Policy) {
        $GccArgs += @("-DM3_DSP_POLICY_TEST", "-DAUDIO_ANTIALIAS_CACHE=1")
    } else {
        $GccArgs += "-DAUDIO_ANTIALIAS_CACHE=0"
    }
    if ($Wsola2) {
        $GccArgs += @("-DCONFIG_AUDIO_KEYLOCK_WSOLA2=1",
            "../../firmware/p4-core/components/audio_engine/audio_keylock_wsola2.c",
            "../../firmware/p4-core/components/audio_engine/wsola2_resampler.c")
    }
    & $GccPath test_audio_keylock_soak.c @GccArgs -o $Target
    if ($LASTEXITCODE -ne 0) {
        throw "audio key-lock soak build failed with exit code $LASTEXITCODE"
    }

    & $Target $VirtualSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "audio key-lock soak failed with exit code $LASTEXITCODE"
    }
    if ($Wsola2) {
        $QualityTarget = Join-Path $TestDir "test_wsola2_quality.exe"
        & $GccPath test_wsola2_quality.c @GccArgs -o $QualityTarget
        if ($LASTEXITCODE -ne 0) { throw "WSOLA2 quality test compilation failed" }
        & $QualityTarget
        if ($LASTEXITCODE -ne 0) { throw "WSOLA2 quality test failed" }
    }
} finally {
    Pop-Location
    $env:Path = $OriginalPath
    if ($QualityTarget -and -not $KeepArtifact -and (Test-Path -LiteralPath $QualityTarget)) {
        Remove-Item -LiteralPath $QualityTarget -Force
    }
    if (-not $KeepArtifact -and (Test-Path -LiteralPath $Target)) {
        Remove-Item -LiteralPath $Target -Force
    }
}
