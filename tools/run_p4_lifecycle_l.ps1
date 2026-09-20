param(
    [string]$BaseUri = "http://192.168.4.1",
    [string]$ExpectedVersion,
    [Parameter(Mandatory = $false)][string]$BundlePath,
    [ValidateRange(1, 2)][int]$Cycle = 1,
    [ValidateRange(5, 60)][int]$PlaybackSeconds = 10,
    [ValidateRange(30, 240)][int]$DeviceTimeoutSeconds = 120,
    [string]$OutputDirectory = "tmp/p4-lifecycle",
    [switch]$SelfTest
)

# Deterministic Group L signed push-OTA lifecycle harness. The same exact
# signed image may be written to the inactive slot for both cycles. Acceptance
# requires a real OTA acknowledgement, slot change and automatic dual-USB
# recovery; a software reboot or power cycle cannot satisfy this gate.

Set-StrictMode -Version 2.0
$ErrorActionPreference = "Stop"
$ProgressPreference = "SilentlyContinue"
$repoRoot = Split-Path -Parent $PSScriptRoot
$lSelfTest = [bool]$SelfTest
$lCycle = $Cycle
$lBundlePath = $BundlePath

. (Join-Path $PSScriptRoot "run_p4_lifecycle_k.ps1") `
    -Cycle $lCycle -BaseUri $BaseUri -ExpectedVersion $ExpectedVersion `
    -PlaybackSeconds $PlaybackSeconds `
    -DeviceTimeoutSeconds ([Math]::Min($DeviceTimeoutSeconds, 180)) `
    -OutputDirectory $OutputDirectory -DefineOnly

if ($Cycle -ne $lCycle) {
    throw "Group L cycle identity changed while loading shared helpers"
}
$BundlePath = $lBundlePath

function Get-OppositeOtaSlot {
    param([string]$CurrentSlot)
    switch ($CurrentSlot) {
        "ota_0" { return "ota_1" }
        "ota_1" { return "ota_0" }
        default { throw "Group L requires an OTA slot, got '$CurrentSlot'" }
    }
}

function ConvertFrom-BundleMetadata {
    param([string[]]$Lines)
    $metadata = @{}
    foreach ($line in $Lines) {
        if ($line -match '^([^=]+)=(.*)$') {
            $metadata[[string]$Matches[1]] = [string]$Matches[2]
        }
    }
    return [pscustomobject]@{
        target = [string]$metadata.target
        project = [string]$metadata.project
        version = [string]$metadata.version
        image_size = [uint64]$metadata.image_size
        sha256 = [string]$metadata.sha256
        key_id = [string]$metadata.key_id
    }
}

function Test-SignedBundle {
    param([string]$Path)
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    Import-Module (Join-Path $PSScriptRoot "OtaReleaseHelpers.psm1") -Force
    $python = Resolve-OtaSigningPython
    $signingTool = Join-Path $PSScriptRoot "ota_signing.py"
    $publicKey = Join-Path $repoRoot `
        "firmware/common/ota_manifest/keys/ddj_ota_release_public.der"
    $lines = @(& $python $signingTool verify-bundle `
        --public-key $publicKey --input $resolved)
    if ($LASTEXITCODE -ne 0) {
        throw "Signed OTA bundle verification failed"
    }
    $metadata = ConvertFrom-BundleMetadata -Lines $lines
    if ($metadata.target -ne "p4" -or
        $metadata.project -ne "main-deck-p4" -or
        $metadata.version -ne $ExpectedVersion) {
        throw "Bundle identity does not match p4/main-deck-p4/$ExpectedVersion"
    }
    $file = Get-Item -LiteralPath $resolved
    [pscustomobject]@{
        path = $resolved
        size = [uint64]$file.Length
        bundle_sha256 = (Get-FileHash -LiteralPath $resolved `
            -Algorithm SHA256).Hash.ToLowerInvariant()
        image_size = $metadata.image_size
        image_sha256 = $metadata.sha256
        version = $metadata.version
        key_id = $metadata.key_id
    }
}

function Invoke-SignedOtaUpload {
    param([string]$Path)
    $headers = @{
        "X-DDJ-Control" = "1"
        "X-DDJ-OTA" = "p4"
    }
    $response = Invoke-WebRequest -UseBasicParsing -Method Post `
        -Headers $headers -ContentType "application/octet-stream" `
        -InFile $Path -Uri "$BaseUri/api/ota/p4" -TimeoutSec 120
    if ([int]$response.StatusCode -ne 200) {
        throw "Signed OTA returned HTTP $($response.StatusCode)"
    }
    $body = $response.Content | ConvertFrom-Json
    if (-not $body.ok -or -not $body.rebooting) {
        throw "Signed OTA did not acknowledge reboot"
    }
}

function Write-LEvidence {
    param($Evidence)
    $outputRoot = if ([IO.Path]::IsPathRooted($OutputDirectory)) {
        $OutputDirectory
    } else { Join-Path $repoRoot $OutputDirectory }
    New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
    $stem = "L$Cycle-boot$($Evidence.boot_epoch)"
    $jsonPath = Join-Path $outputRoot "$stem.json"
    $mdPath = Join-Path $outputRoot "$stem.md"
    $Evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $jsonPath -Encoding utf8
    @(
        "# P4 lifecycle L$Cycle - boot $($Evidence.boot_epoch)", "",
        "- Result: **$($Evidence.result)**",
        "- Firmware: ``$($Evidence.firmware_version)``",
        "- Slot transition: ``$($Evidence.previous_slot)`` -> ``$($Evidence.firmware_slot)``",
        "- Boot transition: $($Evidence.previous_boot_epoch) -> $($Evidence.boot_epoch)",
        "- Reset reason: ``$($Evidence.reset_reason)``",
        "- Bundle SHA-256: ``$($Evidence.bundle.bundle_sha256)``",
        "- Library after reboot: $($Evidence.library_count)",
        "- D1/D2 advance: $($Evidence.playback.deck1_advance_ms)/$($Evidence.playback.deck2_advance_ms) ms",
        "- UAC submitted delta: $($Evidence.playback.submitted_delta) blocks",
        "- Operator confirmation: $($Evidence.operator_confirmation)",
        "- Failures: $(@($Evidence.failures).Count)", "",
        "JSON evidence: ``$([IO.Path]::GetFileName($jsonPath))``"
    ) | Set-Content -LiteralPath $mdPath -Encoding utf8
    return [pscustomobject]@{ json = $jsonPath; markdown = $mdPath }
}

function Invoke-LSelfTest {
    if ((Get-OppositeOtaSlot -CurrentSlot "ota_0") -ne "ota_1" -or
        (Get-OppositeOtaSlot -CurrentSlot "ota_1") -ne "ota_0") {
        throw "opposite-slot self-test failed"
    }
    $metadata = ConvertFrom-BundleMetadata -Lines @(
        "target=p4", "project=main-deck-p4", "version=test",
        "image_size=123", "sha256=abc", "key_id=rel-001")
    if ($metadata.target -ne "p4" -or $metadata.version -ne "test" -or
        $metadata.image_size -ne 123 -or $metadata.key_id -ne "rel-001") {
        throw "bundle metadata self-test failed"
    }
    Write-Output "P4 lifecycle Group L harness self-test passed"
}

if ($lSelfTest) {
    Invoke-LSelfTest
    exit 0
}
if (-not $ExpectedVersion) {
    throw "-ExpectedVersion is required for hardware evidence"
}
if (-not $BundlePath) {
    throw "-BundlePath is required for hardware evidence"
}

$evidence = [ordered]@{
    schema = 1
    group = "L"
    cycle = $Cycle
    scenario = "signed push OTA reboot with USB0 and FLX4 occupied"
    started_utc = [DateTime]::UtcNow.ToString("o")
    expected_version = $ExpectedVersion
    result = "FAIL"
    failures = @()
    operator_confirmation = "not reached"
    previous_boot_epoch = 0
    boot_epoch = 0
    previous_slot = "unknown"
    firmware_version = $ExpectedVersion
    firmware_slot = "unknown"
    reset_reason = "unknown"
    library_count = 0
    bundle = [pscustomobject]@{
        path = $BundlePath; size = 0; bundle_sha256 = ""
        image_size = 0; image_sha256 = ""; version = ""; key_id = ""
    }
    playback = [pscustomobject]@{
        deck1_advance_ms = 0; deck2_advance_ms = 0
        submitted_delta = 0; failures = @()
    }
}

try {
    $bundle = Test-SignedBundle -Path $BundlePath
    $baselineLog = @(Get-CurrentBootLog)
    $baselineBoot = Get-BootEpoch -BootLog $baselineLog
    $baseline = Get-DeviceSnapshot -Name "L${Cycle}_baseline"
    if ($baseline.version -ne $ExpectedVersion) {
        throw "Expected $ExpectedVersion, device runs $($baseline.version)"
    }
    if (-not (Test-DeviceState -Snapshot $baseline `
            -StoragePresent $true -ControllerPresent $true)) {
        throw "L$Cycle requires USB0 and FLX4 healthy"
    }
    if ($baseline.deck1_playing -or $baseline.deck2_playing -or
        $baseline.deck1_state -eq "LOADING" -or $baseline.deck2_state -eq "LOADING") {
        throw "L$Cycle requires both decks stopped"
    }
    [void](Wait-LibraryCount -ExpectedCount 100)
    $expectedSlot = Get-OppositeOtaSlot -CurrentSlot $baseline.slot
    $evidence.previous_boot_epoch = $baselineBoot
    $evidence.previous_slot = $baseline.slot
    $evidence.bundle = $bundle
    $evidence.baseline = $baseline

    Invoke-SignedOtaUpload -Path $bundle.path
    Wait-ApiOutage
    $recovered = Wait-DeviceState -Name "L${Cycle}_recovered" `
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
            -BootLog $bootLog -ExpectedSlot $expectedSlot)) {
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
    $afterOperator = Get-DeviceSnapshot -Name "L${Cycle}_after_operator"
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
    $paths = Write-LEvidence -Evidence ([pscustomobject]$evidence)
    Write-Host "EVIDENCE_JSON=$($paths.json)"
    Write-Host "EVIDENCE_MARKDOWN=$($paths.markdown)"
    Write-Host "RESULT=$($evidence.result)"
}
if ($evidence.result -ne "PASS") {
    throw "L$Cycle failed: $($evidence.failures -join '; ')"
}
