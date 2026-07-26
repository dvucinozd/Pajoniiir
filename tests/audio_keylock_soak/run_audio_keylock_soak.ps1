param(
    [ValidateRange(1, 3600)]
    [int]$VirtualSeconds = 300,
    [switch]$KeepArtifact
)

$ErrorActionPreference = "Stop"
$TestDir = $PSScriptRoot
$Target = Join-Path $TestDir "test_audio_keylock_soak.exe"

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
    $env:Path = "$GccDirectory;$env:Path"
}

Push-Location $TestDir
try {
    & $GccPath `
        -O2 -Wall -Wextra -Wpedantic -Werror=implicit-function-declaration `
        -std=c99 `
        "-I../../firmware/main-deck-p4/components/audio_engine/include" `
        -o $Target `
        "test_audio_keylock_soak.c" `
        "../../firmware/main-deck-p4/components/audio_engine/audio_keylock.c" `
        -lm
    if ($LASTEXITCODE -ne 0) {
        throw "audio key-lock soak build failed with exit code $LASTEXITCODE"
    }

    & $Target $VirtualSeconds
    if ($LASTEXITCODE -ne 0) {
        throw "audio key-lock soak failed with exit code $LASTEXITCODE"
    }
} finally {
    Pop-Location
    $env:Path = $OriginalPath
    if (-not $KeepArtifact -and (Test-Path -LiteralPath $Target)) {
        Remove-Item -LiteralPath $Target -Force
    }
}
