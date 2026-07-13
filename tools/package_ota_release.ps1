param(
    [string]$BuildName = "build_ota3",
    [string]$OutputRoot = "releases"
)

$ErrorActionPreference = "Stop"
$RepoRoot = Split-Path -Parent $PSScriptRoot

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

    [pscustomobject]@{
        Project = $ExpectedProject
        Version = [string]$description.project_version
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

if ($p4.Version -ne $s3.Version) {
    throw "P4/S3 versions differ: '$($p4.Version)' vs '$($s3.Version)'"
}

$safeVersion = $p4.Version -replace '[^A-Za-z0-9._-]', '_'
$outputDir = Join-Path (Join-Path $RepoRoot $OutputRoot) "ddj-ffl4-$safeVersion"
New-Item -ItemType Directory -Path $outputDir -Force | Out-Null

Copy-Item -LiteralPath $p4.Source -Destination (Join-Path $outputDir $p4.File) -Force
Copy-Item -LiteralPath $s3.Source -Destination (Join-Path $outputDir $s3.File) -Force

$manifest = [ordered]@{
    schema_version = 1
    release_version = $p4.Version
    targets = @(
        [ordered]@{
            target = "p4"
            project = $p4.Project
            chip_id = ("0x{0:X4}" -f $p4.ChipId)
            file = $p4.File
            size = $p4.Size
            slot_size = $p4.SlotSize
            sha256 = $p4.Sha256
        },
        [ordered]@{
            target = "s3"
            project = $s3.Project
            chip_id = ("0x{0:X4}" -f $s3.ChipId)
            file = $s3.File
            size = $s3.Size
            slot_size = $s3.SlotSize
            sha256 = $s3.Sha256
        }
    )
}
$manifestPath = Join-Path $outputDir "manifest.json"
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host "OTA release package: $outputDir"
Write-Host "  P4 $($p4.Size) bytes sha256=$($p4.Sha256)"
Write-Host "  S3 $($s3.Size) bytes sha256=$($s3.Sha256)"
