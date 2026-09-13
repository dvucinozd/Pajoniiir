param(
    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,
    [ValidateRange(1, 2)][int]$Cycle = 1,
    [ValidateRange(5, 60)][int]$PlaybackSeconds = 10,
    [ValidateRange(20, 180)][int]$DeviceTimeoutSeconds = 90,
    [string]$OutputDirectory = "tmp/p4-lifecycle",
    [switch]$SelfTest
)

# Deterministic Group K software-reboot lifecycle harness. The firmware owns
# the reboot trigger; the operator confirms only post-boot audio/controller
# behavior. Power cycling never satisfies this gate.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repoRoot = Split-Path -Parent $PSScriptRoot
$kSelfTest = [bool]$SelfTest
$kCycle = $Cycle

. (Join-Path $PSScriptRoot "run_p4_lifecycle_cycle.ps1") `
    -Cycle $kCycle -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -PlaybackSeconds $PlaybackSeconds `
    -DeviceTimeoutSeconds $DeviceTimeoutSeconds `
    -OutputDirectory $OutputDirectory -DefineOnly

if ($Cycle -ne $kCycle) {
    throw "Group K cycle identity changed while loading shared helpers"
}

function Get-ResetReason {
    param([string[]]$BootLog)
    if ($BootLog.Count -gt 0 -and
        $BootLog[0] -match '^schema=1 boot=[0-9]+ .* reset=([^ ]+)$') {
        return [string]$Matches[1]
    }
    throw "Cannot parse reset reason"
}

function Get-PostBootFailures {
    param($Snapshot, [string[]]$BootLog, [string]$ExpectedSlot)
    $failures = New-Object 'System.Collections.Generic.List[string]'
    if (-not (Test-DeviceState -Snapshot $Snapshot `
            -StoragePresent $true -ControllerPresent $true)) {
        Add-Failure $failures "USB0 and FLX4 did not both recover"
    }
    if ($Snapshot.version -ne $ExpectedVersion) {
        Add-Failure $failures "firmware version changed to $($Snapshot.version)"
    }
    if ($Snapshot.slot -ne $ExpectedSlot) {
        Add-Failure $failures "software reboot changed slot to $($Snapshot.slot)"
    }
    if ($Snapshot.ota_state -ne "idle" -or $Snapshot.ota_error) {
        Add-Failure $failures "OTA status is not clean and idle"
    }
    foreach ($field in @(
            "topology_probe_failures", "controller_interface_claim_failures",
            "controller_transfer_alloc_failures", "controller_probe_event_drops",
            "recovery_failures", "recovery_queue_drops", "daemon_errors",
            "runtime_queue_failures", "service_log_dropped", "pcm1", "pcm2",
            "output_late")) {
        if ([uint64]$Snapshot.$field -ne [uint64]0) {
            Add-Failure $failures "$field is $($Snapshot.$field) after reboot"
        }
    }
    if ($Snapshot.storage_last_mount_result -ne 0) {
        Add-Failure $failures "storage mount result is $($Snapshot.storage_last_mount_result)"
    }
    if ($Snapshot.recovery_requests -ne $Snapshot.recovery_successes) {
        Add-Failure $failures "host recovery requests/successes are $($Snapshot.recovery_requests)/$($Snapshot.recovery_successes)"
    }
    if ($Snapshot.uac_data_loss -or $Snapshot.uac_flags -ne 0) {
        Add-Failure $failures "active UAC data-loss state is set"
    }
    if ($Snapshot.twdt_current) {
        Add-Failure $failures "current TWDT ISR flag is set"
    }
    foreach ($event in @("UAC_DATA_LOSS", "AUDIO_UNDERRUN", "AUDIO_OUTPUT_LATE")) {
        if ((Get-EventCount -BootLog $BootLog -Event $event) -ne 0) {
            Add-Failure $failures "current boot contains $event"
        }
    }
    $result = [string[]]::new($failures.Count)
    $failures.CopyTo($result)
    return $result
}

function Invoke-ValidationReboot {
    $headers = @{ "X-DDJ-Control" = "1" }
    $response = Invoke-WebRequest -UseBasicParsing -Method Post -Headers $headers `
        -Uri "$BaseUri/api/validation/reboot" -TimeoutSec 10
    if ([int]$response.StatusCode -ne 202) {
        throw "Validation reboot returned HTTP $($response.StatusCode)"
    }
    $body = $response.Content | ConvertFrom-Json
    if (-not $body.ok -or -not $body.rebooting) {
        throw "Validation reboot did not acknowledge reboot"
    }
}

function Wait-ApiOutage {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt 15) {
        try {
            [void](Invoke-WebRequest -UseBasicParsing `
                -Uri "$BaseUri/api/firmware" -TimeoutSec 2)
        }
        catch {
            return
        }
        Start-Sleep -Milliseconds 200
    }
    throw "Software reboot produced no observable API outage"
}

function Wait-NewBootLog {
    param([uint32]$PreviousBoot)
    $timer = [Diagnostics.Stopwatch]::StartNew()
    while ($timer.Elapsed.TotalSeconds -lt $DeviceTimeoutSeconds) {
        try {
            $log = @(Get-CurrentBootLog)
            if ((Get-BootEpoch -BootLog $log) -gt $PreviousBoot) {
                return $log
            }
        }
        catch {
            # The service log can become readable slightly after the status API.
        }
        Start-Sleep -Milliseconds 250
    }
    throw "Timed out waiting for a new boot log after boot $PreviousBoot"
}

function Write-KEvidence {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else { Join-Path $repoRoot $OutputDirectory }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stem = "K$Cycle-boot$($Evidence.boot_epoch)"
    $jsonPath = Join-Path $outputRoot "$stem.json"
    $mdPath = Join-Path $outputRoot "$stem.md"
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding utf8
    @(
        "# P4 lifecycle K$Cycle - boot $($Evidence.boot_epoch)", "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)`` on ``$($Evidence.firmware_slot)``",
        "- Boot transition: $($Evidence.previous_boot_epoch) -> $($Evidence.boot_epoch)",
        "- Reset reason: ``$($Evidence.reset_reason)``",
        "- Library after reboot: $($Evidence.library_count)",
        "- D1/D2 advance: $($Evidence.playback.deck1_advance_ms)/$($Evidence.playback.deck2_advance_ms) ms",
        "- UAC submitted delta: $($Evidence.playback.submitted_delta) blocks",
        "- Operator confirmation: $($Evidence.operator_confirmation)",
        "- Failures: $(@($Evidence.failures).Count)", "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    ) | Set-Content -LiteralPath $mdPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $mdPath }
}

function Invoke-KSelfTest {
    $log = @(
        "schema=1 boot=12 event=FIRMWARE_INFO fw=test partition=ota_0 reset=SW",
        "seq=1 boot=12 ms=1 level=I event=BOOT msg=ok"
    )
    if ((Get-ResetReason -BootLog $log) -ne "SW") {
        throw "reset reason self-test failed"
    }
    $snapshot = [pscustomobject]@{
        storage_mounted=$true; controller_present=$true; root_power_mask=3
        controller_profile="active"; controller_midi_in=$true
        controller_midi_out=$true; controller_usb_audio=$true
        controller_accepting_midi_out=$true; version="test"; slot="ota_0"
        ota_state="idle"; ota_error=""; topology_probe_failures=0
        controller_interface_claim_failures=0; controller_transfer_alloc_failures=0
        controller_probe_event_drops=0; recovery_failures=0
        recovery_queue_drops=0; daemon_errors=0; runtime_queue_failures=0
        service_log_dropped=0; pcm1=0; pcm2=0; output_late=0
        storage_last_mount_result=0; recovery_requests=1; recovery_successes=1
        uac_data_loss=$false; uac_flags=0; twdt_current=$false
    }
    $script:ExpectedVersion = "test"
    if (@(Get-PostBootFailures -Snapshot $snapshot -BootLog $log `
            -ExpectedSlot "ota_0").Count -ne 0) {
        throw "clean post-boot self-test failed"
    }
    $snapshot.daemon_errors = 1
    if (@(Get-PostBootFailures -Snapshot $snapshot -BootLog $log `
            -ExpectedSlot "ota_0").Count -ne 1) {
        throw "post-boot fault rejection self-test failed"
    }
    Write-Output "P4 lifecycle Group K harness self-test passed"
}

if ($kSelfTest) {
    Invoke-KSelfTest
    exit 0
}
if (-not $ExpectedVersion) {
    throw "-ExpectedVersion is required for hardware evidence"
}

$evidence = [ordered]@{
    schema = 1
    group = "K"
    cycle = $Cycle
    scenario = "software reboot with USB0 and FLX4 occupied"
    started_utc = [DateTime]::UtcNow.ToString("o")
    expected_version = $ExpectedVersion
    result = "FAIL"
    failures = @()
    operator_confirmation = "not reached"
    previous_boot_epoch = 0
    boot_epoch = 0
    reset_reason = "unknown"
    firmware_version = $ExpectedVersion
    firmware_slot = "unknown"
    library_count = 0
    playback = [pscustomobject]@{
        deck1_advance_ms = 0; deck2_advance_ms = 0
        submitted_delta = 0; failures = @()
    }
}

try {
    $baselineLog = @(Get-CurrentBootLog)
    $baselineBoot = Get-BootEpoch -BootLog $baselineLog
    $baseline = Get-DeviceSnapshot -Name "K${Cycle}_baseline"
    if ($baseline.version -ne $ExpectedVersion) {
        throw "Expected $ExpectedVersion, device runs $($baseline.version)"
    }
    if (-not (Test-DeviceState -Snapshot $baseline `
            -StoragePresent $true -ControllerPresent $true)) {
        throw "K$Cycle requires USB0 and FLX4 healthy"
    }
    if ($baseline.deck1_playing -or $baseline.deck2_playing -or
        $baseline.deck1_state -eq "LOADING" -or $baseline.deck2_state -eq "LOADING") {
        throw "K$Cycle requires both decks stopped"
    }
    [void](Wait-LibraryCount -ExpectedCount 100)
    $evidence.previous_boot_epoch = $baselineBoot
    $evidence.baseline = $baseline

    Invoke-ValidationReboot
    Wait-ApiOutage
    $recovered = Wait-DeviceState -Name "K${Cycle}_recovered" `
        -StoragePresent $true -ControllerPresent $true
    $library = Wait-LibraryCount -ExpectedCount 100
    $bootLog = @(Wait-NewBootLog -PreviousBoot $baselineBoot)
    $bootEpoch = Get-BootEpoch -BootLog $bootLog
    $resetReason = Get-ResetReason -BootLog $bootLog
    if ($bootEpoch -le $baselineBoot) {
        throw "Boot epoch did not advance ($baselineBoot -> $bootEpoch)"
    }
    if ($resetReason -ne "SW") {
        throw "Expected SW reset, observed $resetReason"
    }
    $failures = New-Object 'System.Collections.Generic.List[string]'
    foreach ($failure in @(Get-PostBootFailures -Snapshot $recovered `
            -BootLog $bootLog -ExpectedSlot $baseline.slot)) {
        $failures.Add([string]$failure)
    }

    $playback = Invoke-DualPlaybackSmoke -StableSnapshot $recovered
    foreach ($failure in @($playback.failures)) {
        $failures.Add([string]$failure)
    }
    Write-Host "AUDIO_CONFIRMATION_REQUIRED: Confirm audible MAIN and FLX4 cue, normal LEDs and controls, then press Deck 1 PLAY/PAUSE twice."
    Write-Host "Type yes only if every requested check passes."
    $operator = (Read-Host).Trim().ToLowerInvariant()
    if ($operator -notin @("yes", "y", "da", "d")) {
        $failures.Add("operator did not confirm audio/controller recovery")
    }
    $afterOperator = Get-DeviceSnapshot -Name "K${Cycle}_after_operator"
    if ($afterOperator.controller_midi_packets -le $recovered.controller_midi_packets -or
        $afterOperator.semantic_events -le $recovered.semantic_events) {
        $failures.Add("physical Deck 1 PLAY/PAUSE confirmation was not observed")
    }

    $evidence.boot_epoch = $bootEpoch
    $evidence.reset_reason = $resetReason
    $evidence.firmware_version = $recovered.version
    $evidence.firmware_slot = $recovered.slot
    $evidence.library_count = $library.count
    $evidence.recovered = $recovered
    $evidence.playback = $playback
    $evidence.operator_confirmation = $operator
    $evidence.after_operator = $afterOperator
    $evidence.boot_log = $bootLog
    $failureArray = [string[]]::new($failures.Count)
    $failures.CopyTo($failureArray)
    $evidence.failures = $failureArray
    if ($evidence.failures.Count -eq 0) { $evidence.result = "PASS" }
}
catch {
    $evidence.failures = @([string]$_.Exception.Message)
    $evidence.error = [string]$_
}
finally {
    try { Stop-Decks } catch { }
    $evidence.finished_utc = [DateTime]::UtcNow.ToString("o")
    $paths = Write-KEvidence -Evidence ([pscustomobject]$evidence)
    Write-Host "EVIDENCE_JSON=$($paths.json)"
    Write-Host "EVIDENCE_MARKDOWN=$($paths.markdown)"
    Write-Host "RESULT=$($evidence.result)"
}
if ($evidence.result -ne "PASS") {
    throw "K$Cycle failed: $($evidence.failures -join '; ')"
}
