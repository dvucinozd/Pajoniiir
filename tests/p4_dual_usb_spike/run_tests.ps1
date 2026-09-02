param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"
$Here = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = Split-Path -Parent (Split-Path -Parent $Here)
$Gcc = Get-Command gcc -ErrorAction Stop
$Output = Join-Path $Here "test_usb_midi_probe.exe"

try {
    & $Gcc.Source `
        -std=c11 -Wall -Wextra -Wpedantic -Werror `
        "-I$RepoRoot/firmware/p4-dual-usb-spike/main" `
        "$RepoRoot/firmware/p4-dual-usb-spike/main/usb_midi_probe.c" `
        "$Here/test_usb_midi_probe.c" `
        -o $Output
    if ($LASTEXITCODE -ne 0) {
        throw "p4 dual USB spike parser build failed with exit code $LASTEXITCODE"
    }

    $outputLines = & $Output 2>&1
    $outputLines | ForEach-Object { Write-Host $_ }
    if ($LASTEXITCODE -ne 0) {
        throw "p4 dual USB spike parser tests failed with exit code $LASTEXITCODE"
    }
    $match = $outputLines | Select-String -Pattern '^TESTS_RUN=(\d+)$' | Select-Object -Last 1
    if (-not $match) {
        throw "test binary did not report TESTS_RUN"
    }
    $ran = [int]$match.Matches[0].Groups[1].Value
    if ($ran -lt 32) {
        throw "test binary ran $ran assertions; expected at least 32"
    }
} finally {
    if (-not $KeepArtifacts) {
        Remove-Item -Force $Output -ErrorAction SilentlyContinue
    }
}
