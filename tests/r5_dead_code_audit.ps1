$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Rg = (Get-Command rg -ErrorAction Stop).Source

function Find-CSourceCalls {
    param([Parameter(Mandatory = $true)][string]$LiteralPattern)

    $lines = & $Rg --line-number --fixed-strings --glob "*.c" `
        --glob "!**/build*/**" --glob "!**/managed_components/**" `
        -- $LiteralPattern firmware 2>$null
    if ($LASTEXITCODE -notin @(0, 1)) {
        throw "rg call-graph scan failed with exit code $LASTEXITCODE"
    }
    return @($lines | ForEach-Object {
        if ($_ -notmatch '^(.*?):(\d+):(.*)$') {
            throw "Cannot parse rg result: $_"
        }
        [pscustomobject]@{
            RelativePath = $Matches[1].Replace('\', '/')
            LineNumber = [int]$Matches[2]
            Line = $Matches[3].Trim()
        }
    })
}

function Assert-NoUnexpectedCalls {
    param(
        [Parameter(Mandatory = $true)][string]$Symbol,
        [Parameter(Mandatory = $true)][string[]]$AllowedPaths
    )

    $hits = Find-CSourceCalls "$Symbol("
    $unexpected = @($hits | Where-Object { $_.RelativePath -notin $AllowedPaths })
    if ($unexpected.Count -gt 0) {
        $details = $unexpected | ForEach-Object {
            "$($_.RelativePath):$($_.LineNumber): $($_.Line)"
        }
        throw "Unexpected production caller(s) for ${Symbol}:`n$($details -join "`n")"
    }
    Write-Host ("R5 audit: {0} -> {1} expected source hit(s)" -f $Symbol, $hits.Count)
}

function Assert-SymbolAbsent {
    param([Parameter(Mandatory = $true)][string]$Symbol)

    & $Rg --quiet --fixed-strings --glob "*.c" --glob "*.h" `
        --glob "!**/build*/**" --glob "!**/managed_components/**" `
        -- "$Symbol(" firmware
    if ($LASTEXITCODE -eq 0) {
        throw "Removed compatibility symbol returned: $Symbol"
    }
    if ($LASTEXITCODE -ne 1) {
        throw "rg compatibility-symbol scan failed with exit code $LASTEXITCODE"
    }
    Write-Host "R5 audit: $Symbol removed"
}

$AudioEngineImpl = "firmware/main-deck-p4/components/audio_engine/audio_engine.c"
foreach ($symbol in @(
    "audio_engine_load",
    "audio_engine_play",
    "audio_engine_pause",
    "audio_engine_stop",
    "audio_engine_seek",
    "audio_engine_set_pitch",
    "audio_engine_set_pitch_percent",
    "audio_engine_position_ms",
    "audio_engine_is_playing",
    "audio_engine_get_state",
    "audio_engine_load_progress",
    "audio_engine_last_error",
    "audio_engine_last_error_text",
    "audio_engine_set_loop",
    "audio_engine_clear_loop",
    "audio_engine_get_loop_state",
    "audio_output_mixer_next",
    "audio_output_mixer_next_full"
)) {
    Assert-SymbolAbsent $symbol
}

foreach ($relativePath in @(
    "firmware/control-board-s3/components/panel_io",
    "firmware/control-board-s3/components/midi_compat",
    "firmware/control-board-s3/components/calibration",
    "firmware/control-board-s3/sdkconfig.legacy.defaults"
)) {
    if (Test-Path -LiteralPath (Join-Path $RepoRoot $relativePath)) {
        throw "Retired S3 legacy path returned: $relativePath"
    }
    Write-Host "R5 audit: $relativePath removed"
}

& $Rg --quiet --fixed-strings --glob "*.c" --glob "*.h" --glob "Kconfig*" `
    --glob "CMakeLists.txt" --glob "*.defaults" --glob "!**/build*/**" `
    --glob "!**/managed_components/**" -- "DDJ_FLX4_HOST_MODE" `
    firmware/control-board-s3
if ($LASTEXITCODE -eq 0) {
    throw "Retired CONFIG_DDJ_FLX4_HOST_MODE returned"
}
if ($LASTEXITCODE -ne 1) {
    throw "rg S3 legacy-config scan failed with exit code $LASTEXITCODE"
}
Write-Host "R5 audit: CONFIG_DDJ_FLX4_HOST_MODE removed"

$scratchStorageHits = Find-CSourceCalls "s_scratch_storage"
$scratchUnexpected = @($scratchStorageHits | Where-Object { $_.RelativePath -ne $AudioEngineImpl })
if ($scratchUnexpected.Count -gt 0) {
    throw "s_scratch_storage escaped the audio_engine implementation"
}
Write-Host ("R5 audit: s_scratch_storage -> {0} audio_engine hit(s)" -f $scratchStorageHits.Count)

Write-Host "R5 dead-code call-graph audit passed."
