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
Assert-Equal ("a" * 29) (ConvertTo-EspAppVersion (("a" * 29) + "€")) `
    "UTF-8 boundary truncation"
Assert-Equal (("a" * 28) + "€") `
    (ConvertTo-EspAppVersion (("a" * 28) + "€X")) "UTF-8 exact fit"

$emptyRejected = $false
try {
    ConvertTo-EspAppVersion "" | Out-Null
} catch {
    $emptyRejected = $true
}
if (-not $emptyRejected) {
    throw "empty version was accepted"
}

Write-Host "OTA release helper tests passed."
