param(
    [string]$BuildName = "build_signed",
    [string]$OutputRoot = "releases",
    [string]$SigningKey = "keys/ota_signing_private.pem",
    [string]$PublicKey = "firmware/common/ota_manifest/keys/ddj_ota_release_public.der",
    [string]$KeyId = "rel-001"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot
if ($KeyId -ne "rel-001") {
    throw "Firmware currently trusts only OTA signing key ID 'rel-001'"
}
$SigningTool = Join-Path $PSScriptRoot "ota_signing.py"
$ReleaseHelpers = Join-Path $PSScriptRoot "OtaReleaseHelpers.psm1"
Import-Module $ReleaseHelpers -Force
$Python = Resolve-OtaSigningPython
$SigningKeyPath = Join-Path $RepoRoot $SigningKey
$PublicKeyPath = Join-Path $RepoRoot $PublicKey
if (-not (Test-Path -LiteralPath $SigningKeyPath)) {
    throw "Missing private signing key: $SigningKeyPath. Generate/provision it outside git before packaging."
}
if (-not (Test-Path -LiteralPath $PublicKeyPath)) {
    throw "Missing firmware public verification key: $PublicKeyPath"
}

function Invoke-SigningTool {
    param([string[]]$Arguments)
    & $Python $SigningTool @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "OTA signing tool failed with exit code $LASTEXITCODE"
    }
}

function Read-TargetBuild {
    param(
        [string]$RelativeProjectDir,
        [string]$ExpectedProject,
        [int]$ExpectedChipId,
        [long]$SlotSize
    )

    $buildDir = Join-Path (Join-Path $RepoRoot $RelativeProjectDir) $BuildName
    $descriptionPath = Join-Path $buildDir "project_description.json"
    if (-not (Test-Path -LiteralPath $descriptionPath)) {
        throw "Missing build metadata: $descriptionPath"
    }
    $description = Get-Content -LiteralPath $descriptionPath -Raw | ConvertFrom-Json
    if ($description.project_name -ne $ExpectedProject) {
        throw "Wrong project in ${descriptionPath}: $($description.project_name)"
    }

    $binaryPath = Join-Path $buildDir $description.app_bin
    if (-not (Test-Path -LiteralPath $binaryPath)) {
        throw "Missing application binary: $binaryPath"
    }
    $bytes = [System.IO.File]::ReadAllBytes($binaryPath)
    if ($bytes.Length -lt 24 -or $bytes[0] -ne 0xE9) {
        throw "$ExpectedProject is not an ESP application image"
    }
    $chipId = [int]$bytes[12] -bor ([int]$bytes[13] -shl 8)
    if ($chipId -ne $ExpectedChipId) {
        throw ("Wrong chip for {0}: expected 0x{1:X4}, got 0x{2:X4}" -f
               $ExpectedProject, $ExpectedChipId, $chipId)
    }
    if ($bytes.Length -gt $SlotSize) {
        throw ("{0} image is {1} bytes, beyond its {2}-byte OTA slot" -f
               $ExpectedProject, $bytes.Length, $SlotSize)
    }

    $sourceVersion = [string]$description.project_version
    [pscustomobject]@{
        Project = $ExpectedProject
        SourceVersion = $sourceVersion
        Version = ConvertTo-EspAppVersion $sourceVersion
        Source = $binaryPath
        File = [string]$description.app_bin
        ChipId = $chipId
        Size = [long]$bytes.Length
        SlotSize = $SlotSize
        Sha256 = (Get-FileHash -LiteralPath $binaryPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$p4 = Read-TargetBuild `
    -RelativeProjectDir "firmware/main-deck-p4" `
    -ExpectedProject "main-deck-p4" `
    -ExpectedChipId 0x0012 `
    -SlotSize 0x400000
$s3 = Read-TargetBuild `
    -RelativeProjectDir "firmware/control-board-s3" `
    -ExpectedProject "control-board-s3" `
    -ExpectedChipId 0x0009 `
    -SlotSize 0x1e0000

if ($p4.SourceVersion -ne $s3.SourceVersion) {
    throw "P4/S3 versions differ: '$($p4.SourceVersion)' vs '$($s3.SourceVersion)'"
}
if ($p4.SourceVersion -ne $p4.Version) {
    Write-Warning "ESP application version truncated to 31 UTF-8 bytes: '$($p4.Version)'"
}

$safeVersion = $p4.Version -replace '[^A-Za-z0-9._-]', '_'
$outputDir = Join-Path (Join-Path $RepoRoot $OutputRoot) "pajoniiir-$safeVersion"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Copy-Item -LiteralPath $p4.Source -Destination (Join-Path $outputDir $p4.File) -Force
Copy-Item -LiteralPath $s3.Source -Destination (Join-Path $outputDir $s3.File) -Force

$p4BundleFile = [System.IO.Path]::GetFileNameWithoutExtension($p4.File) + ".ddjota"
$s3BundleFile = [System.IO.Path]::GetFileNameWithoutExtension($s3.File) + ".ddjota"
$p4BundlePath = Join-Path $outputDir $p4BundleFile
$s3BundlePath = Join-Path $outputDir $s3BundleFile

Invoke-SigningTool @(
    "bundle", "--private-key", $SigningKeyPath,
    "--target", "p4", "--chip-id", "0x0012",
    "--project", $p4.Project, "--version", $p4.Version,
    "--key-id", $KeyId, "--input", $p4.Source, "--output", $p4BundlePath
)
Invoke-SigningTool @(
    "bundle", "--private-key", $SigningKeyPath,
    "--target", "s3", "--chip-id", "0x0009",
    "--project", $s3.Project, "--version", $s3.Version,
    "--key-id", $KeyId, "--input", $s3.Source, "--output", $s3BundlePath
)

$p4Bundle = Get-Item -LiteralPath $p4BundlePath
$s3Bundle = Get-Item -LiteralPath $s3BundlePath

$manifest = [ordered]@{
    schema_version = 2
    release_version = $p4.Version
    signing = [ordered]@{
        algorithm = "ecdsa-p256-sha256"
        key_id = $KeyId
        signature_file = "manifest.sig"
    }
    targets = @(
        [ordered]@{
            target = "p4"
            project = $p4.Project
            chip_id = ("0x{0:X4}" -f $p4.ChipId)
            file = $p4.File
            ota_bundle = $p4BundleFile
            size = $p4.Size
            bundle_size = $p4Bundle.Length
            slot_size = $p4.SlotSize
            sha256 = $p4.Sha256
            bundle_sha256 = (Get-FileHash -LiteralPath $p4BundlePath -Algorithm SHA256).Hash.ToLowerInvariant()
        },
        [ordered]@{
            target = "s3"
            project = $s3.Project
            chip_id = ("0x{0:X4}" -f $s3.ChipId)
            file = $s3.File
            ota_bundle = $s3BundleFile
            size = $s3.Size
            bundle_size = $s3Bundle.Length
            slot_size = $s3.SlotSize
            sha256 = $s3.Sha256
            bundle_sha256 = (Get-FileHash -LiteralPath $s3BundlePath -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    )
}
$manifestPath = Join-Path $outputDir "manifest.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8
$manifestSignaturePath = Join-Path $outputDir "manifest.sig"
Invoke-SigningTool @(
    "sign-file", "--private-key", $SigningKeyPath,
    "--input", $manifestPath, "--output", $manifestSignaturePath
)

Invoke-SigningTool @("verify-bundle", "--public-key", $PublicKeyPath, "--input", $p4BundlePath)
Invoke-SigningTool @("verify-bundle", "--public-key", $PublicKeyPath, "--input", $s3BundlePath)
Invoke-SigningTool @(
    "verify-file", "--public-key", $PublicKeyPath,
    "--input", $manifestPath, "--signature", $manifestSignaturePath
)

Write-Host "Signed OTA release package: $outputDir"
Write-Host "  P4 $($p4.Size) bytes sha256=$($p4.Sha256)"
Write-Host "  S3 $($s3.Size) bytes sha256=$($s3.Sha256)"
Write-Host "  signing key id=$KeyId algorithm=ECDSA-P256-SHA256"
Write-Host "  upload $p4BundleFile to P4 and $s3BundleFile to S3"
