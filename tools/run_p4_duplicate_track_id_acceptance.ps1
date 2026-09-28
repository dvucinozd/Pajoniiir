param(
    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,
    [uint32]$SharedTrackKey = 1,
    [string]$MediaATitle,
    [string]$MediaBTitle,
    [ValidateRange(1, 8)][int]$CuePad = 8,
    [ValidateRange(1000, 600000)][uint32]$MediaAPositionMs = 11000,
    [ValidateRange(1000, 600000)][uint32]$MediaBPositionMs = 22000,
    [ValidateRange(100, 3000)][uint32]$PositionToleranceMs = 500,
    [ValidateRange(30, 120)][int]$DeviceTimeoutSeconds = 120,
    [string]$OutputDirectory = "tmp/p4-duplicate-track-id",
    [switch]$SelfTest
)

# Guided hardware acceptance for two Rekordbox exports that deliberately use
# the same raw numeric track ID. The harness owns API checks, seeks, loads,
# reboot verification and evidence. The operator only swaps labelled media and
# presses/clears one confirmed-empty FLX4 Hot Cue pad.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repoRoot = Split-Path -Parent $PSScriptRoot
$savedTrackKey = $SharedTrackKey
$savedExpectedVersion = $ExpectedVersion
$savedOutputDirectory = $OutputDirectory
$savedSelfTest = [bool]$SelfTest

. (Join-Path $PSScriptRoot "run_p4_lifecycle_k.ps1") `
    -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -DeviceTimeoutSeconds $DeviceTimeoutSeconds `
    -OutputDirectory $OutputDirectory -DefineOnly

$SharedTrackKey = $savedTrackKey
$ExpectedVersion = $savedExpectedVersion
$OutputDirectory = $savedOutputDirectory
$SelfTest = $savedSelfTest

function Test-PositionWithin {
    param([uint64]$Actual, [uint64]$Expected, [uint64]$Tolerance)
    $delta = if ($Actual -ge $Expected) {
        $Actual - $Expected
    } else {
        $Expected - $Actual
    }
    return $delta -le $Tolerance
}

function Find-TrackByIdentity {
    param($Library, [uint32]$TrackKey, [string]$Title)
    $matches = @($Library.tracks | Where-Object {
        [uint32]$_.track_key -eq $TrackKey -and [string]$_.title -ceq $Title
    })
    if ($matches.Count -ne 1) {
        return $null
    }
    return $matches[0]
}

function Test-GenerationChanged {
    param([uint32]$Generation, $PreviousGeneration)
    return $null -eq $PreviousGeneration -or
        $Generation -ne [uint32]$PreviousGeneration
}

function Get-RecallProbePosition {
    param([uint32]$ExpectedPositionMs)
    if ($ExpectedPositionMs -ge 11000) {
        return [uint32]($ExpectedPositionMs - 10000)
    }
    return [uint32]($ExpectedPositionMs + 10000)
}

function Invoke-SeekPost {
    param([uint32]$PositionMs)
    $headers = @{ "X-DDJ-Control" = "1" }
    Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/control?deck=1&action=seek&value=$PositionMs" `
        -TimeoutSec 10 | Out-Null
}

function Wait-DeckPosition {
    param([uint32]$ExpectedPositionMs, [string]$Stage)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $last = $null
    while ($timer.Elapsed.TotalSeconds -lt 10) {
        $last = Get-DeviceSnapshot -Name $Stage
        if (Test-PositionWithin -Actual $last.deck1_position_ms `
                -Expected $ExpectedPositionMs -Tolerance $PositionToleranceMs) {
            return $last
        }
        Start-Sleep -Milliseconds 100
    }
    if ($null -eq $last) {
        throw "$Stage produced no device snapshot"
    }
    throw "$Stage expected D1 near $ExpectedPositionMs ms, got $($last.deck1_position_ms) ms"
}

function Wait-MediumTrack {
    param(
        [string]$Label,
        [string]$ExpectedTitle,
        $DifferentFromGeneration = $null
    )
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $lastGeneration = [uint32]0
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $library = Invoke-ApiJson -Path "/api/library" -Attempts 1
            $lastGeneration = [uint32]$library.generation
            $generationChanged = Test-GenerationChanged `
                -Generation $lastGeneration `
                -PreviousGeneration $DifferentFromGeneration
            $track = Find-TrackByIdentity -Library $library `
                -TrackKey $SharedTrackKey -Title $ExpectedTitle
            if ($generationChanged -and $null -ne $track) {
                $snapshot = Get-DeviceSnapshot -Name "${Label}_mounted"
                if (Test-DeviceState -Snapshot $snapshot `
                        -StoragePresent $true -ControllerPresent $true) {
                    return [pscustomobject]@{
                        label = $Label
                        generation = $lastGeneration
                        count = @($library.tracks).Count
                        title = [string]$track.title
                        track_key = [uint32]$track.track_key
                        snapshot = $snapshot
                    }
                }
            }
        }
        catch {
            # Media removal and catalog rebuild create a bounded API gap.
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for medium $Label, title '$ExpectedTitle', key $SharedTrackKey (last generation $lastGeneration)"
}

function Load-MediumTrack {
    param($Medium)
    Invoke-LoadPost -TrackKey $SharedTrackKey `
        -Generation $Medium.generation -Deck 1
    return Wait-DeckReady -Deck 1 -ExpectedTitle $Medium.title
}

function Invoke-PadStep {
    param(
        [string]$Message,
        [uint32]$ExpectedPositionMs,
        [string]$Stage
    )
    Request-OperatorStep -Message "Select HOT CUE mode for D1 and verify its mode LED. Do not press a performance pad yet."
    $before = Get-DeviceSnapshot -Name "${Stage}_before_pad"
    Write-Host "ACTION_REQUIRED: $Message"
    Write-Host "The harness is monitoring the physical MIDI event; no Enter key is required."
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $after = $null
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        $candidate = Get-DeviceSnapshot -Name "${Stage}_after_pad"
        if ($candidate.controller_midi_packets -gt $before.controller_midi_packets -and
            $candidate.semantic_events -gt $before.semantic_events) {
            $after = $candidate
            break
        }
        Start-Sleep -Milliseconds 50
    }
    if ($null -eq $after) {
        throw "$Stage did not observe a physical MIDI/semantic pad event"
    }
    $after = Wait-DeckPosition -ExpectedPositionMs $ExpectedPositionMs `
        -Stage "${Stage}_after_pad"
    $padFailures = @(Get-DuplicateIdHealthFailures `
        -Baseline $before -Final $after)
    if ($padFailures.Count -ne 0) {
        throw "$Stage health failure: $($padFailures -join '; ')"
    }
    return [pscustomobject]@{
        before = $before
        after = $after
        expected_position_ms = $ExpectedPositionMs
    }
}

function Prepare-And-SetCue {
    param($Medium, [uint32]$PositionMs, [string]$Label)
    [void](Load-MediumTrack -Medium $Medium)
    Invoke-SeekPost -PositionMs $PositionMs
    [void](Wait-DeckPosition -ExpectedPositionMs $PositionMs -Stage "${Label}_seek")
    return Invoke-PadStep -Stage "${Label}_set" -ExpectedPositionMs $PositionMs `
        -Message "Verify pad $CuePad LED is OFF for medium $Label, then press pad $CuePad once and verify its LED turns ON. If it is already ON, abort with Ctrl+C instead of overwriting a cue."
}

function Recall-Cue {
    param($Medium, [uint32]$ExpectedPositionMs, [string]$Label)
    [void](Load-MediumTrack -Medium $Medium)
    $probePosition = Get-RecallProbePosition `
        -ExpectedPositionMs $ExpectedPositionMs
    Invoke-SeekPost -PositionMs $probePosition
    [void](Wait-DeckPosition -ExpectedPositionMs $probePosition -Stage "${Label}_probe_seek")
    return Invoke-PadStep -Stage "${Label}_recall" `
        -ExpectedPositionMs $ExpectedPositionMs `
        -Message "Press FLX4 Hot Cue pad $CuePad once for medium $Label and verify playback recalls its saved cue."
}

function Request-MediumSwap {
    param(
        [string]$Label,
        [string]$Title,
        $PreviousGeneration = $null
    )
    Write-Host "ACTION_REQUIRED: Stop D1 if needed, remove the current USB0 medium, and insert labelled medium $Label."
    Write-Host "The harness is monitoring the library change; no Enter key is required."
    return Wait-MediumTrack -Label $Label -ExpectedTitle $Title `
        -DifferentFromGeneration $PreviousGeneration
}

function Clear-Cue {
    param([string]$Label)
    Request-OperatorStep -Message "Select HOT CUE mode for D1 and verify its mode LED. Do not press a performance pad yet."
    $before = Get-DeviceSnapshot -Name "${Label}_before_clear"
    Write-Host "ACTION_REQUIRED: On medium $Label hold SHIFT and press Hot Cue pad $CuePad once. Verify the pad LED turns OFF."
    Write-Host "The harness is monitoring the physical MIDI event; no Enter key is required."
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        $after = Get-DeviceSnapshot -Name "${Label}_after_clear"
        if ($after.controller_midi_packets -gt $before.controller_midi_packets -and
            $after.semantic_events -gt $before.semantic_events) {
            $clearFailures = @(Get-DuplicateIdHealthFailures `
                -Baseline $before -Final $after)
            if ($clearFailures.Count -ne 0) {
                throw "$Label cleanup health failure: $($clearFailures -join '; ')"
            }
            Request-OperatorStep -Message "Confirm Hot Cue pad $CuePad LED is OFF on medium $Label after the clear action."
            return [pscustomobject]@{ before = $before; after = $after }
        }
        Start-Sleep -Milliseconds 50
    }
    throw "$Label cleanup did not observe a physical MIDI/semantic pad event"
}

function Get-StrictHealthCounterNames {
    # The UAC consumer may zero-fill while playback is idle, and that expected
    # path contributes to raw underflow_frames. Active playback loss is gated
    # by data_loss_flags on every monitored pad snapshot instead.
    return @(
        "topology_probe_failures", "controller_interface_claim_failures",
        "controller_transfer_alloc_failures", "controller_probe_event_drops",
        "daemon_errors", "recovery_failures", "recovery_queue_drops",
        "runtime_queue_failures", "service_log_dropped", "dropped_blocks",
        "overflow_frames", "packet_failures",
        "packet_lost_frames", "pcm1", "pcm2", "output_late"
    )
}

function Get-DuplicateIdHealthFailures {
    param($Baseline, $Final)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($field in @(Get-StrictHealthCounterNames)) {
        $delta = Get-CounterDelta -Current $Final.$field -Previous $Baseline.$field
        if ($delta -ne 0) {
            Add-Failure $failures "$field increased by $delta"
        }
    }
    if ($Final.version -ne $ExpectedVersion -or $Final.slot -ne $Baseline.slot) {
        Add-Failure $failures "firmware identity changed"
    }
    if ($Final.ota_state -ne "idle" -or $Final.ota_error) {
        Add-Failure $failures "OTA status is not clean and idle"
    }
    if (-not (Test-DeviceState -Snapshot $Final `
            -StoragePresent $true -ControllerPresent $true)) {
        Add-Failure $failures "USB0 and FLX4 are not both healthy"
    }
    if ($Final.storage_last_mount_result -ne 0) {
        Add-Failure $failures "storage mount result is $($Final.storage_last_mount_result)"
    }
    if ($Final.recovery_requests -ne $Final.recovery_successes) {
        Add-Failure $failures "host recovery requests/successes are $($Final.recovery_requests)/$($Final.recovery_successes)"
    }
    if ($Final.uac_data_loss -or $Final.uac_flags -ne 0) {
        Add-Failure $failures "active UAC data-loss state is set"
    }
    if ($Final.twdt_current) {
        Add-Failure $failures "current TWDT ISR flag is set"
    }
    return @($failures)
}

function Get-DuplicateIdAbsoluteCounterFailures {
    param($Snapshot)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($field in @(Get-StrictHealthCounterNames)) {
        if ([uint64]$Snapshot.$field -ne [uint64]0) {
            Add-Failure $failures "$field is $($Snapshot.$field) after reboot"
        }
    }
    return @($failures)
}

function Write-DuplicateIdEvidence {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else {
        Join-Path $repoRoot $OutputDirectory
    }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $jsonPath = Join-Path $outputRoot "duplicate-track-id-$stamp.json"
    $mdPath = Join-Path $outputRoot "duplicate-track-id-$stamp.md"
    $Evidence | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath $jsonPath -Encoding utf8
    @(
        "# P4 duplicate raw track-ID acceptance", "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)``",
        "- Shared raw track key: $($Evidence.shared_track_key)",
        "- Medium A: ``$($Evidence.media_a.title)`` at $($Evidence.media_a.position_ms) ms",
        "- Medium B: ``$($Evidence.media_b.title)`` at $($Evidence.media_b.position_ms) ms",
        "- Boot transition: $($Evidence.boot_before) -> $($Evidence.boot_after)",
        "- Failures: $(@($Evidence.failures).Count)", "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    ) | Set-Content -LiteralPath $mdPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $mdPath }
}

function Invoke-DuplicateIdSelfTest {
    if (-not (Test-PositionWithin -Actual 10490 -Expected 10000 -Tolerance 500) -or
        (Test-PositionWithin -Actual 10501 -Expected 10000 -Tolerance 500)) {
        throw "position tolerance self-test failed"
    }
    $library = [pscustomobject]@{ tracks = @(
        [pscustomobject]@{ track_key = 1; title = "A" },
        [pscustomobject]@{ track_key = 1; title = "B" },
        [pscustomobject]@{ track_key = 2; title = "A" }
    ) }
    if ((Find-TrackByIdentity -Library $library -TrackKey 1 -Title "A").title -ne "A" -or
        $null -ne (Find-TrackByIdentity -Library $library -TrackKey 3 -Title "A")) {
        throw "track identity self-test failed"
    }
    if (-not (Test-GenerationChanged -Generation 1 -PreviousGeneration $null) -or
        -not (Test-GenerationChanged -Generation 2 -PreviousGeneration 1) -or
        (Test-GenerationChanged -Generation 2 -PreviousGeneration 2)) {
        throw "generation comparison self-test failed"
    }
    $probeLow = Get-RecallProbePosition -ExpectedPositionMs 1000
    $probeHigh = Get-RecallProbePosition -ExpectedPositionMs 600000
    if ($probeLow -ne 11000 -or $probeHigh -ne 590000 -or
        (Test-PositionWithin -Actual $probeLow -Expected 1000 -Tolerance 3000) -or
        (Test-PositionWithin -Actual $probeHigh -Expected 600000 -Tolerance 3000)) {
        throw "recall probe separation self-test failed"
    }
    $clean = [pscustomobject]@{
        version="test"; slot="ota_0"; ota_state="idle"; ota_error=""
        storage_mounted=$true; controller_present=$true; root_power_mask=3
        controller_profile="active"; controller_midi_in=$true
        controller_midi_out=$true; controller_usb_audio=$true
        controller_accepting_midi_out=$true; storage_last_mount_result=0
        recovery_requests=2; recovery_successes=2; uac_data_loss=$false
        uac_flags=0; twdt_current=$false; topology_probe_failures=0
        controller_interface_claim_failures=0
        controller_transfer_alloc_failures=0; controller_probe_event_drops=0
        daemon_errors=0; recovery_failures=0; recovery_queue_drops=0
        runtime_queue_failures=0; service_log_dropped=0; dropped_blocks=0
        overflow_frames=0; underflow_frames=0; packet_failures=0
        packet_lost_frames=0; pcm1=0; pcm2=0; output_late=0
    }
    $script:ExpectedVersion = "test"
    if (@(Get-DuplicateIdHealthFailures -Baseline $clean -Final $clean).Count -ne 0) {
        throw "clean health self-test failed"
    }
    if (@(Get-DuplicateIdAbsoluteCounterFailures -Snapshot $clean).Count -ne 0) {
        throw "clean absolute-counter self-test failed"
    }
    $idleUnderflow = $clean | Select-Object *
    $idleUnderflow.underflow_frames = 123456
    if (@(Get-DuplicateIdHealthFailures `
            -Baseline $clean -Final $idleUnderflow).Count -ne 0 -or
        @(Get-DuplicateIdAbsoluteCounterFailures `
            -Snapshot $idleUnderflow).Count -ne 0) {
        throw "idle underflow policy self-test failed"
    }
    $fault = $clean | Select-Object *
    $fault.output_late = 1
    if (@(Get-DuplicateIdHealthFailures -Baseline $clean -Final $fault).Count -ne 1) {
        throw "health fault rejection self-test failed"
    }
    if (@(Get-DuplicateIdAbsoluteCounterFailures -Snapshot $fault).Count -ne 1) {
        throw "absolute-counter fault rejection self-test failed"
    }
    Write-Output "P4 duplicate raw track-ID acceptance harness self-test passed"
}

if ($SelfTest) {
    Invoke-DuplicateIdSelfTest
    exit 0
}
if (-not $ExpectedVersion -or -not $MediaATitle -or -not $MediaBTitle) {
    throw "-ExpectedVersion, -MediaATitle and -MediaBTitle are required"
}
if ($SharedTrackKey -eq 0) {
    throw "-SharedTrackKey must be a non-zero raw Rekordbox track ID"
}
if ($MediaATitle -eq $MediaBTitle) {
    throw "Media A and B titles must differ so the mounted export is observable"
}
$positionSeparation = if ($MediaAPositionMs -ge $MediaBPositionMs) {
    [uint64]$MediaAPositionMs - [uint64]$MediaBPositionMs
} else {
    [uint64]$MediaBPositionMs - [uint64]$MediaAPositionMs
}
if ($positionSeparation -le [uint64]($PositionToleranceMs * 2)) {
    throw "A/B cue positions must be separated by more than twice the tolerance"
}

$evidence = [ordered]@{
    schema = 1
    scenario = "same raw Rekordbox track ID isolated across two export identities"
    started_utc = [DateTime]::UtcNow.ToString("o")
    expected_version = $ExpectedVersion
    firmware_version = "unknown"
    firmware_slot = "unknown"
    shared_track_key = $SharedTrackKey
    cue_pad = $CuePad
    tolerance_ms = $PositionToleranceMs
    media_a = [ordered]@{ title=$MediaATitle; position_ms=$MediaAPositionMs }
    media_b = [ordered]@{ title=$MediaBTitle; position_ms=$MediaBPositionMs }
    boot_before = 0
    boot_after = 0
    stages = [ordered]@{}
    cleanup = "not reached"
    failures = @()
    result = "FAIL"
}

try {
    Stop-Decks
    $baseline = Get-DeviceSnapshot -Name "duplicate_id_baseline"
    $bootBefore = Get-BootEpoch -BootLog @(Get-CurrentBootLog)
    if ($baseline.version -ne $ExpectedVersion) {
        throw "Expected $ExpectedVersion, device runs $($baseline.version)"
    }
    if (-not (Test-DeviceState -Snapshot $baseline `
            -StoragePresent $true -ControllerPresent $true)) {
        throw "USB0 and FLX4 must be healthy before the test"
    }
    $evidence.firmware_version = $baseline.version
    $evidence.firmware_slot = $baseline.slot
    $evidence.boot_before = $bootBefore
    $evidence.baseline = $baseline

    $mediaA = Wait-MediumTrack -Label "A" -ExpectedTitle $MediaATitle
    $evidence.media_a.generation_initial = $mediaA.generation
    $evidence.media_a.library_count = $mediaA.count
    $evidence.stages.a_set = Prepare-And-SetCue -Medium $mediaA `
        -PositionMs $MediaAPositionMs -Label "A"

    $mediaB = Request-MediumSwap -Label "B" -Title $MediaBTitle `
        -PreviousGeneration ([uint32]$mediaA.generation)
    $evidence.media_b.generation_initial = $mediaB.generation
    $evidence.media_b.library_count = $mediaB.count
    $evidence.stages.b_set = Prepare-And-SetCue -Medium $mediaB `
        -PositionMs $MediaBPositionMs -Label "B"

    $mediaA2 = Request-MediumSwap -Label "A" -Title $MediaATitle `
        -PreviousGeneration ([uint32]$mediaB.generation)
    $evidence.stages.a_remount_recall = Recall-Cue -Medium $mediaA2 `
        -ExpectedPositionMs $MediaAPositionMs -Label "A_remount"

    $preReboot = Get-DeviceSnapshot -Name "duplicate_id_pre_reboot"
    $preRebootFailures = @(Get-DuplicateIdHealthFailures `
        -Baseline $baseline -Final $preReboot)
    if ($preRebootFailures.Count -ne 0) {
        throw "pre-reboot health failure: $($preRebootFailures -join '; ')"
    }
    $evidence.pre_reboot = $preReboot

    Stop-Decks
    Write-Host "The harness will request a firmware-owned software reboot. Reconnect this PC to the Pajoniiir Wi-Fi profile if Windows drops the AP association."
    Invoke-ValidationReboot
    Wait-ApiOutage
    $bootLog = @(Wait-NewBootLog -PreviousBoot $bootBefore)
    $bootAfter = Get-BootEpoch -BootLog $bootLog
    $recovered = Wait-DeviceState -Name "duplicate_id_post_reboot" `
        -StoragePresent $true -ControllerPresent $true
    if ($recovered.version -ne $ExpectedVersion -or
        $recovered.slot -ne $baseline.slot) {
        throw "firmware identity changed across reboot"
    }
    $postBootFailures = @(
        @(Get-PostBootFailures -Snapshot $recovered `
            -BootLog $bootLog -ExpectedSlot $baseline.slot)
        @(Get-DuplicateIdAbsoluteCounterFailures -Snapshot $recovered)
    )
    if ($postBootFailures.Count -ne 0) {
        throw "post-reboot health failure: $($postBootFailures -join '; ')"
    }
    $mediaA3 = Wait-MediumTrack -Label "A_post_reboot" `
        -ExpectedTitle $MediaATitle
    $evidence.stages.a_reboot_recall = Recall-Cue -Medium $mediaA3 `
        -ExpectedPositionMs $MediaAPositionMs -Label "A_reboot"
    $evidence.boot_after = $bootAfter
    $evidence.recovered = $recovered

    $mediaB2 = Request-MediumSwap -Label "B" -Title $MediaBTitle `
        -PreviousGeneration ([uint32]$mediaA3.generation)
    $evidence.stages.b_remount_recall = Recall-Cue -Medium $mediaB2 `
        -ExpectedPositionMs $MediaBPositionMs -Label "B_remount"

    $evidence.stages.b_clear = Clear-Cue -Label "B"
    $mediaA4 = Request-MediumSwap -Label "A" -Title $MediaATitle `
        -PreviousGeneration ([uint32]$mediaB2.generation)
    [void](Load-MediumTrack -Medium $mediaA4)
    $evidence.stages.a_clear = Clear-Cue -Label "A"
    $evidence.cleanup = "operator cleared test pad on B and A; A left inserted"

    $final = Get-DeviceSnapshot -Name "duplicate_id_final"
    $finalFailures = @(Get-DuplicateIdHealthFailures `
        -Baseline $recovered -Final $final)
    if ($finalFailures.Count -ne 0) {
        throw "final device health failure: $($finalFailures -join '; ')"
    }
    $evidence.final = $final
    $evidence.result = "PASS"
}
catch {
    $evidence.failures = @([string]$_.Exception.Message)
    throw
}
finally {
    try { Stop-Decks } catch { }
    $evidence.finished_utc = [DateTime]::UtcNow.ToString("o")
    $paths = Write-DuplicateIdEvidence -Evidence $evidence
    Write-Output "Duplicate track-ID evidence JSON: $($paths.json)"
    Write-Output "Duplicate track-ID evidence Markdown: $($paths.markdown)"
}

Write-Output "P4 duplicate raw track-ID hardware acceptance PASS"
