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
    [ValidateSet("main-deck-p4", "main-deck-jc1060", "main-deck-m3")]
    [string]$Project = "main-deck-p4",
    [string]$BaseUrl,
    [string]$PublicKey = "firmware/common/ota_manifest/keys/ddj_ota_release_public.der",
    [switch]$WriteToReleaseDir
)

$ErrorActionPreference = "Stop"
$ReleaseHelpers = Join-Path $PSScriptRoot "OtaReleaseHelpers.psm1"
Import-Module $ReleaseHelpers -Force

if (-not (Test-Path -LiteralPath $ReleaseDir)) {
    throw "No such release directory: $ReleaseDir"
}
$RepoRoot = Split-Path -Parent $PSScriptRoot
if (-not $BaseUrl) {
    $BaseUrl = switch ($Project) {
        'main-deck-p4' { 'https://ota.pajoniiir.eu' }
        'main-deck-jc1060' { 'https://ota.pajoniiir.eu/jc1060' }
        'main-deck-m3' { 'https://ota.pajoniiir.eu/m3' }
    }
}
$BaseUrl = $BaseUrl.TrimEnd('/')
if ($Project -eq 'main-deck-jc1060' -and $BaseUrl -eq 'https://ota.pajoniiir.eu') {
    throw 'JC1060 must use its separate OTA channel'
}

if ($Project -eq 'main-deck-m3' -and ([uri]$BaseUrl).AbsolutePath.TrimEnd('/') -ne '/m3') {
    throw 'M3 must use its separate OTA channel /m3'
}
if ($Project -ne 'main-deck-m3' -and ([uri]$BaseUrl).Host -eq 'ota.pajoniiir.eu' -and
    ([uri]$BaseUrl).AbsolutePath.TrimEnd('/') -eq '/m3') { throw 'The M3 OTA channel cannot serve another board' }
if ($Project -eq 'main-deck-p4' -and $BaseUrl -eq 'https://ota.pajoniiir.eu/jc1060') {
    throw 'JC4880 cannot use the separate OTA channel for JC1060'
}
$bundleFile = "$Project.ddjota"
$bundle = Join-Path $ReleaseDir $bundleFile
if (-not (Test-Path -LiteralPath $bundle)) {
    throw "Missing P4 bundle: $bundle"
}

$PublicKeyPath = if ([System.IO.Path]::IsPathRooted($PublicKey)) {
    $PublicKey
} else {
    Join-Path $RepoRoot $PublicKey
}
if (-not (Test-Path -LiteralPath $PublicKeyPath)) {
    throw "Missing firmware public verification key: $PublicKeyPath"
}
$Python = Resolve-OtaSigningPython
$SigningTool = Join-Path $PSScriptRoot "ota_signing.py"
$metadataLines = & $Python $SigningTool verify-bundle `
    --public-key $PublicKeyPath --input $bundle
if ($LASTEXITCODE -ne 0) {
    throw "P4 bundle signature/manifest verification failed"
}
$metadata = @{}
foreach ($line in $metadataLines) {
    if ($line -match '^([^=]+)=(.*)$') {
        $metadata[$Matches[1]] = $Matches[2]
    }
}
if ($metadata["target"] -cne "p4" -or
    $metadata["project"] -cne $Project -or
    [string]::IsNullOrEmpty($metadata["version"])) {
    throw "Verified bundle metadata is not a versioned $Project image"
}
$version = [string]$metadata["version"]
if ($Project -eq 'main-deck-m3' -and $version -notmatch '^M3-[0-9]+(-[0-9]+-g[0-9a-f]{7,})*$') {
    throw 'M3 development versions cannot generate a public pull channel'
}

$bytes = [System.IO.File]::ReadAllBytes($bundle)
$sha = [System.BitConverter]::ToString(
    [System.Security.Cryptography.SHA256]::Create().ComputeHash($bytes)
).Replace("-", "").ToLowerInvariant()

$doc = [ordered]@{
    schema_version = 1
    release        = $version
    p4             = [ordered]@{
        url    = "$version/$bundleFile"
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
Write-Output "--- publish so these HTTPS URLs resolve ---"
Write-Output "  $BaseUrl/latest.json"
Write-Output "  $BaseUrl/$version/$bundleFile   ($($bytes.Length) bytes)"
Write-Output ""
Write-Output "sha256 $sha"
