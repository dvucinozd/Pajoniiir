param(
    [ValidateSet("main-deck-p4", "main-deck-jc1060")]
    [string]$Project = "main-deck-p4",
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
$SigningKeyPath = if ([System.IO.Path]::IsPathRooted($SigningKey)) { $SigningKey } else { Join-Path $RepoRoot $SigningKey }
$PublicKeyPath = if ([System.IO.Path]::IsPathRooted($PublicKey)) { $PublicKey } else { Join-Path $RepoRoot $PublicKey }
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
    $configPath = Join-Path $buildDir "config/sdkconfig.h"
    if (-not (Test-Path -LiteralPath $configPath)) { throw "Missing build configuration: $configPath" }
    $configText = Get-Content -LiteralPath $configPath -Raw
    if ($configText -match '(?m)^#define CONFIG_DDJ_OTA_(FORCE_ROLLBACK_TEST|STARTUP_TIMEOUT_TEST) 1\r?$') {
        throw "OTA fault-injection images cannot be packaged as a production release"
    }
    if ($configText -match '(?m)^#define CONFIG_(AUDIO_RECORDER_ENABLED|AUDIO_RECORDER_EXPERIMENTAL_BUILD|PAJONIIIR_SD_IDLE_WAIT) 1\r?$') {
        throw "Storage experiment images cannot be packaged as a production OTA release"
    }
    if ($configText -match '(?m)^#define CONFIG_(PAJONIIIR_DJ_OVERVIEW|USB_HOST_DWC_DMA_CAP_MEMORY_IN_PSRAM) 1\r?$') {
        throw "Preview or DMA experiment images cannot be packaged as ordinary OTA candidates"
    }
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
    if ($bytes.Length -gt 0x380000) { throw "Application exceeds the fixed 0x380000 build budget" }
    if ($bytes.Length -lt 112 -or [BitConverter]::ToUInt32($bytes, 32) -ne [Convert]::ToUInt32('ABCD5432',16)) {
        throw "Missing ESP app descriptor"
    }
    $embeddedProject = [Text.Encoding]::UTF8.GetString($bytes, 80, 32).Split([char]0)[0]
    $embeddedVersion = [Text.Encoding]::UTF8.GetString($bytes, 48, 32).Split([char]0)[0]
    if ($embeddedProject -cne $ExpectedProject -or
        $embeddedVersion -cne (ConvertTo-EspAppVersion ([string]$description.project_version))) {
        throw "Embedded app descriptor does not match build project/version"
    }

    $sourceVersion = [string]$description.project_version
    if ($sourceVersion -match '-dirty$') { throw "OTA candidate must come from a clean committed source" }
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
    -RelativeProjectDir "firmware/$Project" `
    -ExpectedProject $Project `
    -ExpectedChipId 0x0012 `
    -SlotSize 0x400000
if ($p4.SourceVersion -ne $p4.Version) {
    Write-Warning "ESP application version truncated to 31 UTF-8 bytes: '$($p4.Version)'"
}

$safeVersion = $p4.Version -replace '[^A-Za-z0-9._-]', '_'
$resolvedOutputRoot = if ([IO.Path]::IsPathRooted($OutputRoot)) { $OutputRoot } else { Join-Path $RepoRoot $OutputRoot }
$directoryName = if ($Project -eq 'main-deck-p4') { "pajoniiir-$safeVersion" } else { "pajoniiir-jc1060-$safeVersion" }
$outputDir = Join-Path $resolvedOutputRoot $directoryName
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Copy-Item -LiteralPath $p4.Source -Destination (Join-Path $outputDir $p4.File) -Force

$p4BundleFile = [System.IO.Path]::GetFileNameWithoutExtension($p4.File) + ".ddjota"
$p4BundlePath = Join-Path $outputDir $p4BundleFile

Invoke-SigningTool @(
    "bundle", "--private-key", $SigningKeyPath,
    "--target", "p4", "--chip-id", "0x0012",
    "--project", $p4.Project, "--version", $p4.Version,
    "--key-id", $KeyId, "--input", $p4.Source, "--output", $p4BundlePath
)
$p4Bundle = Get-Item -LiteralPath $p4BundlePath

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
Invoke-SigningTool @(
    "verify-file", "--public-key", $PublicKeyPath,
    "--input", $manifestPath, "--signature", $manifestSignaturePath
)

Write-Host "Signed OTA release package: $outputDir"
Write-Host "  P4 $($p4.Size) bytes sha256=$($p4.Sha256)"
Write-Host "  signing key id=$KeyId algorithm=ECDSA-P256-SHA256"
Write-Host "  upload $p4BundleFile to P4"
