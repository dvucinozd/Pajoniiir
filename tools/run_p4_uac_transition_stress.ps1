param(
    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,

    [ValidateRange(1, 100)]
    [int]$Cycles = 12,

    [ValidateRange(100, 2000)]
    [int]$CueHoldMilliseconds = 300,

    [ValidateRange(25, 1000)]
    [int]$SampleIntervalMilliseconds = 100,

    [ValidateRange(1, 100)]
    [int]$SamplesPerCycle = 15,

    [uint32]$Deck1TrackKey = 115,
    [uint32]$Deck2TrackKey = 18,
    [string]$OutputDirectory = "tmp/p4-uac-transition-stress",
    [switch]$SelfTest
)

# Focused regression for the release-soak failure in which the FLX4 UAC ring
# drained during D2 loop-clear + D1 CUE/restart + loop-set.  It intentionally
# compresses the operation interval so the transport boundary is exercised in
# seconds rather than waiting for the full combined soak.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = Split-Path -Parent $PSScriptRoot
$transitionSelfTest = [bool]$SelfTest
. (Join-Path $PSScriptRoot "run_p4_lifecycle_cycle.ps1") `
    -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -OutputDirectory $OutputDirectory -DefineOnly

function Get-TransitionFailures {
    param($Baseline, $Current)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ($Current.version -ne $Baseline.version -or
        $Current.slot -ne $Baseline.slot) {
        Add-Failure $failures "firmware identity changed"
    }
    if (-not (Test-DeviceState -Snapshot $Current `
            -StoragePresent $true -ControllerPresent $true)) {
        Add-Failure $failures "USB0 or FLX4 is not healthy"
    }
    if (-not $Current.deck1_playing -or -not $Current.deck2_playing) {
        Add-Failure $failures "both decks are not playing after the transition"
    }
    foreach ($field in @(
            "dropped_blocks", "overflow_frames", "underflow_frames",
            "packet_failures", "packet_lost_frames", "pcm1", "pcm2",
            "recovery_failures", "recovery_queue_drops", "daemon_errors",
            "runtime_queue_failures", "service_log_dropped")) {
        $delta = Get-CounterDelta $Current.$field $Baseline.$field
        if ($delta -ne 0) {
            Add-Failure $failures "$field increased by $delta"
        }
    }
    if ($Current.uac_data_loss -or $Current.uac_flags -ne 0) {
        Add-Failure $failures "active UAC data-loss state is set"
    }
    if ($Current.twdt_current) {
        Add-Failure $failures "current TWDT ISR flag is set"
    }
    return @($failures)
}

function Write-TransitionEvidence {
    param($Evidence)
    $root = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else {
        Join-Path $repoRoot $OutputDirectory
    }
    New-Item -ItemType Directory -Path $root -Force | Out-Null
    $stamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
    $path = Join-Path $root "uac-transition-$stamp.json"
    $Evidence | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path `
        -Encoding utf8
    return $path
}

function Invoke-HarnessSelfTest {
    $baseline = [pscustomobject]@{
        version = "test"; slot = "ota_0"; storage_mounted = $true
        controller_present = $true; controller_profile = "active"
        controller_midi_in = $true; controller_midi_out = $true
        controller_usb_audio = $true; controller_accepting_midi_out = $true
        root_power_mask = 3; deck1_playing = $true; deck2_playing = $true
        dropped_blocks = 0; overflow_frames = 0; underflow_frames = 10
        packet_failures = 0; packet_lost_frames = 0; pcm1 = 0; pcm2 = 0
        recovery_failures = 0; recovery_queue_drops = 0; daemon_errors = 0
        runtime_queue_failures = 0; service_log_dropped = 0
        uac_data_loss = $false; uac_flags = 0; twdt_current = $false
    }
    $good = $baseline.psobject.Copy()
    if (@(Get-TransitionFailures -Baseline $baseline -Current $good).Count -ne 0) {
        throw "clean transition was rejected"
    }
    $bad = $baseline.psobject.Copy()
    $bad.underflow_frames = 11
    $bad.uac_data_loss = $true
    $bad.uac_flags = 16
    if (@(Get-TransitionFailures -Baseline $baseline -Current $bad).Count -ne 2) {
        throw "UAC transition loss was not rejected"
    }
    Write-Output "P4 UAC transition stress self-test passed."
}

if ($transitionSelfTest) {
    Invoke-HarnessSelfTest
    exit 0
}
if (-not $ExpectedVersion) {
    throw "-ExpectedVersion is required for hardware evidence"
}

$library = Get-LibraryInfo
$track1 = Get-TrackByKey -Library $library -TrackKey $Deck1TrackKey
$track2 = Get-TrackByKey -Library $library -TrackKey $Deck2TrackKey
$preflight = Get-DeviceSnapshot -Name "preflight"
if ($preflight.version -ne $ExpectedVersion) {
    throw "Expected $ExpectedVersion, device runs $($preflight.version)"
}
if (-not (Test-DeviceState -Snapshot $preflight `
        -StoragePresent $true -ControllerPresent $true)) {
    throw "USB0 and FLX4 are not ready"
}

$samples = New-Object 'System.Collections.Generic.List[object]'
$operations = New-Object 'System.Collections.Generic.List[object]'
$failures = New-Object 'System.Collections.Generic.List[string]'
$bootEpoch = Get-BootEpoch -BootLog @(Get-CurrentBootLog)
$baseline = $null
$final = $null
$timer = [Diagnostics.Stopwatch]::StartNew()

try {
    Stop-Decks
    Invoke-LoadPost -TrackKey $Deck1TrackKey -Generation $library.generation -Deck 1
    [void](Wait-DeckReady -Deck 1 -ExpectedTitle ([string]$track1.title))
    Invoke-LoadPost -TrackKey $Deck2TrackKey -Generation $library.generation -Deck 2
    [void](Wait-DeckReady -Deck 2 -ExpectedTitle ([string]$track2.title))
    Invoke-ControlPost -Deck 1 -Action "play_pause"
    Invoke-ControlPost -Deck 2 -Action "play_pause"
    Start-Sleep -Seconds 3
    Invoke-ControlPost -Deck 1 -Action "loop_clear"
    Invoke-ControlPost -Deck 2 -Action "loop_clear"
    Start-Sleep -Milliseconds 400
    Invoke-ControlPost -Deck 2 -Action "loop_4"
    Start-Sleep -Seconds 1

    $baseline = Get-DeviceSnapshot -Name "baseline"
    $samples.Add($baseline)
    foreach ($failure in @(Get-TransitionFailures -Baseline $baseline `
            -Current $baseline)) {
        Add-Failure $failures $failure
    }

    # The dot-sourced lifecycle helper owns a validated top-level $Cycle
    # parameter. PowerShell variable names are case-insensitive, so keep this
    # local counter distinct from that helper parameter.
    for ($transitionIndex = 1;
        $transitionIndex -le $Cycles -and $failures.Count -eq 0;
        $transitionIndex++) {
        Invoke-ControlPost -Deck 2 -Action "loop_clear"
        Invoke-ControlPost -Deck 1 -Action "cue"
        Start-Sleep -Milliseconds $CueHoldMilliseconds
        Invoke-ControlPost -Deck 1 -Action "play_pause"
        Start-Sleep -Milliseconds $CueHoldMilliseconds
        Invoke-ControlPost -Deck 1 -Action "loop_4"
        $operations.Add([pscustomobject]@{
            cycle = $transitionIndex
            elapsed_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
            operation = "D2 loop clear, D1 CUE/restart and four-beat loop set"
        })

        for ($sampleIndex = 1; $sampleIndex -le $SamplesPerCycle; $sampleIndex++) {
            Start-Sleep -Milliseconds $SampleIntervalMilliseconds
            $current = Get-DeviceSnapshot -Name "cycle_${transitionIndex}_sample_$sampleIndex"
            $samples.Add($current)
            foreach ($failure in @(Get-TransitionFailures -Baseline $baseline `
                    -Current $current)) {
                if (-not $failures.Contains([string]$failure)) {
                    Add-Failure $failures ([string]$failure)
                }
            }
            if ($failures.Count -ne 0) { break }
        }
        if ($failures.Count -eq 0) {
            Invoke-ControlPost -Deck 1 -Action "loop_clear"
            Invoke-ControlPost -Deck 2 -Action "loop_4"
            Start-Sleep -Milliseconds 400
        }
    }
    $final = Get-DeviceSnapshot -Name "final"
    $samples.Add($final)
    if ((Get-BootEpoch -BootLog @(Get-CurrentBootLog)) -ne $bootEpoch) {
        Add-Failure $failures "device rebooted during transition stress"
    }
}
catch {
    Add-Failure $failures $_.Exception.Message
}
finally {
    try { Stop-Decks } catch { }
}

$evidence = [pscustomobject][ordered]@{
    schema = "pajoniiir.p4-uac-transition-stress.v1"
    result = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
    firmware_version = if ($baseline) { $baseline.version } else { "unknown" }
    firmware_slot = if ($baseline) { $baseline.slot } else { "unknown" }
    boot_epoch = $bootEpoch
    requested_cycles = $Cycles
    completed_operations = $operations.Count
    actual_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
    baseline = $baseline
    final = $final
    operations = $operations.ToArray()
    samples = $samples.ToArray()
    failures = $failures.ToArray()
}
$path = Write-TransitionEvidence -Evidence $evidence
Write-Output "Result: $($evidence.result)"
Write-Output "Evidence: $path"
if ($failures.Count -ne 0) {
    throw "UAC transition stress failed: $($failures -join '; ')"
}
