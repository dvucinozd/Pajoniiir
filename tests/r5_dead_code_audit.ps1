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

$AudioEngineImpl = "firmware/main-deck-p4/components/audio_engine/audio_engine.c"
$MixerImpl = "firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c"
$LibraryImpl = "firmware/main-deck-p4/components/ui/ui_library.c"

foreach ($symbol in @(
    "audio_engine_play",
    "audio_engine_pause",
    "audio_engine_stop",
    "audio_engine_seek",
    "audio_engine_set_loop"
)) {
    Assert-NoUnexpectedCalls $symbol @($AudioEngineImpl)
}
Assert-NoUnexpectedCalls "audio_engine_clear_loop" @($AudioEngineImpl, $LibraryImpl)
Assert-NoUnexpectedCalls "audio_output_mixer_next" @($MixerImpl)

$scratchStorageHits = Find-CSourceCalls "s_scratch_storage"
$scratchUnexpected = @($scratchStorageHits | Where-Object { $_.RelativePath -ne $AudioEngineImpl })
if ($scratchUnexpected.Count -gt 0) {
    throw "s_scratch_storage escaped the audio_engine implementation"
}
Write-Host ("R5 audit: s_scratch_storage -> {0} audio_engine hit(s)" -f $scratchStorageHits.Count)

Write-Host "R5 dead-code call-graph audit passed."
