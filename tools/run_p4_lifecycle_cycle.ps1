param(
    [ValidateSet("C", "D", "E", "F", "G", "H")]
    [string]$Group,

    [ValidateRange(1, 5)]
    [int]$Cycle = 1,

    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,

    [ValidateRange(5, 60)]
    [int]$PlaybackSeconds = 10,

    [ValidateRange(5, 120)]
    [int]$DeviceTimeoutSeconds = 30,

    [string]$OutputDirectory = "tmp/p4-lifecycle",
    [switch]$SelfTest,
    [switch]$DefineOnly
)

# Guided hardware harness for Groups C--H of the P4 dual-USB lifecycle
# matrix. The operator performs only the physical cable actions and listening
# check. The harness owns API polling, exact-image checks, counter deltas,
# dual-deck playback, cleanup and durable local evidence.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"

$repoRoot = Split-Path -Parent $PSScriptRoot

function Get-CounterDelta {
    param([uint64]$Current, [uint64]$Previous)
    if ($Current -ge $Previous) {
        return [uint64]($Current - $Previous)
    }
    return [uint64]0
}

function Add-Failure {
    param(
        [System.Collections.Generic.List[string]]$Failures,
        [string]$Message
    )
    $Failures.Add($Message)
}

function Invoke-ApiJson {
    param(
        [string]$Path,
        [int]$Attempts = 5
    )
    $lastError = $null
    for ($attempt = 1; $attempt -le $Attempts; $attempt++) {
        try {
            $response = Invoke-WebRequest -UseBasicParsing `
                -Uri "$BaseUri$Path" -TimeoutSec 5
            return $response.Content | ConvertFrom-Json
        }
        catch {
            $lastError = $_
            if ($attempt -lt $Attempts) {
                Start-Sleep -Milliseconds 500
            }
        }
    }
    throw $lastError
}

function Invoke-ControlPost {
    param([int]$Deck, [string]$Action)
    $headers = @{ "X-DDJ-Control" = "1" }
    Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/control?deck=$Deck&action=$Action" `
        -TimeoutSec 5 | Out-Null
}

function Invoke-ValidationGatePost {
    param([ValidateSet("arm", "cancel")][string]$Action)
    $headers = @{ "X-DDJ-Control" = "1" }
    $response = Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/validation/library-load-gate/$Action" `
        -TimeoutSec 5
    return $response.Content | ConvertFrom-Json
}

function Get-ValidationGate {
    return Invoke-ApiJson -Path "/api/validation/library-load-gate" -Attempts 3
}

function Invoke-AudioLoadValidationGatePost {
    param(
        [ValidateSet("arm", "cancel")][string]$Action,
        [ValidateRange(1, 2)][int]$Deck = 1
    )
    $headers = @{ "X-DDJ-Control" = "1" }
    $query = if ($Action -eq "arm") { "?deck=$Deck" } else { "" }
    $response = Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/validation/audio-load-gate/$Action$query" `
        -TimeoutSec 5
    return $response.Content | ConvertFrom-Json
}

function Get-AudioLoadValidationGate {
    return Invoke-ApiJson -Path "/api/validation/audio-load-gate" -Attempts 3
}

function Wait-AudioLoadValidationGateState {
    param(
        [string]$ExpectedState,
        [uint32]$ExpectedSequence,
        [int]$ExpectedDeck
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastState = "unavailable"
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $gate = Get-AudioLoadValidationGate
            $lastState = [string]$gate.state
            if ([uint32]$gate.sequence -eq $ExpectedSequence -and
                [int]$gate.deck -eq $ExpectedDeck -and
                $lastState -eq $ExpectedState) {
                return $gate
            }
            if ($lastState -in @("timed_out", "canceled")) {
                throw "Audio-load validation gate ended as $lastState"
            }
        }
        catch {
            if ($_.Exception.Message -like "Audio-load validation gate ended*") {
                throw
            }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for audio-load validation gate $ExpectedState (last=$lastState)"
}

function Wait-ValidationGateState {
    param(
        [string]$ExpectedState,
        [uint32]$ExpectedSequence
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastState = "unavailable"
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $gate = Get-ValidationGate
            $lastState = [string]$gate.state
            if ([uint32]$gate.sequence -eq $ExpectedSequence -and
                $lastState -eq $ExpectedState) {
                return $gate
            }
            if ($lastState -in @("timed_out", "canceled")) {
                throw "Validation gate ended as $lastState"
            }
        }
        catch {
            if ($_.Exception.Message -like "Validation gate ended*") { throw }
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Timed out waiting for validation gate $ExpectedState (last=$lastState)"
}

function Invoke-LoadPost {
    param(
        [uint32]$TrackKey,
        [uint32]$Generation,
        [int]$Deck
    )
    $headers = @{ "X-DDJ-Control" = "1" }
    $lastError = $null
    for ($attempt = 1; $attempt -le 15; $attempt++) {
        try {
            Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
                -Uri "$BaseUri/api/load?track_key=$TrackKey&generation=$Generation&deck=$Deck" `
                -TimeoutSec 5 | Out-Null
            return
        }
        catch {
            $lastError = $_
            if ($attempt -lt 15) {
                Start-Sleep -Milliseconds 250
            }
        }
    }
    throw $lastError
}

function Get-CurrentBootLog {
    $response = Invoke-WebRequest -UseBasicParsing `
        -Uri "$BaseUri/api/diagnostic-log" -TimeoutSec 15
    $lines = @($response.Content -split "`r?`n")
    $lastHeader = -1
    for ($index = 0; $index -lt $lines.Count; $index++) {
        if ($lines[$index] -match '^schema=1 boot=([0-9]+) ') {
            $lastHeader = $index
        }
    }
    if ($lastHeader -lt 0) {
        throw "Diagnostic log contains no boot header"
    }
    return @($lines[$lastHeader..($lines.Count - 1)])
}

function Get-BootEpoch {
    param([string[]]$BootLog)
    if ($BootLog.Count -gt 0 -and $BootLog[0] -match '^schema=1 boot=([0-9]+) ') {
        return [uint32]$Matches[1]
    }
    throw "Cannot parse boot epoch"
}

function Get-LibraryInfo {
    $library = Invoke-ApiJson -Path "/api/library" -Attempts 3
    return [pscustomobject]@{
        generation = [uint32]$library.generation
        count = @($library.tracks).Count
        tracks = @($library.tracks)
    }
}

function Get-TrackByKey {
    param($Library, [uint32]$TrackKey)
    foreach ($track in $Library.tracks) {
        if ([uint32]$track.track_key -eq $TrackKey) {
            return $track
        }
    }
    throw "Track key $TrackKey is missing from the Library"
}

function Get-DeviceSnapshot {
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
        storage_connect_events = [uint64]$status.p4_usb.storage.connect_events
        storage_connects = [uint64]$status.p4_usb.storage.connect_accepted
        storage_disconnects = [uint64]$status.p4_usb.storage.disconnect_accepted
        storage_mount_attempts = [uint64]$status.p4_usb.storage.mount_attempts
        storage_mount_successes = [uint64]$status.p4_usb.storage.mount_successes
        storage_releases = [uint64]$status.p4_usb.storage.releases
        storage_last_mount_result = [int]$status.p4_usb.storage.last_mount_result
        controller_present = [bool]$status.controller.present
        controller_profile = [string]$status.controller.profile_state
        controller_midi_in = [bool]$status.controller.midi_in
        controller_midi_out = [bool]$status.controller.midi_out
        controller_usb_audio = [bool]$status.controller.usb_audio
        controller_midi_packets = [uint64]$status.p4_usb.controller.midi_packets
        controller_connects = [uint64]$status.p4_usb.controller.midi_connects
        controller_disconnects = [uint64]$status.p4_usb.controller.midi_disconnects
        controller_interface_claim_failures = [uint64]$status.p4_usb.controller.interface_claim_failures
        controller_transfer_alloc_failures = [uint64]$status.p4_usb.controller.transfer_alloc_failures
        controller_probe_event_drops = [uint64]$status.p4_usb.controller.probe_event_drops
        controller_fault_recovery_epochs = [uint64]$status.p4_usb.controller.fault_recovery_epochs
        controller_accepting_midi_out = [bool]$status.p4_usb.controller.accepting_midi_out
        semantic_events = [uint64]$status.p4_usb.runtime.semantic_events
        root_power_mask = [uint32]$status.p4_usb.host.root_power_mask
        topology_probe_failures = [uint64]$status.p4_usb.topology.probe_failures
        recovery_requests = [uint64]$status.p4_usb.host.recovery_requests
        recovery_successes = [uint64]$status.p4_usb.host.recovery_successes
        recovery_failures = [uint64]$status.p4_usb.host.recovery_failures
        recovery_queue_drops = [uint64]$status.p4_usb.host.recovery_queue_drops
        daemon_errors = [uint64]$status.p4_usb.host.daemon_errors
        runtime_queue_failures = [uint64]$status.p4_usb.runtime.queue_failures
        service_log_dropped = [uint64]$status.service_log.dropped
        deck1_title = [string]$status.deck1.title
        deck2_title = [string]$status.deck2.title
        deck1_state = [string]$status.deck1.state_text
        deck2_state = [string]$status.deck2.state_text
        deck1_playing = [bool]$status.deck1.playing
        deck2_playing = [bool]$status.deck2.playing
        deck1_position_ms = [uint64]$status.deck1.position_ms
        deck2_position_ms = [uint64]$status.deck2.position_ms
        submitted_blocks = [uint64]$status.diagnostics.usb_headphones.submitted_blocks
        dropped_blocks = [uint64]$status.diagnostics.usb_headphones.dropped_blocks
        overflow_frames = [uint64]$status.diagnostics.usb_headphones.overflow_frames
        underflow_frames = [uint64]$status.diagnostics.usb_headphones.underflow_frames
        packet_failures = [uint64]$status.diagnostics.usb_headphones.packet_failures
        packet_lost_frames = [uint64]$status.diagnostics.usb_headphones.packet_lost_frames
        ring_frames = [uint32]$status.diagnostics.usb_headphones.ring_queued_frames
        ring_state = [string]$status.diagnostics.usb_headphones.ring_state
        uac_data_loss = [bool]$status.diagnostics.usb_headphones.data_loss
        uac_flags = [uint32]$status.diagnostics.usb_headphones.data_loss_flags
        pcm1 = [uint64]$status.diagnostics.pcm_underrun1
        pcm2 = [uint64]$status.diagnostics.pcm_underrun2
        output_late = [uint64]$status.diagnostics.output_late_count
        twdt_current = [bool]$status.audio_wdt_trace.current.twdt_isr_seen
    }
}

function Test-DeviceState {
    param(
        $Snapshot,
        [bool]$StoragePresent,
        [bool]$ControllerPresent
    )
    if ($Snapshot.storage_mounted -ne $StoragePresent) {
        return $false
    }
    if ($Snapshot.controller_present -ne $ControllerPresent) {
        return $false
    }
    if ($Snapshot.root_power_mask -ne 3) {
        return $false
    }
    if ($ControllerPresent) {
        return $Snapshot.controller_profile -eq "active" -and
            $Snapshot.controller_midi_in -and
            $Snapshot.controller_midi_out -and
            $Snapshot.controller_usb_audio -and
            $Snapshot.controller_accepting_midi_out
    }
    return $true
}

function Wait-DeviceState {
    param(
        [string]$Name,
        [bool]$StoragePresent,
        [bool]$ControllerPresent
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $last = $null
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $last = Get-DeviceSnapshot -Name $Name
            if (Test-DeviceState -Snapshot $last `
                    -StoragePresent $StoragePresent `
                    -ControllerPresent $ControllerPresent) {
                return $last
            }
        }
        catch {
            # A bounded HTTP outage is expected around recovery transitions.
        }
        Start-Sleep -Milliseconds 250
    }
    if ($null -eq $last) {
        throw "Timed out waiting for $Name; no status snapshot was available"
    }
    throw "Timed out waiting for $Name (storage=$($last.storage_mounted), controller=$($last.controller_present), profile=$($last.controller_profile))"
}

function Wait-DeckReady {
    param(
        [int]$Deck,
        [string]$ExpectedTitle
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 10) {
        $snapshot = Get-DeviceSnapshot -Name "load_d$Deck"
        $state = if ($Deck -eq 1) { $snapshot.deck1_state } else { $snapshot.deck2_state }
        $title = if ($Deck -eq 1) { $snapshot.deck1_title } else { $snapshot.deck2_title }
        if ($state -eq "READY" -and $title -eq $ExpectedTitle) {
            return $snapshot
        }
        Start-Sleep -Milliseconds 100
    }
    throw "Deck $Deck did not load '$ExpectedTitle'"
}

function Request-OperatorStep {
    param([string]$Message)
    Write-Host "ACTION_REQUIRED: $Message"
    Write-Host "Press Enter here only after the physical action is complete."
    [void](Read-Host)
}

function Stop-Decks {
    $snapshot = Get-DeviceSnapshot -Name "before_cleanup"
    if ($snapshot.deck1_playing) {
        Invoke-ControlPost -Deck 1 -Action "play_pause"
    }
    if ($snapshot.deck2_playing) {
        Invoke-ControlPost -Deck 2 -Action "play_pause"
    }
    Start-Sleep -Milliseconds 500
}

function Get-PlaybackFailures {
    param($Start, $Finish, [int]$Seconds)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $minimumAdvance = [uint64]($Seconds * 800)
    if (-not $Start.deck1_playing -or -not $Start.deck2_playing -or
        -not $Finish.deck1_playing -or -not $Finish.deck2_playing) {
        Add-Failure $failures "both decks were not continuously playing"
    }
    if ((Get-CounterDelta $Finish.deck1_position_ms $Start.deck1_position_ms) -lt $minimumAdvance) {
        Add-Failure $failures "Deck 1 position did not advance enough"
    }
    if ((Get-CounterDelta $Finish.deck2_position_ms $Start.deck2_position_ms) -lt $minimumAdvance) {
        Add-Failure $failures "Deck 2 position did not advance enough"
    }
    if ((Get-CounterDelta $Finish.submitted_blocks $Start.submitted_blocks) -eq 0) {
        Add-Failure $failures "UAC submitted no blocks"
    }
    foreach ($field in @("dropped_blocks", "overflow_frames", "underflow_frames",
                         "packet_failures", "packet_lost_frames", "pcm1", "pcm2",
                         "output_late", "controller_disconnects", "storage_disconnects",
                         "daemon_errors", "recovery_failures", "recovery_queue_drops",
                         "runtime_queue_failures", "service_log_dropped")) {
        if ((Get-CounterDelta $Finish.$field $Start.$field) -ne 0) {
            Add-Failure $failures "$field increased during active playback"
        }
    }
    if ($Finish.ring_state -ne "nominal") {
        Add-Failure $failures "UAC ring ended in '$($Finish.ring_state)' state"
    }
    if ($Finish.uac_data_loss -or $Finish.uac_flags -ne 0) {
        Add-Failure $failures "active UAC data-loss state is set"
    }
    if ($Finish.twdt_current) {
        Add-Failure $failures "current TWDT ISR flag is set"
    }
    return @($failures)
}

function Get-CycleFailures {
    param(
        $Baseline,
        $Final,
        [uint64]$MinimumRecoveryCount,
        [uint64]$MaximumRecoveryCount,
        [bool]$EnforceRecoveryRange = $true,
        [uint64]$ExpectedStorageDisconnects,
        [uint64]$ExpectedControllerDisconnects,
        [uint64]$ExpectedStorageReleases,
        [uint64]$MinimumControllerFaultRecoveryEpochs = 0,
        [uint64]$MaximumControllerFaultRecoveryEpochs = 0
    )
    $failures = New-Object 'System.Collections.Generic.List[string]'
    $strictCounters = @(
        "topology_probe_failures",
        "controller_interface_claim_failures",
        "controller_transfer_alloc_failures",
        "controller_probe_event_drops",
        "daemon_errors",
        "recovery_failures",
        "recovery_queue_drops",
        "runtime_queue_failures",
        "service_log_dropped",
        "pcm1",
        "pcm2",
        "output_late"
    )
    foreach ($field in $strictCounters) {
        $delta = Get-CounterDelta $Final.$field $Baseline.$field
        if ($delta -ne 0) {
            Add-Failure $failures "$field increased by $delta during the cycle"
        }
    }
    foreach ($expectation in @(
            @{ Field = "storage_disconnects"; Expected = $ExpectedStorageDisconnects },
            @{ Field = "controller_disconnects"; Expected = $ExpectedControllerDisconnects },
            @{ Field = "storage_releases"; Expected = $ExpectedStorageReleases })) {
        $delta = Get-CounterDelta $Final.($expectation.Field) $Baseline.($expectation.Field)
        if ($delta -ne $expectation.Expected) {
            Add-Failure $failures "$($expectation.Field) delta is $delta, expected $($expectation.Expected)"
        }
    }
    $faultEpochDelta = Get-CounterDelta $Final.controller_fault_recovery_epochs `
        $Baseline.controller_fault_recovery_epochs
    if ($faultEpochDelta -lt $MinimumControllerFaultRecoveryEpochs -or
        $faultEpochDelta -gt $MaximumControllerFaultRecoveryEpochs) {
        Add-Failure $failures "controller_fault_recovery_epochs delta is $faultEpochDelta, expected $MinimumControllerFaultRecoveryEpochs-$MaximumControllerFaultRecoveryEpochs"
    }
    if ($Final.version -ne $Baseline.version -or $Final.slot -ne $Baseline.slot) {
        Add-Failure $failures "firmware identity changed during the cycle"
    }
    if ($Final.ota_state -ne "idle" -or $Final.ota_error) {
        Add-Failure $failures "final OTA state is not clean and idle"
    }
    if ($Final.uac_data_loss -or $Final.uac_flags -ne 0) {
        Add-Failure $failures "final active UAC data-loss state is set"
    }
    if ($Final.twdt_current) {
        Add-Failure $failures "final current TWDT ISR flag is set"
    }
    $recoveryRequestDelta = Get-CounterDelta $Final.recovery_requests $Baseline.recovery_requests
    $recoverySuccessDelta = Get-CounterDelta $Final.recovery_successes $Baseline.recovery_successes
    if ($recoverySuccessDelta -ne $recoveryRequestDelta) {
        Add-Failure $failures "host recovery successes do not match requests: $recoveryRequestDelta request(s), $recoverySuccessDelta success(es)"
    }
    elseif ($EnforceRecoveryRange -and
            ($recoveryRequestDelta -lt $MinimumRecoveryCount -or
             $recoveryRequestDelta -gt $MaximumRecoveryCount)) {
        Add-Failure $failures "expected $MinimumRecoveryCount-$MaximumRecoveryCount bounded host recoveries with matching successes, observed $recoveryRequestDelta request(s) and $recoverySuccessDelta success(es)"
    }
    if ($Final.storage_last_mount_result -ne 0) {
        Add-Failure $failures "final storage mount result is $($Final.storage_last_mount_result)"
    }
    return @($failures)
}

function Get-CycleDeltas {
    param($Baseline, $Final)
    $deltas = [ordered]@{}
    foreach ($field in @(
            "storage_connect_events", "storage_connects", "storage_disconnects",
            "storage_mount_attempts", "storage_mount_successes", "storage_releases",
            "controller_connects", "controller_disconnects", "controller_midi_packets", "semantic_events",
            "topology_probe_failures", "controller_interface_claim_failures",
            "controller_transfer_alloc_failures", "controller_probe_event_drops",
            "controller_fault_recovery_epochs", "recovery_requests",
            "recovery_successes", "recovery_failures", "recovery_queue_drops",
            "daemon_errors", "runtime_queue_failures", "service_log_dropped",
            "pcm1", "pcm2", "output_late")) {
        $deltas[$field] = Get-CounterDelta $Final.$field $Baseline.$field
    }
    return [pscustomobject]$deltas
}

function Invoke-DualPlaybackSmoke {
    param($StableSnapshot)
    $library = Get-LibraryInfo
    if ($library.count -lt 2) {
        throw "Library has fewer than two tracks"
    }
    $track1 = Get-TrackByKey -Library $library -TrackKey 3
    $track2 = Get-TrackByKey -Library $library -TrackKey 10

    Invoke-LoadPost -TrackKey 3 -Generation $library.generation -Deck 1
    [void](Wait-DeckReady -Deck 1 -ExpectedTitle ([string]$track1.title))
    Invoke-LoadPost -TrackKey 10 -Generation $library.generation -Deck 2
    [void](Wait-DeckReady -Deck 2 -ExpectedTitle ([string]$track2.title))

    Invoke-ControlPost -Deck 1 -Action "play_pause"
    Invoke-ControlPost -Deck 2 -Action "play_pause"
    Start-Sleep -Seconds 3
    $start = Get-DeviceSnapshot -Name "playback_start"
    Start-Sleep -Seconds $PlaybackSeconds
    $finish = Get-DeviceSnapshot -Name "playback_finish"
    $failures = @(Get-PlaybackFailures -Start $start -Finish $finish `
        -Seconds $PlaybackSeconds)

    [pscustomobject][ordered]@{
        library_generation = $library.generation
        library_count = $library.count
        track1_key = 3
        track1_title = [string]$track1.title
        track2_key = 10
        track2_title = [string]$track2.title
        start = $start
        finish = $finish
        deck1_advance_ms = Get-CounterDelta $finish.deck1_position_ms $start.deck1_position_ms
        deck2_advance_ms = Get-CounterDelta $finish.deck2_position_ms $start.deck2_position_ms
        submitted_delta = Get-CounterDelta $finish.submitted_blocks $start.submitted_blocks
        failures = $failures
    }
}

function Get-EventCount {
    param([string[]]$BootLog, [string]$Event)
    return @($BootLog | Select-String "event=$Event(?: |$)").Count
}

function Get-GatedEventCounts {
    param([string[]]$BootLog)
    return [pscustomobject][ordered]@{
        uac_data_loss = Get-EventCount $BootLog "UAC_DATA_LOSS"
        audio_underrun = Get-EventCount $BootLog "AUDIO_UNDERRUN"
        audio_output_late = Get-EventCount $BootLog "AUDIO_OUTPUT_LATE"
        usb_unmounted = Get-EventCount $BootLog "USB_UNMOUNTED"
        controller_connected = Get-EventCount $BootLog "CONTROLLER_CONNECTED"
        controller_disconnected = Get-EventCount $BootLog "CONTROLLER_DISCONNECTED"
    }
}

function Get-GatedEventDeltas {
    param($Baseline, $Final)
    return [pscustomobject][ordered]@{
        uac_data_loss = Get-CounterDelta $Final.uac_data_loss $Baseline.uac_data_loss
        audio_underrun = Get-CounterDelta $Final.audio_underrun $Baseline.audio_underrun
        audio_output_late = Get-CounterDelta $Final.audio_output_late $Baseline.audio_output_late
        usb_unmounted = Get-CounterDelta $Final.usb_unmounted $Baseline.usb_unmounted
        controller_connected = Get-CounterDelta $Final.controller_connected $Baseline.controller_connected
        controller_disconnected = Get-CounterDelta $Final.controller_disconnected $Baseline.controller_disconnected
    }
}

function Wait-LibraryCount {
    param([int]$ExpectedCount)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastCount = -1
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $library = Get-LibraryInfo
            $lastCount = $library.count
            if ($lastCount -eq $ExpectedCount) {
                return $library
            }
        }
        catch {
            # A bounded HTTP or media transition is expected during unmount.
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for Library count $ExpectedCount (last=$lastCount)"
}

function Write-EvidenceFiles {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    }
    else {
        Join-Path $repoRoot $OutputDirectory
    }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stem = "{0}{1}-boot{2}" -f $Group, $Cycle, $Evidence.boot_epoch
    $jsonPath = Join-Path $outputRoot "$stem.json"
    $markdownPath = Join-Path $outputRoot "$stem.md"
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding utf8

    $playback = $Evidence.playback
    $lines = @(
        "# P4 lifecycle $Group$Cycle - boot $($Evidence.boot_epoch)",
        "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)`` on ``$($Evidence.firmware_slot)``",
        "- Scenario: $($Evidence.scenario)",
        "- Operator confirmation: $($Evidence.operator_confirmation)",
        "- D1/D2 advance: $($playback.deck1_advance_ms)/$($playback.deck2_advance_ms) ms",
        "- UAC submitted delta: $($playback.submitted_delta) blocks",
        "- Final ring: $($playback.finish.ring_frames) ($($playback.finish.ring_state))",
        "- UAC flags: $($playback.finish.uac_flags)",
        "- Full-cycle fault deltas: $(@($Evidence.cycle_failures).Count)",
        "- Failures: $(@($Evidence.failures).Count)",
        "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    )
    $lines | Set-Content -LiteralPath $markdownPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $markdownPath }
}

function Invoke-SelfTest {
    if ((Get-CounterDelta 20 10) -ne 10) { throw "delta forward test failed" }
    if ((Get-CounterDelta 1 10) -ne 0) { throw "delta reset test failed" }

    $base = [pscustomobject]@{
        deck1_playing = $true; deck2_playing = $true
        deck1_position_ms = [uint64]1000; deck2_position_ms = [uint64]2000
        submitted_blocks = [uint64]100
        dropped_blocks = [uint64]0; overflow_frames = [uint64]0
        underflow_frames = [uint64]50; packet_failures = [uint64]0
        packet_lost_frames = [uint64]0; pcm1 = [uint64]0; pcm2 = [uint64]0
        output_late = [uint64]0; controller_disconnects = [uint64]1
        storage_disconnects = [uint64]0; daemon_errors = [uint64]0
        recovery_failures = [uint64]0; recovery_queue_drops = [uint64]0
        runtime_queue_failures = [uint64]0; service_log_dropped = [uint64]0
        ring_state = "nominal"; uac_data_loss = $false; uac_flags = [uint32]0
        twdt_current = $false
    }
    $good = $base.psobject.Copy()
    $good.deck1_position_ms = [uint64]11000
    $good.deck2_position_ms = [uint64]12000
    $good.submitted_blocks = [uint64]1830
    if (@(Get-PlaybackFailures -Start $base -Finish $good -Seconds 10).Count -ne 0) {
        throw "good playback window failed"
    }
    $bad = $good.psobject.Copy()
    $bad.underflow_frames = [uint64]51
    if (@(Get-PlaybackFailures -Start $base -Finish $bad -Seconds 10).Count -ne 1) {
        throw "underflow failure test failed"
    }

    $cycleBase = [pscustomobject]@{
        storage_disconnects = [uint64]0; storage_releases = [uint64]0
        controller_disconnects = [uint64]0
        storage_last_mount_result = 0
        topology_probe_failures = [uint64]0
        controller_interface_claim_failures = [uint64]0
        controller_transfer_alloc_failures = [uint64]0
        controller_probe_event_drops = [uint64]0
        controller_fault_recovery_epochs = [uint64]0
        recovery_requests = [uint64]8; recovery_successes = [uint64]8
        daemon_errors = [uint64]0; recovery_failures = [uint64]0
        recovery_queue_drops = [uint64]0; runtime_queue_failures = [uint64]0
        service_log_dropped = [uint64]0; pcm1 = [uint64]0; pcm2 = [uint64]0
        output_late = [uint64]0; version = "test"; slot = "ota_0"
        ota_state = "idle"; ota_error = ""; uac_data_loss = $false
        uac_flags = [uint32]0; twdt_current = $false
    }
    $cycleGood = $cycleBase.psobject.Copy()
    $cycleGood.recovery_requests = [uint64]9
    $cycleGood.recovery_successes = [uint64]9
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleGood `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 0).Count -ne 0) {
        throw "good full-cycle window failed"
    }
    $cycleGoodD = $cycleGood.psobject.Copy()
    $cycleGoodD.recovery_requests = [uint64]10
    $cycleGoodD.recovery_successes = [uint64]10
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleGoodD `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 0).Count -ne 0) {
        throw "good Group D recovery window failed"
    }
    $cycleTooMany = $cycleGood.psobject.Copy()
    $cycleTooMany.recovery_requests = [uint64]11
    $cycleTooMany.recovery_successes = [uint64]11
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleTooMany `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 0).Count -ne 1) {
        throw "excess recovery test failed"
    }
    $cycleIncomplete = $cycleGood.psobject.Copy()
    $cycleIncomplete.recovery_successes = [uint64]8
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleIncomplete `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 0).Count -ne 1) {
        throw "incomplete recovery test failed"
    }
    $cycleBad = $cycleGood.psobject.Copy()
    $cycleBad.controller_probe_event_drops = [uint64]1
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleBad `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 0).Count -ne 1) {
        throw "full-cycle fault delta test failed"
    }
    $cycleE = $cycleBase.psobject.Copy()
    $cycleE.storage_disconnects = [uint64]1
    $cycleE.storage_releases = [uint64]1
    $cycleE.recovery_requests = [uint64]16
    $cycleE.recovery_successes = [uint64]16
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleE `
        -MinimumRecoveryCount 0 -MaximumRecoveryCount 1 `
            -EnforceRecoveryRange $false `
            -ExpectedStorageDisconnects 1 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 1).Count -ne 0) {
        throw "good Group E removal/reinsert window failed"
    }
    $cycleF = $cycleBase.psobject.Copy()
    $cycleF.storage_disconnects = [uint64]2
    $cycleF.storage_releases = [uint64]2
    $cycleF.recovery_requests = [uint64]18
    $cycleF.recovery_successes = [uint64]18
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleF `
            -MinimumRecoveryCount 0 -MaximumRecoveryCount 1 `
            -EnforceRecoveryRange $false `
            -ExpectedStorageDisconnects 2 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 2).Count -ne 0) {
        throw "good Group F Library-load removal window failed"
    }
    $cycleG = $cycleBase.psobject.Copy()
    $cycleG.storage_disconnects = [uint64]1
    $cycleG.storage_releases = [uint64]1
    $cycleG.recovery_requests = [uint64]16
    $cycleG.recovery_successes = [uint64]16
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleG `
            -MinimumRecoveryCount 0 -MaximumRecoveryCount 1 `
            -EnforceRecoveryRange $false `
            -ExpectedStorageDisconnects 1 -ExpectedControllerDisconnects 0 `
            -ExpectedStorageReleases 1).Count -ne 0) {
        throw "good Group G audio-load removal window failed"
    }
    $cycleH = $cycleBase.psobject.Copy()
    $cycleH.controller_disconnects = [uint64]1
    $cycleH.controller_fault_recovery_epochs = [uint64]1
    $cycleH.recovery_requests = [uint64]9
    $cycleH.recovery_successes = [uint64]9
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleH `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 1 `
            -ExpectedStorageReleases 0 `
            -MinimumControllerFaultRecoveryEpochs 0 `
            -MaximumControllerFaultRecoveryEpochs 1).Count -ne 0) {
        throw "good Group H FLX4 idle reconnect window failed"
    }
    $cycleHDuplicateEpoch = $cycleH.psobject.Copy()
    $cycleHDuplicateEpoch.controller_fault_recovery_epochs = [uint64]2
    if (@(Get-CycleFailures -Baseline $cycleBase -Final $cycleHDuplicateEpoch `
            -MinimumRecoveryCount 1 -MaximumRecoveryCount 2 `
            -ExpectedStorageDisconnects 0 -ExpectedControllerDisconnects 1 `
            -ExpectedStorageReleases 0 `
            -MinimumControllerFaultRecoveryEpochs 0 `
            -MaximumControllerFaultRecoveryEpochs 1).Count -ne 1) {
        throw "duplicate Group H controller fault epoch was not rejected"
    }
    Write-Output "P4 lifecycle harness self-test passed"
}

if ($DefineOnly) {
    return
}

if ($SelfTest) {
    Invoke-SelfTest
    exit 0
}

if (-not $Group) {
    throw "-Group C, -Group D, -Group E, -Group F, -Group G or -Group H is required"
}
if (-not $ExpectedVersion) {
    throw "-ExpectedVersion is required for hardware evidence"
}

$scenario = switch ($Group) {
    "C" { "boot empty, attach USB0 then USB1" }
    "D" { "boot empty, attach USB1 then USB0" }
    "E" { "both active, remove and reinsert idle USB0 while FLX4 remains active" }
    "F" { "remove and reinsert USB0 during deterministic Library load while FLX4 remains active" }
    "G" { "remove and reinsert USB0 after the first bounded audio-cache read while FLX4 remains active" }
    "H" { "disconnect and reconnect idle FLX4 while USB0 remains mounted" }
}

$evidence = [ordered]@{
    schema = 1
    group = $Group
    cycle = $Cycle
    scenario = $scenario
    started_utc = [DateTime]::UtcNow.ToString("o")
    expected_version = $ExpectedVersion
    result = "FAIL"
    failures = @()
}

try {
    $bootLog = @(Get-CurrentBootLog)
    $bootEpoch = Get-BootEpoch -BootLog $bootLog
    $baselineEventCounts = Get-GatedEventCounts -BootLog $bootLog
    $baselineName = if ($Group -in @("E", "F", "G", "H")) { "both_active_idle_baseline" } else { "empty_boot_baseline" }
    $baseline = Get-DeviceSnapshot -Name $baselineName
    $evidence.boot_epoch = $bootEpoch
    $evidence.firmware_version = $baseline.version
    $evidence.firmware_slot = $baseline.slot
    $evidence.baseline = $baseline
    $evidence.baseline_event_counts = $baselineEventCounts

    if ($baseline.version -ne $ExpectedVersion) {
        throw "Expected $ExpectedVersion, device runs $($baseline.version)"
    }
    if ($baseline.ota_state -ne "idle" -or $baseline.ota_error) {
        throw "OTA state is not a clean idle baseline"
    }
    $baselineStoragePresent = $Group -in @("E", "F", "G", "H")
    $baselineControllerPresent = $Group -in @("E", "F", "G", "H")
    if (-not (Test-DeviceState -Snapshot $baseline `
            -StoragePresent $baselineStoragePresent `
            -ControllerPresent $baselineControllerPresent)) {
        $requiredState = if ($Group -in @("E", "F", "G", "H")) { "both devices active and idle" } else { "an empty boot" }
        throw "Cycle must start with $requiredState (storage=$($baseline.storage_mounted), controller=$($baseline.controller_present))"
    }
    if ($baseline.deck1_playing -or $baseline.deck2_playing) {
        throw "Cycle baseline requires both decks stopped"
    }
    if ($baseline.uac_data_loss -or $baseline.uac_flags -ne 0 -or
        $baseline.pcm1 -ne 0 -or $baseline.pcm2 -ne 0 -or
        $baseline.output_late -ne 0 -or $baseline.twdt_current) {
        throw "Empty-boot health baseline is not clean"
    }

    if ($Group -eq "C") {
        Request-OperatorStep "Attach the Rekordbox USB stick to USB0; leave FLX4 disconnected."
        $afterFirst = Wait-DeviceState -Name "after_usb0" `
            -StoragePresent $true -ControllerPresent $false
        $libraryAfterFirst = Get-LibraryInfo
        if ($libraryAfterFirst.count -ne 100) {
            throw "USB0 mounted but Library count is $($libraryAfterFirst.count), expected 100"
        }
        Request-OperatorStep "Attach FLX4 to USB1; leave USB0 connected."
    }
    elseif ($Group -eq "D") {
        Request-OperatorStep "Attach FLX4 to USB1; leave USB0 disconnected."
        $afterFirst = Wait-DeviceState -Name "after_usb1" `
            -StoragePresent $false -ControllerPresent $true
        Request-OperatorStep "Attach the Rekordbox USB stick to USB0; leave FLX4 connected."
    }
    elseif ($Group -eq "E") {
        $baselineLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.baseline_library_count = $baselineLibrary.count
        Request-OperatorStep "Remove the Rekordbox USB stick from USB0; leave FLX4 connected and do not touch its controls."
        $afterFirst = Wait-DeviceState -Name "after_usb0_removal" `
            -StoragePresent $false -ControllerPresent $true
        $libraryAfterRemoval = Wait-LibraryCount -ExpectedCount 0
        $evidence.library_after_removal_count = $libraryAfterRemoval.count
        if ((Get-CounterDelta $afterFirst.controller_disconnects `
                $baseline.controller_disconnects) -ne 0) {
            throw "FLX4 disconnected while USB0 was removed"
        }
        Request-OperatorStep "Reinsert the same Rekordbox USB stick into USB0; leave FLX4 connected."
    }
    elseif ($Group -eq "F") {
        $baselineLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.baseline_library_count = $baselineLibrary.count

        Request-OperatorStep "Remove the Rekordbox USB stick from USB0 to prepare the controlled Library-load window; leave FLX4 connected."
        $afterPreparationRemoval = Wait-DeviceState `
            -Name "after_preparation_removal" `
            -StoragePresent $false -ControllerPresent $true
        $preparationLibrary = Wait-LibraryCount -ExpectedCount 0
        $evidence.after_preparation_removal = $afterPreparationRemoval
        $evidence.preparation_library_count = $preparationLibrary.count

        $armed = Invoke-ValidationGatePost -Action "arm"
        if ([string]$armed.state -ne "armed") {
            throw "Validation gate did not arm (state=$($armed.state))"
        }
        $gateSequence = [uint32]$armed.sequence
        $evidence.validation_gate_armed = $armed

        Request-OperatorStep "Reinsert USB0 to start the controlled Library load; leave FLX4 connected. The harness will tell you when to remove it again."
        $holding = Wait-ValidationGateState -ExpectedState "holding" `
            -ExpectedSequence $gateSequence
        $evidence.validation_gate_holding = $holding

        Request-OperatorStep "Remove USB0 now. The Library parser is paused after its first bounded PDB read; leave FLX4 connected."
        $removedGate = Wait-ValidationGateState -ExpectedState "media_removed" `
            -ExpectedSequence $gateSequence
        $afterFirst = Wait-DeviceState -Name "after_library_load_removal" `
            -StoragePresent $false -ControllerPresent $true
        $libraryAfterRemoval = Wait-LibraryCount -ExpectedCount 0
        $evidence.validation_gate_removed = $removedGate
        $evidence.library_after_load_removal_count = $libraryAfterRemoval.count

        Request-OperatorStep "Reinsert the same Rekordbox USB stick into USB0 for normal recovery; leave FLX4 connected."
    }
    elseif ($Group -eq "G") {
        $baselineLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.baseline_library_count = $baselineLibrary.count
        $targetDeck = if (($Cycle % 2) -eq 0) { 2 } else { 1 }
        $targetTrackKey = if ($targetDeck -eq 1) { [uint32]3 } else { [uint32]10 }
        $targetTrack = Get-TrackByKey -Library $baselineLibrary `
            -TrackKey $targetTrackKey
        $evidence.audio_load_target = [ordered]@{
            deck = $targetDeck
            track_key = $targetTrackKey
            title = [string]$targetTrack.title
        }

        $armed = Invoke-AudioLoadValidationGatePost -Action "arm" `
            -Deck $targetDeck
        if ([string]$armed.state -ne "armed" -or
            [int]$armed.deck -ne $targetDeck) {
            throw "Audio-load validation gate did not arm for Deck $targetDeck"
        }
        $gateSequence = [uint32]$armed.sequence
        $evidence.audio_load_gate_armed = $armed

        Invoke-LoadPost -TrackKey $targetTrackKey `
            -Generation $baselineLibrary.generation -Deck $targetDeck
        $holding = Wait-AudioLoadValidationGateState -ExpectedState "holding" `
            -ExpectedSequence $gateSequence -ExpectedDeck $targetDeck
        $evidence.audio_load_gate_holding = $holding

        Request-OperatorStep "Remove USB0 now. Deck $targetDeck has read its first bounded audio-cache page and its loader is paused; leave FLX4 connected."
        $removedGate = Wait-AudioLoadValidationGateState `
            -ExpectedState "media_removed" -ExpectedSequence $gateSequence `
            -ExpectedDeck $targetDeck
        $afterFirst = Wait-DeviceState -Name "after_audio_load_removal" `
            -StoragePresent $false -ControllerPresent $true
        $libraryAfterRemoval = Wait-LibraryCount -ExpectedCount 0
        $evidence.audio_load_gate_removed = $removedGate
        $evidence.library_after_audio_load_removal_count = `
            $libraryAfterRemoval.count

        Request-OperatorStep "Reinsert the same Rekordbox USB stick into USB0 for normal recovery; leave FLX4 connected."
    }
    else {
        $baselineLibrary = Wait-LibraryCount -ExpectedCount 100
        $evidence.baseline_library_count = $baselineLibrary.count

        Request-OperatorStep "Disconnect FLX4 from USB1 while both decks are stopped; leave USB0 connected and do not touch the stick."
        $afterFirst = Wait-DeviceState -Name "after_flx4_idle_disconnect" `
            -StoragePresent $true -ControllerPresent $false
        $libraryWhileControllerAbsent = Wait-LibraryCount -ExpectedCount 100
        $evidence.library_while_controller_absent_count = `
            $libraryWhileControllerAbsent.count
        if ((Get-CounterDelta $afterFirst.storage_disconnects `
                $baseline.storage_disconnects) -ne 0) {
            throw "USB0 disconnected while FLX4 was removed"
        }

        Request-OperatorStep "Reconnect FLX4 to USB1; leave USB0 connected and wait for profile, MIDI and UAC recovery."
    }
    $afterBoth = Wait-DeviceState -Name "after_both" `
        -StoragePresent $true -ControllerPresent $true
    # The storage owner publishes mounted=true before the asynchronous Library
    # parser finishes. Wait for the coherent generation instead of racing the
    # short mounted-with-zero-tracks transition.
    $libraryAfterBoth = Wait-LibraryCount -ExpectedCount 100
    $evidence.after_first_attachment = $afterFirst
    $evidence.after_both_attachments = $afterBoth

    $playback = Invoke-DualPlaybackSmoke -StableSnapshot $afterBoth
    $evidence.playback = $playback
    $allFailures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($failure in @($playback.failures)) {
        Add-Failure $allFailures ([string]$failure)
    }

    Write-Host "AUDIO_CONFIRMATION_REQUIRED: Confirm audible MAIN and FLX4 cue output, then press Deck 1 PLAY/PAUSE twice on FLX4."
    Write-Host "Type yes only if audio, physical transport, LEDs and all controls are normal, with no held jog/Shift/Censor/Pad FX/roll state."
    $operator = (Read-Host).Trim().ToLowerInvariant()
    $evidence.operator_confirmation = $operator
    $afterOperator = Get-DeviceSnapshot -Name "after_operator_check"
    $evidence.after_operator_check = $afterOperator
    if ($operator -notin @("yes", "y", "da")) {
        Add-Failure $allFailures "operator did not accept audio/controller/LED check"
    }
    if (-not $afterOperator.deck1_playing -or -not $afterOperator.deck2_playing) {
        Add-Failure $allFailures "physical double PLAY/PAUSE did not return both decks to playing"
    }
    if ((Get-CounterDelta $afterOperator.controller_midi_packets `
            $afterBoth.controller_midi_packets) -lt 2) {
        Add-Failure $allFailures "physical controller check produced fewer than two MIDI packets"
    }
    if ((Get-CounterDelta $afterOperator.semantic_events `
            $afterBoth.semantic_events) -lt 2) {
        Add-Failure $allFailures "physical controller check produced fewer than two semantic events"
    }

    Stop-Decks
    $final = Get-DeviceSnapshot -Name "final_idle"
    $evidence.final = $final
    if ($final.deck1_playing -or $final.deck2_playing) {
        Add-Failure $allFailures "cleanup did not stop both decks"
    }
    if (-not (Test-DeviceState -Snapshot $final `
            -StoragePresent $true -ControllerPresent $true)) {
        Add-Failure $allFailures "both USB devices were not healthy at final snapshot"
    }
    $minimumRecoveryCount = if ($Group -in @("E", "F", "G", "H")) { 0 } else { 1 }
    $maximumRecoveryCount = if ($Group -in @("E", "F", "G")) { 1 } else { 2 }
    $enforceRecoveryRange = $Group -notin @("E", "F", "G")
    $expectedStorageDisconnects = if ($Group -eq "F") { 2 } elseif ($Group -eq "E") { 1 } else { 0 }
    $expectedStorageReleases = if ($Group -eq "F") { 2 } elseif ($Group -eq "E") { 1 } else { 0 }
    if ($Group -eq "G") {
        $expectedStorageDisconnects = 1
        $expectedStorageReleases = 1
    }
    $cycleFailures = @(Get-CycleFailures -Baseline $baseline -Final $final `
        -MinimumRecoveryCount $minimumRecoveryCount `
        -MaximumRecoveryCount $maximumRecoveryCount `
        -EnforceRecoveryRange $enforceRecoveryRange `
        -ExpectedStorageDisconnects $expectedStorageDisconnects `
        -ExpectedControllerDisconnects $(if ($Group -eq "H") { 1 } else { 0 }) `
        -ExpectedStorageReleases $expectedStorageReleases `
        -MinimumControllerFaultRecoveryEpochs 0 `
        -MaximumControllerFaultRecoveryEpochs $(if ($Group -eq "H") { 1 } else { 0 }))
    $evidence.cycle_deltas = Get-CycleDeltas -Baseline $baseline -Final $final
    $evidence.cycle_failures = $cycleFailures
    foreach ($failure in $cycleFailures) {
        Add-Failure $allFailures ([string]$failure)
    }
    if ($Group -eq "H") {
        $controllerConnectDelta = Get-CounterDelta $final.controller_connects `
            $baseline.controller_connects
        if ($controllerConnectDelta -ne 1) {
            Add-Failure $allFailures "controller_connects delta is $controllerConnectDelta, expected 1"
        }
    }

    $finalBootLog = @(Get-CurrentBootLog)
    if ((Get-BootEpoch -BootLog $finalBootLog) -ne $bootEpoch) {
        Add-Failure $allFailures "device rebooted during the cycle"
    }
    $finalEventCounts = Get-GatedEventCounts -BootLog $finalBootLog
    $eventDeltas = Get-GatedEventDeltas -Baseline $baselineEventCounts `
        -Final $finalEventCounts
    $evidence.final_event_counts = $finalEventCounts
    $evidence.event_deltas = $eventDeltas
    $evidence.boot_log = $finalBootLog
    foreach ($eventName in @("uac_data_loss", "audio_underrun", "audio_output_late")) {
        if ($eventDeltas.$eventName -ne 0) {
            Add-Failure $allFailures "$eventName event delta is $($eventDeltas.$eventName)"
        }
    }
    $expectedUsbUnmountedEvents = if ($Group -eq "F") { 2 } elseif ($Group -in @("E", "G")) { 1 } else { 0 }
    if ($eventDeltas.usb_unmounted -ne $expectedUsbUnmountedEvents) {
        Add-Failure $allFailures "usb_unmounted event delta is $($eventDeltas.usb_unmounted), expected $expectedUsbUnmountedEvents"
    }
    if ($Group -eq "H") {
        if ($eventDeltas.controller_disconnected -ne 1) {
            Add-Failure $allFailures "controller_disconnected event delta is $($eventDeltas.controller_disconnected), expected 1"
        }
        if ($eventDeltas.controller_connected -ne 1) {
            Add-Failure $allFailures "controller_connected event delta is $($eventDeltas.controller_connected), expected 1"
        }
    }

    $evidence.failures = @($allFailures)
    $evidence.result = if ($allFailures.Count -eq 0) { "PASS" } else { "FAIL" }
}
catch {
    $evidence.failures = @([string]$_.Exception.Message)
    $evidence.error = [string]$_
    try { Invoke-ValidationGatePost -Action "cancel" | Out-Null } catch { }
    try { Invoke-AudioLoadValidationGatePost -Action "cancel" | Out-Null } catch { }
    try { Stop-Decks } catch { }
}
finally {
    $evidence.finished_utc = [DateTime]::UtcNow.ToString("o")
    if (-not $evidence.Contains("boot_epoch")) {
        $evidence.boot_epoch = 0
    }
    if (-not $evidence.Contains("firmware_version")) {
        $evidence.firmware_version = "unknown"
        $evidence.firmware_slot = "unknown"
    }
    if (-not $evidence.Contains("operator_confirmation")) {
        $evidence.operator_confirmation = "not reached"
    }
    if (-not $evidence.Contains("playback")) {
        $evidence.playback = [ordered]@{
            deck1_advance_ms = 0; deck2_advance_ms = 0; submitted_delta = 0
            finish = [ordered]@{ ring_frames = 0; ring_state = "not reached"; uac_flags = 0 }
        }
    }
    if (-not $evidence.Contains("cycle_failures")) {
        $evidence.cycle_failures = @()
    }
    $paths = Write-EvidenceFiles -Evidence ([pscustomobject]$evidence)
    Write-Output "EVIDENCE_JSON=$($paths.json)"
    Write-Output "EVIDENCE_MARKDOWN=$($paths.markdown)"
    Write-Output "RESULT=$($evidence.result)"
}

if ($evidence.result -ne "PASS") {
    exit 1
}
