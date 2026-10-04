param(
    [ValidateSet('main-deck-p4','main-deck-jc1060')][string]$Project = 'main-deck-p4',
    [ValidateSet('idle-on','idle-off','psram')][string]$Variant = 'idle-on'
)
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if ($Variant -eq 'psram' -and $Project -ne 'main-deck-jc1060') {
    throw 'JC4880 USB DMA stays internal; PSRAM experiment is JC1060 only'
}
# Build only. Never inspect serial devices, flash, or send OTA.
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
$version = & idf.py --version
if ($LASTEXITCODE -ne 0 -or "$version" -notmatch 'ESP-IDF v6\.0\.2') { throw 'ESP-IDF 6.0.2 required' }
$projectPath = Join-Path $repo "firmware/$Project"
$build = Join-Path $projectPath "build_storage_$Variant"
$args = @('-C', $projectPath, '-B', $build, '-D', "SDKCONFIG=$build/sdkconfig", '-D', 'PAJONIIIR_STORAGE_EXPERIMENT=ON')
if ($Variant -ne 'idle-on') {
    $file = if ($Variant -eq 'idle-off') { 'sdkconfig.storage_idle_off' } else { 'sdkconfig.storage_psram' }
    $args += @('-D', "PAJONIIIR_STORAGE_VARIANT=$(Join-Path $repo "firmware/main-deck-p4/$file")")
}
& idf.py @args build
if ($LASTEXITCODE -ne 0) { throw "Storage experiment build failed: $LASTEXITCODE" }
& python (Join-Path $repo 'tools/check_board_build.py') --build $build --project $Project --experimental-recorder
if ($LASTEXITCODE -ne 0) { throw 'Experimental build isolation gate failed' }
& git -C $repo diff --exit-code -- "firmware/$Project/dependencies.lock"
if ($LASTEXITCODE -ne 0) { throw 'Dependency lock drifted' }
Write-Host "EXPERIMENTAL ONLY: $Project / $Variant. Hardware acceptance NOT RUN. No device contacted."
