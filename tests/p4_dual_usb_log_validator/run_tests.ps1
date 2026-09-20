$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
Push-Location $RepoRoot
try {
    $Python = Get-Command python -ErrorAction SilentlyContinue
    if (-not $Python) {
        $Python = Get-Command python3 -ErrorAction Stop
    }
    & $Python.Source -m unittest discover `
        -s tests/p4_dual_usb_log_validator `
        -p "test_*.py" `
        -v
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
