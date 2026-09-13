param(
    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,

    [ValidateRange(5, 60)]
    [int]$PlaybackSeconds = 5,

    [ValidateRange(5, 120)]
    [int]$DeviceTimeoutSeconds = 30,

    [ValidateRange(1, 5)]
    [int]$StartPair = 1,

    [ValidateRange(1, 5)]
    [int]$EndPair = 5,

    [string]$OutputDirectory = "tmp/p4-lifecycle",
    [switch]$SelfTest
)

# Accelerated physical harness for lifecycle Groups I and J. One load/play
# setup is shared across five I/J pairs, but every reconnect keeps an
# independent baseline, result and evidence file. The batch stops on the first
# failure so later cycles can never hide an earlier fault.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = Split-Path -Parent $PSScriptRoot
$ijSelfTest = [bool]$SelfTest
. (Join-Path $PSScriptRoot "run_p4_lifecycle_cycle.ps1") `
    -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -PlaybackSeconds $PlaybackSeconds `
    -DeviceTimeoutSeconds $DeviceTimeoutSeconds `
    -OutputDirectory $OutputDirectory -DefineOnly

$heldControls = @(
    [pscustomobject]@{
        name = "Deck 1 jog touch"
        action = "Hold the top surface of the Deck 1 jog wheel, disconnect FLX4 while it is still held, then release it only after the cable is out."
        acceptance = "Deck 1 jog/scratch is not latched after reconnect."
    },
    [pscustomobject]@{
        name = "Deck 1 Shift"
        action = "Hold Deck 1 SHIFT, disconnect FLX4 while SHIFT is still held, then release it only after the cable is out."
        acceptance = "Deck 1 SHIFT is not latched after reconnect."
    },
    [pscustomobject]@{
        name = "Deck 1 Censor"
        action = "Hold Deck 1 SHIFT plus PLAY/PAUSE (Censor), disconnect FLX4 while both are still held, then release them only after the cable is out."
        acceptance = "Censor/reverse is released and normal forward playback resumes after reconnect."
    },
    [pscustomobject]@{
        name = "Deck 1 Pad FX1 pad 1"
        action = "Select Deck 1 PAD FX1, hold performance pad 1, disconnect FLX4 while the pad is still held, then release it only after the cable is out."
        acceptance = "Pad FX is released and its LED/effect is not latched after reconnect."
    },
    [pscustomobject]@{
        name = "Deck 1 shifted Beat Loop roll pad 1"
        action = "Select Deck 1 Beat Loop with SHIFT plus BEAT JUMP, then hold SHIFT plus performance pad 1, disconnect FLX4 while both are held, and release them only after the cable is out."
        acceptance = "The momentary roll and SHIFT state are both released after reconnect."
    }
)

function Test-CleanAudioHealth {
    param($Snapshot)
    return -not $Snapshot.uac_data_loss -and
        $Snapshot.uac_flags -eq 0 -and
        $Snapshot.pcm1 -eq 0 -and
        $Snapshot.pcm2 -eq 0 -and
        $Snapshot.output_late -eq 0 -and
        -not $Snapshot.twdt_current
}

function Invoke-SeekZero {
    param([ValidateRange(1, 2)][int]$Deck)
    $headers = @{ "X-DDJ-Control" = "1" }
    Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/control?deck=$Deck&action=seek&value=0" `
        -TimeoutSec 5 | Out-Null
}

function Reset-DualPlaybackPosition {
    Invoke-SeekZero -Deck 1
    Invoke-SeekZero -Deck 2
    Start-Sleep -Seconds 2
    $snapshot = Get-DeviceSnapshot -Name "after_batch_seek"
    if (-not $snapshot.deck1_playing -or -not $snapshot.deck2_playing) {
        throw "Batch seek did not retain dual-deck playback"
    }
    if (-not (Test-CleanAudioHealth -Snapshot $snapshot)) {
        throw "Audio health is not clean after batch seek"
    }
    return $snapshot
}

function Start-DualPlaybackOnce {
    $library = Wait-LibraryCount -ExpectedCount 100
    $track1 = Get-TrackByKey -Library $library -TrackKey 3
    $track2 = Get-TrackByKey -Library $library -TrackKey 10

    Invoke-LoadPost -TrackKey 3 -Generation $library.generation -Deck 1
    [void](Wait-DeckReady -Deck 1 -ExpectedTitle ([string]$track1.title))
    Invoke-LoadPost -TrackKey 10 -Generation $library.generation -Deck 2
    [void](Wait-DeckReady -Deck 2 -ExpectedTitle ([string]$track2.title))
    Invoke-ControlPost -Deck 1 -Action "play_pause"
    Invoke-ControlPost -Deck 2 -Action "play_pause"
    Start-Sleep -Seconds 3

    $start = Get-DeviceSnapshot -Name "batch_playback_start"
    Start-Sleep -Seconds $PlaybackSeconds
    $finish = Get-DeviceSnapshot -Name "batch_playback_ready"
    $failures = @(Get-PlaybackFailures -Start $start -Finish $finish `
        -Seconds $PlaybackSeconds)
    if ($failures.Count -ne 0) {
        throw "Initial batch playback failed: $($failures -join '; ')"
    }
    return [pscustomobject][ordered]@{
        library_generation = $library.generation
        library_count = $library.count
        track1_key = 3
        track1_title = [string]$track1.title
        track2_key = 10
        track2_title = [string]$track2.title
        start = $start
        finish = $finish
    }
}

function Get-IjReconnectFailures {
    param(
        $Baseline,
        $Absent,
        $Recovered,
        $PlaybackStart,
        $PlaybackFinish,
        $AfterOperator,
        $BaselineEvents,
        $FinalEvents,
        [uint32]$InitialBoot,
        [uint32]$FinalBoot,
        [string]$OperatorConfirmation
    )
    $failures = New-Object 'System.Collections.Generic.List[string]'

    if (-not $Absent.storage_mounted -or $Absent.controller_present) {
        Add-Failure $failures "FLX4-absent snapshot did not retain USB0 alone"
    }
    if (-not $Absent.deck1_playing -or -not $Absent.deck2_playing) {
        Add-Failure $failures "dual playback stopped while FLX4 was absent"
    }
    if (-not (Test-DeviceState -Snapshot $Recovered `
            -StoragePresent $true -ControllerPresent $true)) {
        Add-Failure $failures "FLX4 profile/MIDI/UAC did not recover"
    }
    if (-not $Recovered.deck1_playing -or -not $Recovered.deck2_playing) {
        Add-Failure $failures "dual playback was not active after reconnect"
    }
    foreach ($failure in @(Get-PlaybackFailures -Start $PlaybackStart `
            -Finish $PlaybackFinish -Seconds $PlaybackSeconds)) {
        Add-Failure $failures ([string]$failure)
    }
    foreach ($failure in @(Get-CycleFailures -Baseline $Baseline `
            -Final $AfterOperator -MinimumRecoveryCount 0 `
            -MaximumRecoveryCount 2 -ExpectedStorageDisconnects 0 `
            -ExpectedControllerDisconnects 1 -ExpectedStorageReleases 0 `
            -MinimumControllerFaultRecoveryEpochs 0 `
            -MaximumControllerFaultRecoveryEpochs 1)) {
        Add-Failure $failures ([string]$failure)
    }
    if ((Get-CounterDelta $AfterOperator.controller_connects `
            $Baseline.controller_connects) -ne 1) {
        Add-Failure $failures "controller_connects delta is not exactly one"
    }
    if ($OperatorConfirmation -notin @("yes", "y", "da")) {
        Add-Failure $failures "operator did not accept audio/control/LED check"
    }
    if (-not $AfterOperator.deck1_playing -or
        -not $AfterOperator.deck2_playing) {
        Add-Failure $failures "physical double PLAY/PAUSE did not return both decks to playing"
    }
    if ((Get-CounterDelta $AfterOperator.controller_midi_packets `
            $PlaybackFinish.controller_midi_packets) -lt 2) {
        Add-Failure $failures "operator check produced fewer than two MIDI packets"
    }
    if ((Get-CounterDelta $AfterOperator.semantic_events `
            $PlaybackFinish.semantic_events) -lt 2) {
        Add-Failure $failures "operator check produced fewer than two semantic events"
    }
    if ($FinalBoot -ne $InitialBoot) {
        Add-Failure $failures "device rebooted during the reconnect"
    }

    $eventDeltas = Get-GatedEventDeltas -Baseline $BaselineEvents `
        -Final $FinalEvents
    foreach ($eventName in @("uac_data_loss", "audio_underrun",
                             "audio_output_late", "usb_unmounted")) {
        if ($eventDeltas.$eventName -ne 0) {
            Add-Failure $failures "$eventName event delta is $($eventDeltas.$eventName)"
        }
    }
    if ($eventDeltas.controller_disconnected -ne 1) {
        Add-Failure $failures "controller_disconnected event delta is $($eventDeltas.controller_disconnected), expected 1"
    }
    if ($eventDeltas.controller_connected -ne 1) {
        Add-Failure $failures "controller_connected event delta is $($eventDeltas.controller_connected), expected 1"
    }
    return [pscustomobject]@{
        failures = @($failures)
        event_deltas = $eventDeltas
    }
}

function Write-IjEvidence {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    }
    else {
        Join-Path $repoRoot $OutputDirectory
    }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stem = "{0}{1}-boot{2}" -f $Evidence.group, $Evidence.cycle,
        $Evidence.boot_epoch
    $jsonPath = Join-Path $outputRoot "$stem.json"
    $markdownPath = Join-Path $outputRoot "$stem.md"
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath `
        -Encoding utf8
    @(
        "# P4 lifecycle $($Evidence.group)$($Evidence.cycle) - boot $($Evidence.boot_epoch)",
        "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)`` on ``$($Evidence.firmware_slot)``",
        "- Scenario: $($Evidence.scenario)",
        "- Held control: $($Evidence.held_control)",
        "- Operator confirmation: $($Evidence.operator_confirmation)",
        "- D1/D2 post-reconnect advance: $($Evidence.playback.deck1_advance_ms)/$($Evidence.playback.deck2_advance_ms) ms",
        "- UAC submitted delta: $($Evidence.playback.submitted_delta) blocks",
        "- Controller disconnect/connect: $($Evidence.cycle_deltas.controller_disconnects)/$($Evidence.cycle_deltas.controller_connects)",
        "- Failures: $(@($Evidence.failures).Count)",
        "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    ) | Set-Content -LiteralPath $markdownPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $markdownPath }
}

function Invoke-IjCycle {
    param(
        [ValidateSet("I", "J")][string]$Group,
        [ValidateRange(1, 5)][int]$Cycle,
        $HeldControl,
        [uint32]$BootEpoch
    )
    $scenario = if ($Group -eq "I") {
        "FLX4 disconnect/reconnect during dual-deck playback"
    }
    else {
        "FLX4 disconnect/reconnect during dual-deck playback with a held control"
    }
    $evidence = [ordered]@{
        schema = 1
        batch_schema = 1
        group = $Group
        cycle = $Cycle
        scenario = $scenario
        held_control = if ($HeldControl) { $HeldControl.name } else { "none" }
        started_utc = [DateTime]::UtcNow.ToString("o")
        expected_version = $ExpectedVersion
        boot_epoch = $BootEpoch
        result = "FAIL"
        failures = @()
    }

    try {
        [void](Reset-DualPlaybackPosition)
        $bootLog = @(Get-CurrentBootLog)
        $cycleBoot = Get-BootEpoch -BootLog $bootLog
        if ($cycleBoot -ne $BootEpoch) {
            throw "Boot epoch changed before $Group$Cycle"
        }
        $baselineEvents = Get-GatedEventCounts -BootLog $bootLog
        $baseline = Get-DeviceSnapshot -Name "${Group}${Cycle}_active_baseline"
        $evidence.firmware_version = $baseline.version
        $evidence.firmware_slot = $baseline.slot
        $evidence.baseline = $baseline
        $evidence.baseline_event_counts = $baselineEvents
        if ($baseline.version -ne $ExpectedVersion) {
            throw "Expected $ExpectedVersion, device runs $($baseline.version)"
        }
        if (-not (Test-DeviceState -Snapshot $baseline `
                -StoragePresent $true -ControllerPresent $true) -or
            -not $baseline.deck1_playing -or -not $baseline.deck2_playing) {
            throw "$Group$Cycle requires both devices healthy and both decks playing"
        }
        if (-not (Test-CleanAudioHealth -Snapshot $baseline)) {
            throw "$Group$Cycle audio baseline is not clean"
        }
        $library = Wait-LibraryCount -ExpectedCount 100
        $evidence.baseline_library_count = $library.count

        if ($Group -eq "I") {
            Request-OperatorStep "I${Cycle}: Disconnect FLX4 from USB1 now while both decks are playing. Leave USB0 connected and do not touch its cable."
        }
        else {
            Request-OperatorStep "J${Cycle}: $($HeldControl.action) Leave USB0 connected and do not touch its cable."
        }
        $absent = Wait-DeviceState -Name "${Group}${Cycle}_flx4_absent" `
            -StoragePresent $true -ControllerPresent $false
        $absentLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.after_disconnect = $absent
        $evidence.library_while_controller_absent_count = $absentLibrary.count

        Request-OperatorStep "$Group${Cycle}: Reconnect FLX4 to USB1. Leave USB0 connected and wait for profile, MIDI and UAC recovery."
        $recovered = Wait-DeviceState -Name "${Group}${Cycle}_recovered" `
            -StoragePresent $true -ControllerPresent $true
        $recoveredLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.after_reconnect = $recovered
        $evidence.library_after_reconnect_count = $recoveredLibrary.count

        Start-Sleep -Seconds 3
        $playbackStart = Get-DeviceSnapshot `
            -Name "${Group}${Cycle}_post_reconnect_start"
        Start-Sleep -Seconds $PlaybackSeconds
        $playbackFinish = Get-DeviceSnapshot `
            -Name "${Group}${Cycle}_post_reconnect_finish"
        $evidence.playback = [ordered]@{
            start = $playbackStart
            finish = $playbackFinish
            deck1_advance_ms = Get-CounterDelta `
                $playbackFinish.deck1_position_ms $playbackStart.deck1_position_ms
            deck2_advance_ms = Get-CounterDelta `
                $playbackFinish.deck2_position_ms $playbackStart.deck2_position_ms
            submitted_delta = Get-CounterDelta `
                $playbackFinish.submitted_blocks $playbackStart.submitted_blocks
        }

        if ($Group -eq "I") {
            Write-Host "AUDIO_CONFIRMATION_REQUIRED: Confirm audible MAIN and FLX4 cue, normal LEDs and controls, then press Deck 1 PLAY/PAUSE twice."
        }
        else {
            Write-Host "HELD_CONTROL_CONFIRMATION_REQUIRED: $($HeldControl.acceptance) Confirm audible MAIN/cue and normal LEDs/controls, then press Deck 1 PLAY/PAUSE twice."
        }
        Write-Host "Type yes only if every requested check passes."
        $operator = (Read-Host).Trim().ToLowerInvariant()
        $afterOperator = Get-DeviceSnapshot `
            -Name "${Group}${Cycle}_after_operator"
        $finalBootLog = @(Get-CurrentBootLog)
        $finalBoot = Get-BootEpoch -BootLog $finalBootLog
        $finalEvents = Get-GatedEventCounts -BootLog $finalBootLog
        $assessment = Get-IjReconnectFailures -Baseline $baseline `
            -Absent $absent -Recovered $recovered `
            -PlaybackStart $playbackStart -PlaybackFinish $playbackFinish `
            -AfterOperator $afterOperator -BaselineEvents $baselineEvents `
            -FinalEvents $finalEvents -InitialBoot $BootEpoch `
            -FinalBoot $finalBoot -OperatorConfirmation $operator

        $evidence.operator_confirmation = $operator
        $evidence.after_operator_check = $afterOperator
        $evidence.cycle_deltas = Get-CycleDeltas -Baseline $baseline `
            -Final $afterOperator
        $evidence.event_deltas = $assessment.event_deltas
        $evidence.boot_log = $finalBootLog
        $evidence.failures = @($assessment.failures)
        $evidence.result = if ($evidence.failures.Count -eq 0) {
            "PASS"
        }
        else {
            "FAIL"
        }
    }
    catch {
        $evidence.failures = @([string]$_.Exception.Message)
        $evidence.error = [string]$_
        if (-not $evidence.Contains("firmware_version")) {
            $evidence.firmware_version = "unknown"
            $evidence.firmware_slot = "unknown"
        }
        if (-not $evidence.Contains("operator_confirmation")) {
            $evidence.operator_confirmation = "not reached"
        }
        if (-not $evidence.Contains("playback")) {
            $evidence.playback = [ordered]@{
                deck1_advance_ms = 0
                deck2_advance_ms = 0
                submitted_delta = 0
            }
        }
        if (-not $evidence.Contains("cycle_deltas")) {
            $evidence.cycle_deltas = [ordered]@{
                controller_disconnects = 0
                controller_connects = 0
            }
        }
    }
    finally {
        $evidence.finished_utc = [DateTime]::UtcNow.ToString("o")
        $paths = Write-IjEvidence -Evidence ([pscustomobject]$evidence)
        Write-Host "EVIDENCE_JSON=$($paths.json)"
        Write-Host "EVIDENCE_MARKDOWN=$($paths.markdown)"
        Write-Host "RESULT=$($evidence.result)"
    }
    if ($evidence.result -ne "PASS") {
        throw "$Group$Cycle failed: $($evidence.failures -join '; ')"
    }
    return [pscustomobject]$evidence
}

function Invoke-IjSelfTest {
    $baseline = [pscustomobject]@{
        storage_mounted = $true; controller_present = $true
        root_power_mask = [uint32]3; controller_profile = "active"
        controller_midi_in = $true; controller_midi_out = $true
        controller_usb_audio = $true; controller_accepting_midi_out = $true
        deck1_playing = $true; deck2_playing = $true
        version = "test"; slot = "ota_0"; ota_state = "idle"; ota_error = ""
        storage_disconnects = [uint64]0; storage_releases = [uint64]0
        storage_last_mount_result = 0; controller_disconnects = [uint64]0
        controller_connects = [uint64]0; controller_midi_packets = [uint64]0
        semantic_events = [uint64]0; controller_fault_recovery_epochs = [uint64]0
        topology_probe_failures = [uint64]0; controller_interface_claim_failures = [uint64]0
        controller_transfer_alloc_failures = [uint64]0; controller_probe_event_drops = [uint64]0
        recovery_requests = [uint64]0; recovery_successes = [uint64]0
        recovery_failures = [uint64]0; recovery_queue_drops = [uint64]0
        daemon_errors = [uint64]0; runtime_queue_failures = [uint64]0
        service_log_dropped = [uint64]0; pcm1 = [uint64]0; pcm2 = [uint64]0
        output_late = [uint64]0; uac_data_loss = $false; uac_flags = [uint32]0
        twdt_current = $false; deck1_position_ms = [uint64]1000
        deck2_position_ms = [uint64]1000; submitted_blocks = [uint64]100
        dropped_blocks = [uint64]0; overflow_frames = [uint64]0
        underflow_frames = [uint64]0; packet_failures = [uint64]0
        packet_lost_frames = [uint64]0; ring_state = "nominal"
    }
    $absent = $baseline.psobject.Copy()
    $absent.controller_present = $false
    $recovered = $baseline.psobject.Copy()
    $recovered.controller_disconnects = [uint64]1
    $recovered.controller_connects = [uint64]1
    $playbackFinish = $recovered.psobject.Copy()
    $playbackFinish.deck1_position_ms = [uint64]6000
    $playbackFinish.deck2_position_ms = [uint64]6000
    $playbackFinish.submitted_blocks = [uint64]970
    $afterOperator = $playbackFinish.psobject.Copy()
    $afterOperator.controller_midi_packets = [uint64]4
    $afterOperator.semantic_events = [uint64]4
    $eventBase = [pscustomobject]@{
        uac_data_loss = [uint64]0; audio_underrun = [uint64]0
        audio_output_late = [uint64]0; usb_unmounted = [uint64]0
        controller_connected = [uint64]0; controller_disconnected = [uint64]0
    }
    $eventFinal = $eventBase.psobject.Copy()
    $eventFinal.controller_connected = [uint64]1
    $eventFinal.controller_disconnected = [uint64]1
    $good = Get-IjReconnectFailures -Baseline $baseline -Absent $absent `
        -Recovered $recovered -PlaybackStart $recovered `
        -PlaybackFinish $playbackFinish -AfterOperator $afterOperator `
        -BaselineEvents $eventBase -FinalEvents $eventFinal `
        -InitialBoot 1 -FinalBoot 1 -OperatorConfirmation "yes"
    if (@($good.failures).Count -ne 0) {
        throw "good I/J reconnect self-test failed: $($good.failures -join '; ')"
    }
    $badEvents = $eventFinal.psobject.Copy()
    $badEvents.uac_data_loss = [uint64]1
    $bad = Get-IjReconnectFailures -Baseline $baseline -Absent $absent `
        -Recovered $recovered -PlaybackStart $recovered `
        -PlaybackFinish $playbackFinish -AfterOperator $afterOperator `
        -BaselineEvents $eventBase -FinalEvents $badEvents `
        -InitialBoot 1 -FinalBoot 1 -OperatorConfirmation "yes"
    if (@($bad.failures).Count -ne 1) {
        throw "I/J UAC event rejection self-test failed"
    }
    Write-Output "P4 lifecycle I/J batch self-test passed"
}

if ($ijSelfTest) {
    Invoke-IjSelfTest
    exit 0
}
if (-not $ExpectedVersion) {
    throw "-ExpectedVersion is required for hardware evidence"
}
if ($EndPair -lt $StartPair) {
    throw "-EndPair must be greater than or equal to -StartPair"
}

$batch = [ordered]@{
    schema = 1
    started_utc = [DateTime]::UtcNow.ToString("o")
    expected_version = $ExpectedVersion
    start_pair = $StartPair
    end_pair = $EndPair
    result = "FAIL"
    cycles = @()
    failures = @()
}

try {
    $initialBootLog = @(Get-CurrentBootLog)
    $bootEpoch = Get-BootEpoch -BootLog $initialBootLog
    $initial = Get-DeviceSnapshot -Name "ij_batch_idle_baseline"
    if ($initial.version -ne $ExpectedVersion) {
        throw "Expected $ExpectedVersion, device runs $($initial.version)"
    }
    if (-not (Test-DeviceState -Snapshot $initial `
            -StoragePresent $true -ControllerPresent $true)) {
        throw "I/J batch requires USB0 and FLX4 healthy"
    }
    if ($initial.deck1_playing -or $initial.deck2_playing) {
        throw "I/J batch requires both decks stopped at startup"
    }
    if (-not (Test-CleanAudioHealth -Snapshot $initial)) {
        throw "I/J batch requires a clean boot-level audio baseline"
    }
    [void](Wait-LibraryCount -ExpectedCount 100)
    $batch.boot_epoch = $bootEpoch
    $batch.firmware_version = $initial.version
    $batch.firmware_slot = $initial.slot
    $batch.initial = $initial
    $batch.playback_setup = Start-DualPlaybackOnce

    $results = New-Object 'System.Collections.Generic.List[object]'
    for ($pair = $StartPair; $pair -le $EndPair; $pair++) {
        $results.Add((Invoke-IjCycle -Group I -Cycle $pair `
            -HeldControl $null -BootEpoch $bootEpoch))
        $results.Add((Invoke-IjCycle -Group J -Cycle $pair `
            -HeldControl $heldControls[$pair - 1] -BootEpoch $bootEpoch))
    }
    $batch.cycles = @($results)
    $batch.result = "PASS"
}
catch {
    $batch.failures = @([string]$_.Exception.Message)
    $batch.error = [string]$_
    throw
}
finally {
    try { Stop-Decks } catch { }
    $batch.finished_utc = [DateTime]::UtcNow.ToString("o")
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    }
    else {
        Join-Path $repoRoot $OutputDirectory
    }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $bootSuffix = if ($batch.Contains("boot_epoch")) { $batch.boot_epoch } else { 0 }
    $batchPath = Join-Path $outputRoot "IJ-batch-boot$bootSuffix.json"
    [pscustomobject]$batch | ConvertTo-Json -Depth 14 |
        Set-Content -LiteralPath $batchPath -Encoding utf8
    Write-Host "BATCH_EVIDENCE_JSON=$batchPath"
    Write-Host "BATCH_RESULT=$($batch.result)"
}
