$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
Import-Module (Join-Path $RepoRoot "tools/OtaReleaseHelpers.psm1") -Force

function Assert-Equal {
    param([string]$Expected, [string]$Actual, [string]$Name)
    if ($Expected -cne $Actual) {
        throw "$Name expected '$Expected', got '$Actual'"
    }
}

Assert-Equal "RC2-test" (ConvertTo-EspAppVersion "RC2-test") "short version"
$ascii31 = "1234567890123456789012345678901"
Assert-Equal $ascii31 (ConvertTo-EspAppVersion $ascii31) "31-byte version"
Assert-Equal $ascii31 (ConvertTo-EspAppVersion ($ascii31 + "X")) "32-byte truncation"
$euro = [char]0x20AC
Assert-Equal ("a" * 29) (ConvertTo-EspAppVersion (("a" * 29) + $euro)) `
    "UTF-8 boundary truncation"
Assert-Equal (("a" * 28) + $euro) `
    (ConvertTo-EspAppVersion (("a" * 28) + $euro + "X")) "UTF-8 exact fit"

$emptyRejected = $false
try {
    ConvertTo-EspAppVersion "" | Out-Null
} catch {
    $emptyRejected = $true
}
if (-not $emptyRejected) {
    throw "empty version was accepted"
}

$Python = Resolve-OtaSigningPython
$tempRoot = Join-Path ([System.IO.Path]::GetTempPath()) `
    ("pajoniiir-ota-publish-test-" + [guid]::NewGuid().ToString("N"))
try {
    $releaseDir = Join-Path $tempRoot "directory-name-must-not-be-the-release"
    New-Item -ItemType Directory -Path $releaseDir -Force | Out-Null
    $privateKey = Join-Path $tempRoot "private.pem"
    $publicKey = Join-Path $tempRoot "public.der"
    $image = Join-Path $tempRoot "image.bin"
    $bundle = Join-Path $releaseDir "main-deck-p4.ddjota"
    [System.IO.File]::WriteAllBytes($image, [byte[]](1..24))

    & $Python (Join-Path $RepoRoot "tools/ota_signing.py") generate-key `
        --private $privateKey --public $publicKey
    if ($LASTEXITCODE -ne 0) { throw "test key generation failed" }
    & $Python (Join-Path $RepoRoot "tools/ota_signing.py") bundle `
        --private-key $privateKey --target p4 --chip-id 0x0012 `
        --project main-deck-p4 --version RC9-7-gabcdef0 `
        --input $image --output $bundle
    if ($LASTEXITCODE -ne 0) { throw "test bundle creation failed" }

    & (Join-Path $RepoRoot "tools/publish_ota_release.ps1") `
        -ReleaseDir $releaseDir -PublicKey $publicKey -WriteToReleaseDir | Out-Null
    $latest = Get-Content -LiteralPath (Join-Path $releaseDir "latest.json") `
        -Raw | ConvertFrom-Json
    Assert-Equal "RC9-7-gabcdef0" ([string]$latest.release) `
        "publisher derives release from signed bundle"
} finally {
    if (Test-Path -LiteralPath $tempRoot) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force
    }
}

Write-Host "OTA release helper tests passed."
