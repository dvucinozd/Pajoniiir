param(
    [ValidateSet("Functional", "Soak")]
    [string]$Mode = "Functional",

    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,

    [ValidateRange(1, 1440)]
    [int]$DurationMinutes = 50,

    [ValidateRange(1, 3600)]
    [int]$OperationIntervalSeconds = 180,

    [ValidateRange(1, 60)]
    [int]$SampleIntervalSeconds = 5,

    [uint32]$Deck1TrackKey = 115,
    [uint32]$Deck2TrackKey = 18,

    [ValidateRange(0, 10000)]
    [uint32]$MaximumOutputLateDelta = 120,

    [string]$OutputDirectory = "tmp/p4-release-qualification",
    [switch]$SelfTest
)

# Accelerated M2 beta qualification harness. It combines guarded remote
# operations with continuous health sampling. Physical FLX4-only behavior is
# kept as an explicit operator gate; HTTP controls are not treated as proof of
# Master Tempo, scratch, Hot Cue, Beat FX or acoustic behavior.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = Split-Path -Parent $PSScriptRoot
$qualificationSelfTest = [bool]$SelfTest
. (Join-Path $PSScriptRoot "run_p4_lifecycle_cycle.ps1") `
    -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -OutputDirectory $OutputDirectory -DefineOnly

function Get-QualificationSnapshot {
    param([string]$Name)
    $firmware = Invoke-ApiJson -Path "/api/firmware"
    $status = Invoke-ApiJson -Path "/api/status"
    return [pscustomobject][ordered]@{
        name = $Name
        captured_utc = [DateTime]::UtcNow.ToString("o")
        version = [string]$firmware.running_version
        slot = [string]$firmware.running_slot
        ota_state = [string]$firmware.state
        ota_error = [string]$firmware.last_error
        storage_mounted = [bool]$status.p4_usb.storage.mounted
        controller_present = [bool]$status.controller.present
        controller_profile = [string]$status.controller.profile_state
        controller_midi_in = [bool]$status.controller.midi_in
        controller_midi_out = [bool]$status.controller.midi_out
        controller_usb_audio = [bool]$status.controller.usb_audio
        controller_accepting_midi_out = [bool]$status.p4_usb.controller.accepting_midi_out
        root_power_mask = [uint32]$status.p4_usb.host.root_power_mask
        recovery_failures = [uint64]$status.p4_usb.host.recovery_failures
        recovery_queue_drops = [uint64]$status.p4_usb.host.recovery_queue_drops
        daemon_errors = [uint64]$status.p4_usb.host.daemon_errors
        runtime_queue_failures = [uint64]$status.p4_usb.runtime.queue_failures
        service_log_dropped = [uint64]$status.service_log.dropped
        deck1_state = [string]$status.deck1.state_text
        deck2_state = [string]$status.deck2.state_text
        deck1_playing = [bool]$status.deck1.playing
        deck2_playing = [bool]$status.deck2.playing
        deck1_position_ms = [uint64]$status.deck1.position_ms
        deck2_position_ms = [uint64]$status.deck2.position_ms
        deck1_duration_ms = [uint64]$status.deck1.duration_ms
        deck2_duration_ms = [uint64]$status.deck2.duration_ms
        submitted_blocks = [uint64]$status.diagnostics.usb_headphones.submitted_blocks
        dropped_blocks = [uint64]$status.diagnostics.usb_headphones.dropped_blocks
        overflow_frames = [uint64]$status.diagnostics.usb_headphones.overflow_frames
        underflow_frames = [uint64]$status.diagnostics.usb_headphones.underflow_frames
        packet_failures = [uint64]$status.diagnostics.usb_headphones.packet_failures
        packet_lost_frames = [uint64]$status.diagnostics.usb_headphones.packet_lost_frames
        uac_data_loss = [bool]$status.diagnostics.usb_headphones.data_loss
        uac_flags = [uint32]$status.diagnostics.usb_headphones.data_loss_flags
        pcm1 = [uint64]$status.diagnostics.pcm_underrun1
        pcm2 = [uint64]$status.diagnostics.pcm_underrun2
        locked_reads1 = [uint64]$status.diagnostics.locked_backend_reads1
        locked_reads2 = [uint64]$status.diagnostics.locked_backend_reads2
        output_late = [uint64]$status.diagnostics.output_late_count
        output_late_max_us = [uint64]$status.diagnostics.output_late_max_us
        output_late_threshold_us = [uint64]$status.diagnostics.output_late_threshold_us
        phase_mix_us = [uint64]$status.diagnostics.phase_mix_us
        phase_main_us = [uint64]$status.diagnostics.phase_main_us
        internal_free = [uint64]$status.diagnostics.internal_free
        psram_free = [uint64]$status.diagnostics.psram_free
        twdt_current = [bool]$status.audio_wdt_trace.current.twdt_isr_seen
    }
}

function Get-QualificationFailures {
    param($Baseline, $Current, [uint32]$LateLimit)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if ($Current.version -ne $Baseline.version -or
        $Current.slot -ne $Baseline.slot) {
        Add-Failure $failures "firmware identity changed"
    }
    if (-not $Current.storage_mounted) {
        Add-Failure $failures "USB0 is not mounted"
    }
    if (-not $Current.controller_present -or
        $Current.controller_profile -ne "active" -or
        -not $Current.controller_midi_in -or
        -not $Current.controller_midi_out -or
        -not $Current.controller_usb_audio) {
        Add-Failure $failures "FLX4 profile, MIDI or UAC is not healthy"
    }
    if ($Current.root_power_mask -ne 3) {
        Add-Failure $failures "both USB root ports are not powered"
    }
    if (-not $Current.deck1_playing -or -not $Current.deck2_playing) {
        Add-Failure $failures "both decks are not playing"
    }
    foreach ($field in @(
            "recovery_failures", "recovery_queue_drops", "daemon_errors",
            "runtime_queue_failures", "service_log_dropped", "dropped_blocks",
            "overflow_frames", "underflow_frames", "packet_failures",
            "packet_lost_frames", "pcm1", "pcm2", "locked_reads1",
            "locked_reads2")) {
        $delta = Get-CounterDelta $Current.$field $Baseline.$field
        if ($delta -ne 0) {
            Add-Failure $failures "$field increased by $delta"
        }
    }
    $lateDelta = Get-CounterDelta $Current.output_late $Baseline.output_late
    if ($lateDelta -gt $LateLimit) {
        Add-Failure $failures "output_late increased by $lateDelta (limit $LateLimit)"
    }
    if ($Current.uac_data_loss -or $Current.uac_flags -ne 0) {
        Add-Failure $failures "active UAC data-loss state is set"
    }
    if ($Current.twdt_current) {
        Add-Failure $failures "current TWDT ISR flag is set"
    }
    if ($Current.ota_state -ne "idle" -or $Current.ota_error) {
        Add-Failure $failures "OTA state is not clean and idle"
    }
    return @($failures)
}

function Invoke-ControlValuePost {
    param(
        [ValidateRange(1, 2)][int]$Deck,
        [string]$Action,
        [uint32]$Value
    )
    $headers = @{ "X-DDJ-Control" = "1" }
    Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/control?deck=$Deck&action=$Action&value=$Value" `
        -TimeoutSec 5 | Out-Null
}

function Invoke-QualificationOperation {
    param([int]$Index, $Snapshot)
    switch ($Index % 6) {
        0 {
            $target = [uint32][Math]::Min(5000, [Math]::Max(0, $Snapshot.deck1_duration_ms - 10000))
            Invoke-ControlValuePost -Deck 1 -Action "seek" -Value $target
            return "D1 seek to $target ms"
        }
        1 {
            $target = [uint32][Math]::Min(7000, [Math]::Max(0, $Snapshot.deck2_duration_ms - 10000))
            Invoke-ControlValuePost -Deck 2 -Action "seek" -Value $target
            return "D2 seek to $target ms"
        }
        2 {
            Invoke-ControlPost -Deck 1 -Action "loop_4"
            return "D1 four-beat loop set"
        }
        3 {
            Invoke-ControlPost -Deck 1 -Action "loop_clear"
            Invoke-ControlPost -Deck 2 -Action "loop_4"
            return "D1 loop clear, D2 four-beat loop set"
        }
        4 {
            Invoke-ControlPost -Deck 2 -Action "loop_clear"
            Invoke-ControlPost -Deck 1 -Action "cue"
            Start-Sleep -Milliseconds 300
            Invoke-ControlPost -Deck 1 -Action "play_pause"
            Start-Sleep -Milliseconds 300
            Invoke-ControlPost -Deck 1 -Action "loop_4"
            return "D2 loop clear, D1 CUE/restart and four-beat loop set"
        }
        default {
            Invoke-ControlPost -Deck 2 -Action "cue"
            Start-Sleep -Milliseconds 300
            Invoke-ControlPost -Deck 2 -Action "play_pause"
            Start-Sleep -Milliseconds 300
            Invoke-ControlPost -Deck 2 -Action "loop_4"
            return "D2 CUE/restart and four-beat loop set"
        }
    }
}

function Request-FunctionalStage {
    param([int]$Stage)
    $message = switch ($Stage) {
        1 { "Enable Master Tempo on both decks, set opposing non-zero pitch, and confirm MAIN plus cue are audible." }
        2 { "Exercise D1 scratch release/re-grab, one Hot Cue, and near-EOF Shift+Jog search; return D1 to stable PLAY. Then repeat on D2 while D1 keeps playing, and return both decks to PLAY." }
        default { "Exercise Beat FX on CH1, CH2 and 1&2, change beat size, check Echo/Delay tails and PFL/MAIN routing. Return both decks to PLAY." }
    }
    Request-OperatorStep -Message $message
}

function Write-QualificationEvidence {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else { Join-Path $repoRoot $OutputDirectory }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stamp = [DateTime]::UtcNow.ToString("yyyyMMddTHHmmssZ")
    $stem = "{0}-{1}-boot{2}" -f $Mode.ToLowerInvariant(), $stamp,
        $Evidence.boot_epoch
    $jsonPath = Join-Path $outputRoot "$stem.json"
    $mdPath = Join-Path $outputRoot "$stem.md"
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath `
        -Encoding utf8
    @(
        "# P4 $Mode qualification - boot $($Evidence.boot_epoch)", "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)`` on ``$($Evidence.firmware_slot)``",
        "- Planned/actual duration: $($Evidence.duration_minutes) / $($Evidence.actual_minutes) min",
        "- Tracks: D1 $($Evidence.deck1_track_key) / D2 $($Evidence.deck2_track_key)",
        "- Samples/operations: $(@($Evidence.samples).Count) / $(@($Evidence.operations).Count)",
        "- Output-late delta/max: $($Evidence.deltas.output_late) / $($Evidence.final.output_late_max_us) us",
        "- PCM underrun deltas: $($Evidence.deltas.pcm1) / $($Evidence.deltas.pcm2)",
        "- UAC loss: $($Evidence.final.uac_data_loss), flags $($Evidence.final.uac_flags)",
        "- Operator acceptance: $($Evidence.operator_acceptance)",
        "- Failures: $(@($Evidence.failures).Count)", "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    ) | Set-Content -LiteralPath $mdPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $mdPath }
}

function Invoke-QualificationSelfTest {
    $baseline = [pscustomobject]@{
        version="test"; slot="ota_0"; storage_mounted=$true
        controller_present=$true; controller_profile="active"
        controller_midi_in=$true; controller_midi_out=$true
        controller_usb_audio=$true; root_power_mask=3; deck1_playing=$true
        deck2_playing=$true; recovery_failures=0; recovery_queue_drops=0
        daemon_errors=0; runtime_queue_failures=0; service_log_dropped=0
        dropped_blocks=0; overflow_frames=0; underflow_frames=100
        packet_failures=0; packet_lost_frames=0; pcm1=0; pcm2=0
        locked_reads1=0; locked_reads2=0; output_late=10
        uac_data_loss=$false; uac_flags=0; twdt_current=$false
        ota_state="idle"; ota_error=""
    }
    $good = $baseline.psobject.Copy()
    $good.output_late = 14
    if (@(Get-QualificationFailures -Baseline $baseline -Current $good `
            -LateLimit 4).Count -ne 0) {
        throw "clean qualification self-test failed"
    }
    $bad = $good.psobject.Copy()
    $bad.pcm1 = 1
    $bad.output_late = 15
    if (@(Get-QualificationFailures -Baseline $baseline -Current $bad `
            -LateLimit 4).Count -ne 2) {
        throw "fault qualification self-test failed"
    }
    Write-Host "P4 release qualification harness self-test passed."
}

if ($qualificationSelfTest) {
    Invoke-QualificationSelfTest
    return
}

if (-not $PSBoundParameters.ContainsKey("DurationMinutes")) {
    $DurationMinutes = if ($Mode -eq "Soak") { 180 } else { 50 }
}
if ($Mode -eq "Functional" -and
    ($DurationMinutes -lt 45 -or $DurationMinutes -gt 60)) {
    throw "Functional mode requires a declared 45-60 minute window"
}
if ($Mode -eq "Soak" -and $DurationMinutes -lt 180) {
    throw "Soak mode requires at least 180 minutes"
}

$library = Get-LibraryInfo
$track1 = Get-TrackByKey -Library $library -TrackKey $Deck1TrackKey
$track2 = Get-TrackByKey -Library $library -TrackKey $Deck2TrackKey
$preflight = Get-QualificationSnapshot -Name "preflight"
if ($ExpectedVersion -and $preflight.version -ne $ExpectedVersion) {
    throw "Expected $ExpectedVersion, device runs $($preflight.version)"
}
if (-not (Test-DeviceState -Snapshot $preflight `
        -StoragePresent $true -ControllerPresent $true)) {
    throw "USB0 and FLX4 are not ready"
}

Stop-Decks
Invoke-LoadPost -TrackKey $Deck1TrackKey -Generation $library.generation -Deck 1
[void](Wait-DeckReady -Deck 1 -ExpectedTitle ([string]$track1.title))
Invoke-LoadPost -TrackKey $Deck2TrackKey -Generation $library.generation -Deck 2
[void](Wait-DeckReady -Deck 2 -ExpectedTitle ([string]$track2.title))
Invoke-ControlPost -Deck 1 -Action "play_pause"
Invoke-ControlPost -Deck 2 -Action "play_pause"
Start-Sleep -Seconds 3
Invoke-ControlPost -Deck 1 -Action "loop_4"
Invoke-ControlPost -Deck 2 -Action "loop_4"

if ($Mode -eq "Functional") {
    Request-FunctionalStage -Stage 1
} else {
    Request-OperatorStep -Message "Set the intended dual-deck Master Tempo, pitch, MAIN/cue and FX stress state. Confirm both decks sound normal; the harness will then run unattended."
}

$bootLog = @(Get-CurrentBootLog)
$bootEpoch = Get-BootEpoch -BootLog $bootLog
$baseline = Get-QualificationSnapshot -Name "baseline"
$samples = New-Object 'System.Collections.Generic.List[object]'
$operations = New-Object 'System.Collections.Generic.List[object]'
$failures = New-Object 'System.Collections.Generic.List[string]'
$samples.Add($baseline)
$timer = [Diagnostics.Stopwatch]::StartNew()
$nextOperation = [double]$OperationIntervalSeconds
$stage2Done = $false
$stage3Done = $false
$operationIndex = 0

try {
    while ($timer.Elapsed.TotalMinutes -lt $DurationMinutes) {
        Start-Sleep -Seconds $SampleIntervalSeconds
        $current = Get-QualificationSnapshot -Name "sample"
        $samples.Add($current)
        $currentFailures = @(Get-QualificationFailures -Baseline $baseline `
            -Current $current -LateLimit $MaximumOutputLateDelta)
        if ($currentFailures.Count -ne 0) {
            foreach ($failure in $currentFailures) { Add-Failure $failures $failure }
            break
        }

        if ($Mode -eq "Functional" -and -not $stage2Done -and
            $timer.Elapsed.TotalMinutes -ge ($DurationMinutes / 3.0)) {
            Request-FunctionalStage -Stage 2
            $stage2Done = $true
        }
        if ($Mode -eq "Functional" -and -not $stage3Done -and
            $timer.Elapsed.TotalMinutes -ge (2.0 * $DurationMinutes / 3.0)) {
            Request-FunctionalStage -Stage 3
            $stage3Done = $true
        }
        if ($timer.Elapsed.TotalSeconds -ge $nextOperation) {
            $description = Invoke-QualificationOperation -Index $operationIndex `
                -Snapshot $current
            $operations.Add([pscustomobject]@{
                elapsed_seconds = [Math]::Round($timer.Elapsed.TotalSeconds, 3)
                operation = $description
            })
            $operationIndex++
            $nextOperation += $OperationIntervalSeconds
        }
    }
}
catch {
    Add-Failure $failures $_.Exception.Message
}

$final = $samples[$samples.Count - 1]
try {
    $final = Get-QualificationSnapshot -Name "final"
}
catch {
    Add-Failure $failures "final device snapshot unavailable: $($_.Exception.Message)"
}
try {
    $finalBoot = Get-BootEpoch -BootLog @(Get-CurrentBootLog)
    if ($finalBoot -ne $bootEpoch) {
        Add-Failure $failures "boot epoch changed from $bootEpoch to $finalBoot"
    }
}
catch {
    Add-Failure $failures "final boot epoch unavailable: $($_.Exception.Message)"
}
foreach ($failure in @(Get-QualificationFailures -Baseline $baseline `
        -Current $final -LateLimit $MaximumOutputLateDelta)) {
    if (-not $failures.Contains([string]$failure)) {
        Add-Failure $failures ([string]$failure)
    }
}

$operatorAcceptance = "not-requested"
if ($Mode -eq "Functional" -and $failures.Count -eq 0) {
    $operatorAcceptance = (Read-Host "Were MAIN and cue continuously free of interruption/distortion? (da/ne)").Trim().ToLowerInvariant()
    if ($operatorAcceptance -notin @("da", "yes", "y")) {
        Add-Failure $failures "operator did not accept the listening gate"
    }
}

$deltas = [pscustomobject][ordered]@{
    output_late = Get-CounterDelta $final.output_late $baseline.output_late
    pcm1 = Get-CounterDelta $final.pcm1 $baseline.pcm1
    pcm2 = Get-CounterDelta $final.pcm2 $baseline.pcm2
    locked_reads1 = Get-CounterDelta $final.locked_reads1 $baseline.locked_reads1
    locked_reads2 = Get-CounterDelta $final.locked_reads2 $baseline.locked_reads2
    submitted_blocks = Get-CounterDelta $final.submitted_blocks $baseline.submitted_blocks
}
$evidence = [pscustomobject][ordered]@{
    schema = "pajoniiir.p4-release-qualification.v1"
    result = if ($failures.Count -eq 0) { "PASS" } else { "FAIL" }
    mode = $Mode
    firmware_version = $baseline.version
    firmware_slot = $baseline.slot
    boot_epoch = $bootEpoch
    duration_minutes = $DurationMinutes
    actual_minutes = [Math]::Round($timer.Elapsed.TotalMinutes, 3)
    deck1_track_key = $Deck1TrackKey
    deck1_track_title = [string]$track1.title
    deck2_track_key = $Deck2TrackKey
    deck2_track_title = [string]$track2.title
    maximum_output_late_delta = $MaximumOutputLateDelta
    operator_acceptance = $operatorAcceptance
    baseline = $baseline
    final = $final
    deltas = $deltas
    operations = $operations.ToArray()
    samples = $samples.ToArray()
    failures = $failures.ToArray()
}
$paths = Write-QualificationEvidence -Evidence $evidence
Write-Host "Result: $($evidence.result)"
Write-Host "Evidence: $($paths.json)"
try {
    Stop-Decks
}
catch {
    Write-Warning "Unable to stop decks after evidence capture: $($_.Exception.Message)"
}
if ($failures.Count -ne 0) {
    throw "Qualification failed: $($failures -join '; ')"
}
