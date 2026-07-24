# Generate the pull-OTA channel document for a packaged release, and lay out
# exactly what has to be uploaded to the VPS.
#
# The channel document is discovery only. Authenticity stays with the .ddjota's
# own ECDSA-P256 manifest, which the device verifies before flashing, so a
# tampered latest.json can misdirect a download but cannot install unsigned
# firmware. size/sha256 are carried so a truncated transfer is rejected before
# the bundle parser is entered.
param(
    [Parameter(Mandatory = $true)][string]$ReleaseDir,
    [string]$BaseUrl = "https://pajoniiir.zadar.click/ota",
    [switch]$WriteToReleaseDir
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path -LiteralPath $ReleaseDir)) {
    throw "No such release directory: $ReleaseDir"
}
$release = Split-Path -Leaf $ReleaseDir
# Release directories are named ddj-ffl4-<version>; the device compares the
# version it is running against the "release" field, so they must match exactly.
$version = $release -replace '^ddj-ffl4-', ''

$bundle = Join-Path $ReleaseDir "main-deck-p4.ddjota"
if (-not (Test-Path -LiteralPath $bundle)) {
    throw "Missing P4 bundle: $bundle"
}

$bytes = [System.IO.File]::ReadAllBytes($bundle)
$sha = [System.BitConverter]::ToString(
    [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
).Replace("-", "").ToLowerInvariant()

$doc = [ordered]@{
    schema_version = 1
    release        = $version
    p4             = [ordered]@{
        url    = "$version/main-deck-p4.ddjota"
        size   = $bytes.Length
        sha256 = $sha
    }
}
$json = $doc | ConvertTo-Json -Depth 4

if ($WriteToReleaseDir) {
    $out = Join-Path $ReleaseDir "latest.json"
    [System.IO.File]::WriteAllText($out, $json)
    Write-Output "wrote $out"
}

Write-Output ""
Write-Output "--- latest.json ---"
Write-Output $json
Write-Output ""
Write-Output "--- upload to the VPS so these resolve ---"
Write-Output "  $BaseUrl/latest.json"
Write-Output "  $BaseUrl/$version/main-deck-p4.ddjota   ($($bytes.Length) bytes)"
Write-Output ""
Write-Output "sha256 $sha"
