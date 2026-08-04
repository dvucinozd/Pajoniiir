$ErrorActionPreference = "Stop"

$RepoRoot = Resolve-Path (Join-Path $PSScriptRoot "../..")
Push-Location $RepoRoot
try {
    python3 -m unittest discover `
        -s tests/p4_dual_usb_log_validator `
        -p "test_*.py" `
        -v
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
} finally {
    Pop-Location
}
