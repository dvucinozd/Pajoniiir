[CmdletBinding()]
param(
    [switch]$UpdateBaselines,
    [switch]$KeepArtifacts,
    [string]$LvglPath
)

$ErrorActionPreference = 'Stop'

$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
$CacheRoot = Join-Path $RepoRoot '.cache\ui_simulator'
$BuildDir = Join-Path $CacheRoot 'build'
$OutputDir = Join-Path $CacheRoot 'screenshots'
$ManifestPath = Join-Path $ScriptDir 'baselines.json'
$LvglCommit = '263ae5e13dec1e525109aed556cec1bbdfdecd5a'

function Resolve-Tool {
    param(
        [string]$Name,
        [string[]]$Fallbacks
    )
    $cmd = Get-Command $Name -ErrorAction SilentlyContinue
    if ($cmd) {
        return $cmd.Source
    }
    foreach ($fallback in $Fallbacks) {
        if (Test-Path -LiteralPath $fallback -PathType Leaf) {
            return $fallback
        }
    }
    throw "Required tool '$Name' was not found."
}

$Git = Resolve-Tool 'git' @()
$CMake = Resolve-Tool 'cmake' @(
    'C:\Espressif\tools\cmake\3.30.2\bin\cmake.exe'
)
$Ninja = Resolve-Tool 'ninja' @(
    'C:\Espressif\tools\ninja\1.12.1\ninja.exe'
)
$Gcc = Resolve-Tool 'gcc' @(
    'C:\msys64\ucrt64\bin\gcc.exe'
)
$Gxx = Resolve-Tool 'g++' @(
    'C:\msys64\ucrt64\bin\g++.exe'
)

if ($LvglPath) {
    $ResolvedLvgl = (Resolve-Path -LiteralPath $LvglPath).Path
} else {
    $ResolvedLvgl = Join-Path $CacheRoot "lvgl-$($LvglCommit.Substring(0, 12))"
    if (-not (Test-Path -LiteralPath (Join-Path $ResolvedLvgl '.git'))) {
        New-Item -ItemType Directory -Force -Path $CacheRoot | Out-Null
        & $Git clone --filter=blob:none --no-checkout https://github.com/lvgl/lvgl.git $ResolvedLvgl
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to clone the pinned LVGL dependency.'
        }
        & $Git -C $ResolvedLvgl fetch --depth 1 origin $LvglCommit
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to fetch the pinned LVGL commit.'
        }
        & $Git -C $ResolvedLvgl checkout --detach $LvglCommit
        if ($LASTEXITCODE -ne 0) {
            throw 'Failed to check out the pinned LVGL commit.'
        }
    }
}

$ActualLvglCommit = (& $Git -C $ResolvedLvgl rev-parse HEAD).Trim()
if ($ActualLvglCommit -ne $LvglCommit) {
    throw "LVGL checkout mismatch: expected $LvglCommit, found $ActualLvglCommit"
}
$LvglChanges = @(& $Git -C $ResolvedLvgl status --porcelain)
if ($LASTEXITCODE -ne 0) {
    throw 'Failed to inspect the LVGL checkout.'
}
if ($LvglChanges.Count -ne 0) {
    throw "LVGL checkout has local changes and is not reproducible: $ResolvedLvgl"
}

New-Item -ItemType Directory -Force -Path $BuildDir, $OutputDir | Out-Null

$OldPath = $env:PATH
$env:PATH = "$(Split-Path -Parent $Gcc);$(Split-Path -Parent $Ninja);$env:PATH"
try {
    & $CMake -S $ScriptDir -B $BuildDir -G Ninja `
        "-DLVGL_DIR=$($ResolvedLvgl -replace '\\','/')" `
        "-DCMAKE_MAKE_PROGRAM=$($Ninja -replace '\\','/')" `
        "-DCMAKE_C_COMPILER=$($Gcc -replace '\\','/')" `
        "-DCMAKE_CXX_COMPILER=$($Gxx -replace '\\','/')" `
        '-DCMAKE_BUILD_TYPE=Release'
    if ($LASTEXITCODE -ne 0) {
        throw 'UI simulator CMake configure failed.'
    }

    & $CMake --build $BuildDir --target ui_simulator_e2e
    if ($LASTEXITCODE -ne 0) {
        throw 'UI simulator build failed.'
    }

    $Executable = Join-Path $BuildDir 'ui_simulator_e2e.exe'
    if (-not (Test-Path -LiteralPath $Executable)) {
        $Executable = Join-Path $BuildDir 'ui_simulator_e2e'
    }
    & $Executable ($OutputDir -replace '\\','/')
    if ($LASTEXITCODE -ne 0) {
        throw 'UI simulator E2E scenario failed.'
    }
} finally {
    $env:PATH = $OldPath
}

$Captures = @(
    'overview_deck1',
    'overview_deck2',
    'library',
    'hot_cues',
    'settings',
    'screensaver',
    'settings_restored'
)

$Actual = [ordered]@{}
foreach ($name in $Captures) {
    $path = Join-Path $OutputDir "$name.ppm"
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Missing screenshot: $path"
    }
    $Actual[$name] = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash.ToLowerInvariant()
}

if ($UpdateBaselines) {
    $Manifest = [ordered]@{
        schema = 1
        width = 800
        height = 480
        lvgl_commit = $LvglCommit
        captures = $Actual
    }
    $Manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $ManifestPath -Encoding utf8
    Write-Host "Updated UI screenshot baselines: $ManifestPath"
} else {
    if (-not (Test-Path -LiteralPath $ManifestPath)) {
        throw "Baseline manifest is missing. Review captures, then run with -UpdateBaselines."
    }
    $Expected = Get-Content -Raw -LiteralPath $ManifestPath | ConvertFrom-Json
    if ($Expected.schema -ne 1 -or
        $Expected.lvgl_commit -ne $LvglCommit -or
        $Expected.width -ne 800 -or $Expected.height -ne 480) {
        throw 'Baseline metadata does not match the pinned simulator configuration.'
    }
    $Mismatch = $false
    foreach ($name in $Captures) {
        $expectedHash = $Expected.captures.$name
        if (-not $expectedHash -or $expectedHash -ne $Actual[$name]) {
            Write-Error "Screenshot mismatch: $name expected=$expectedHash actual=$($Actual[$name])"
            $Mismatch = $true
        } else {
            Write-Host "PASS screenshot $name $($Actual[$name])"
        }
    }
    if ($Mismatch) {
        throw "UI screenshot regression failed. Actual captures: $OutputDir"
    }
}

if ($KeepArtifacts) {
    Write-Host "UI screenshots retained at $OutputDir"
}

Write-Host 'UI simulator build, scripted navigation and screenshot gate passed.'
