<#
.SYNOPSIS
Read-only reliability monitor for shared Pajoniiir P4 firmware.

.DESCRIPTION
Captures status, library, firmware, diagnostic-log, latency and counter-delta
evidence. Observe records without enforcing a workload. TimingSoak enforces the
documented dual-deck mixed-rate profile. UsbRecovery measures controller and
library recovery against the documented 15/20-second investigation limits.
#>
[CmdletBinding()]
param(
    [string]$BaseUrl = "http://192.168.4.1",
    [ValidateSet('m3','jc4880','jc1060')]
    [string]$ExpectedBoard = 'm3',
    [string]$ExpectedSourceSha = '',
    [string]$ExpectedElfSha256 = '',
    [ValidateSet('Observe', 'TimingSoak', 'UsbRecovery')]
    [string]$Mode = 'Observe',
    [ValidateRange(1, 1440)]
    [int]$DurationMinutes = 60,
    [ValidateRange(0, 86400)]
    [int]$DurationSeconds = 0,
    [ValidateRange(100, 60000)]
    [int]$PollIntervalMs = 250,
    [ValidateRange(1, 100000)]
    [int]$LibraryEvery = 40,
    [ValidateRange(1, 100000)]
    [int]$FirmwareEvery = 120,
    [ValidateRange(1, 600)]
    [int]$ExpectedLibraryTracks = 191,
    [ValidateRange(1, 600)]
    [int]$MaxApiOutageSeconds = 15,
    [string]$OutputDirectory = ""
)

$ErrorActionPreference = "Stop"
Add-Type -AssemblyName System.Net.Http
$Utf8NoBom = [System.Text.UTF8Encoding]::new($false)
$StartedAt = Get-Date
$RunSeconds = if ($DurationSeconds -gt 0) {
    $DurationSeconds
} else {
    $DurationMinutes * 60
}

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $stamp = $StartedAt.ToString("yyyyMMdd-HHmmss")
    $OutputDirectory = Join-Path $PSScriptRoot "..\tmp\p4-$ExpectedBoard-reliability-$stamp"
}
$OutputDirectory = [System.IO.Path]::GetFullPath($OutputDirectory)
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null

$statusRawPath = Join-Path $OutputDirectory "status.jsonl"
$libraryRawPath = Join-Path $OutputDirectory "library.jsonl"
$firmwareRawPath = Join-Path $OutputDirectory "firmware.jsonl"
$resourcesRawPath = Join-Path $OutputDirectory "resources.jsonl"
$csvPath = Join-Path $OutputDirectory "summary.csv"
$errorsPath = Join-Path $OutputDirectory "http-errors.csv"
$initialLogPath = Join-Path $OutputDirectory "diagnostic-log-initial.txt"
$finalLogPath = Join-Path $OutputDirectory "diagnostic-log-final.txt"
$summaryPath = Join-Path $OutputDirectory "result.json"

$client = [System.Net.Http.HttpClient]::new()
$client.Timeout = [TimeSpan]::FromSeconds([Math]::Max(10, $MaxApiOutageSeconds))
$client.DefaultRequestHeaders.CacheControl =
    [System.Net.Http.Headers.CacheControlHeaderValue]::new()
$client.DefaultRequestHeaders.CacheControl.NoCache = $true

$statusWriter = [System.IO.StreamWriter]::new($statusRawPath, $false, $Utf8NoBom)
$libraryWriter = [System.IO.StreamWriter]::new($libraryRawPath, $false, $Utf8NoBom)
$firmwareWriter = [System.IO.StreamWriter]::new($firmwareRawPath, $false, $Utf8NoBom)
$resourcesWriter = [System.IO.StreamWriter]::new($resourcesRawPath, $false, $Utf8NoBom)
$csvWriter = [System.IO.StreamWriter]::new($csvPath, $false, $Utf8NoBom)
$errorsWriter = [System.IO.StreamWriter]::new($errorsPath, $false, $Utf8NoBom)

function Require-Value($Record, [string]$Path) {
    $value = $Record
    foreach ($part in $Path.Split('.')) {
        if ($null -eq $value -or $null -eq $value.PSObject.Properties[$part]) {
            throw "Required telemetry is missing: $Path"
        }
        $value = $value.$part
    }
    if ($null -eq $value) { throw "Required telemetry is null: $Path" }
    return $value
}

function Assert-Identity($Record, $Expected, [bool]$Firmware) {
    foreach ($field in @('board_id','project','source_sha','source_dirty')) {
        $value = Require-Value $Record $field
        if ($value -cne $Expected[$field]) { throw "Device identity changed: $field" }
    }
    if ($Firmware -and (Require-Value $Record 'image_elf_sha256') -cne $Expected.image_elf_sha256) {
        throw 'Compiled image ELF identity changed'
    }
}

function Assert-Status($Record, $Expected) {
    Assert-Identity $Record $Expected $false
    foreach ($field in @(
        'uptime_ms','controller.present','controller.midi_in','controller.midi_out','controller.usb_audio',
        'deck1.playing','deck2.playing',
        'deck1.pitch_percent','deck2.pitch_percent','deck1.position_ms','deck2.position_ms',
        'diagnostics.deck_sample_rate1','diagnostics.deck_sample_rate2','diagnostics.output_sample_rate',
        'diagnostics.output_late_count','diagnostics.output_late_max_us',
        'diagnostics.pcm_underrun1','diagnostics.pcm_underrun2',
        'diagnostics.usb_headphones.dropped_blocks','diagnostics.usb_headphones.overflow_frames',
        'diagnostics.usb_headphones.underflow_frames','diagnostics.usb_headphones.ring_state',
        'diagnostics.usb_headphones.data_loss','service_log.dropped','service_log.queue_depth',
        'diagnostics.internal_free','diagnostics.psram_free')) {
        [void](Require-Value $Record $field)
    }
    if ($Mode -eq 'TimingSoak') {
        foreach ($field in @('deck1.loop_active','deck2.loop_active','deck1.master_tempo','deck2.master_tempo')) {
            [void](Require-Value $Record $field)
        }
    }
}

function Get-LibraryCount($Record) {
    [void](Require-Value $Record 'generation')
    $property = $Record.PSObject.Properties['tracks']
    if ($null -eq $property -or $property.Value -isnot [System.Array]) {
        throw 'Library tracks must be an array'
    }
    return $property.Value.Count
}

function ConvertTo-CsvField {
    param([AllowNull()]$Value)
    if ($null -eq $Value) { return '""' }
    $text = [string]$Value
    return '"' + $text.Replace('"', '""') + '"'
}

function Write-CsvRow {
    param(
        [System.IO.StreamWriter]$Writer,
        [object[]]$Values
    )
    $fields = foreach ($value in $Values) { ConvertTo-CsvField $value }
    $Writer.WriteLine(($fields -join ','))
    $Writer.Flush()
}

function Invoke-M3Get {
    param([string]$Path)
    $capturedAt = Get-Date
    $watch = [System.Diagnostics.Stopwatch]::StartNew()
    try {
        $uri = $BaseUrl.TrimEnd('/') + $Path
        $response = $client.GetAsync($uri).GetAwaiter().GetResult()
        $body = $response.Content.ReadAsStringAsync().GetAwaiter().GetResult()
        $watch.Stop()
        $code = [int]$response.StatusCode
        if (-not $response.IsSuccessStatusCode) {
            return [pscustomobject]@{
                Ok = $false; At = $capturedAt; LatencyMs = $watch.Elapsed.TotalMilliseconds
                StatusCode = $code; Body = $body; Error = "HTTP $code"
            }
        }
        return [pscustomobject]@{
            Ok = $true; At = $capturedAt; LatencyMs = $watch.Elapsed.TotalMilliseconds
            StatusCode = $code; Body = $body; Error = ""
        }
    } catch {
        $watch.Stop()
        return [pscustomobject]@{
            Ok = $false; At = $capturedAt; LatencyMs = $watch.Elapsed.TotalMilliseconds
            StatusCode = 0; Body = ""; Error = $_.Exception.Message
        }
    }
}

function Write-RawJsonLine {
    param(
        [System.IO.StreamWriter]$Writer,
        $Response
    )
    if ($Response.Ok) {
        $prefix = [ordered]@{
            captured_at = $Response.At.ToString('o')
            latency_ms = [Math]::Round($Response.LatencyMs, 3)
            http_status = $Response.StatusCode
        } | ConvertTo-Json -Compress
        $Writer.WriteLine($prefix.Substring(0, $prefix.Length - 1) + ',"payload":' +
                          $Response.Body + '}')
    } else {
        $line = [ordered]@{
            captured_at = $Response.At.ToString('o')
            latency_ms = [Math]::Round($Response.LatencyMs, 3)
            http_status = $Response.StatusCode
            error = $Response.Error
        } | ConvertTo-Json -Compress
        $Writer.WriteLine($line)
    }
    $Writer.Flush()
}

function Write-HttpError {
    param([string]$Endpoint, $Response)
    Write-CsvRow $errorsWriter @(
        $Response.At.ToString('o'), $Endpoint, $Response.StatusCode,
        [Math]::Round($Response.LatencyMs, 3), $Response.Error
    )
}

function Save-DiagnosticLog {
    param([string]$Path)
    $response = Invoke-M3Get "/api/diagnostic-log"
    if ($response.Ok) {
        [System.IO.File]::WriteAllText($Path, $response.Body, $Utf8NoBom)
    } else {
        Write-HttpError "/api/diagnostic-log" $response
        [System.IO.File]::WriteAllText($Path, "unavailable: $($response.Error)`n", $Utf8NoBom)
    }
}

$csvHeader = @(
    'captured_at','elapsed_s','status_latency_ms','firmware_latency_ms',
    'library_latency_ms','firmware_version','running_slot','ota_state',
    'library_tracks','controller_present','midi_in','midi_out','usb_audio',
    'deck1_title','deck2_title','deck1_playing','deck2_playing',
    'deck1_loop','deck2_loop','deck1_position_ms','deck2_position_ms',
    'deck1_source_rate','deck2_source_rate','deck1_pitch_percent',
    'deck2_pitch_percent','output_sample_rate','output_late_count',
    'output_late_max_us','pcm_underrun1','pcm_underrun2','uac_dropped_blocks',
    'uac_overflow_frames','uac_underflow_frames','uac_ring_state',
    'uac_data_loss','phase_mix_max_us','phase_main_max_us',
    'service_log_queue_depth','service_log_dropped',
    'internal_free','psram_free'
)
Write-CsvRow $csvWriter $csvHeader
Write-CsvRow $errorsWriter @('captured_at','endpoint','http_status','latency_ms','error')

$hardFailures = [System.Collections.Generic.List[string]]::new()
$investigations = [System.Collections.Generic.List[string]]::new()
$lateEvents = [System.Collections.Generic.Queue[datetime]]::new()
$statusSamples = 0
$statusErrors = 0
$libraryErrors = 0
$firmwareErrors = 0
$maxStatusLatencyMs = 0.0
$maxLibraryLatencyMs = 0.0
$maxFirmwareLatencyMs = 0.0
$lastStatusSuccess = $StartedAt
$lastLibraryCount = $null
$lastLibraryLatency = $null
$lastFirmwareLatency = $null
$firmware = $null
$identity = $null
$previousUptime = 0
$baseline = $null
$finalStatus = $null
$firstControllerReadyS = $null
$firstLibraryReadyS = $null
$timingWorkloadSamples = 0
$timingWorkloadViolations = 0
$exitCode = 0

try {
    Save-DiagnosticLog $initialLogPath

    $firmwareResponse = Invoke-M3Get "/api/firmware"
    Write-RawJsonLine $firmwareWriter $firmwareResponse
    if (-not $firmwareResponse.Ok) {
        Write-HttpError "/api/firmware" $firmwareResponse
        throw "Initial firmware request failed: $($firmwareResponse.Error)"
    }
    $firmware = $firmwareResponse.Body | ConvertFrom-Json
    $initialVersion = [string]$firmware.running_version
    $initialSlot = [string]$firmware.running_slot
    $initialOtaState = [string]$firmware.state
    $lastFirmwareLatency = $firmwareResponse.LatencyMs
    $maxFirmwareLatencyMs = $lastFirmwareLatency

    $libraryResponse = Invoke-M3Get "/api/library"
    Write-RawJsonLine $libraryWriter $libraryResponse
    if (-not $libraryResponse.Ok) {
        Write-HttpError "/api/library" $libraryResponse
        throw "Initial library request failed: $($libraryResponse.Error)"
    }
    $library = $libraryResponse.Body | ConvertFrom-Json
    $lastLibraryCount = Get-LibraryCount $library
    $lastLibraryLatency = $libraryResponse.LatencyMs
    $maxLibraryLatencyMs = $lastLibraryLatency
    if ($lastLibraryCount -eq $ExpectedLibraryTracks) { $firstLibraryReadyS = 0.0 }

    $initialStatusResponse = Invoke-M3Get "/api/status"
    Write-RawJsonLine $statusWriter $initialStatusResponse
    if (-not $initialStatusResponse.Ok) {
        Write-HttpError "/api/status" $initialStatusResponse
        throw "Initial status request failed: $($initialStatusResponse.Error)"
    }
    $baseline = $initialStatusResponse.Body | ConvertFrom-Json
    $expectedProjects = @{ m3='main-deck-m3'; jc4880='main-deck-p4'; jc1060='main-deck-jc1060' }
    $identity = [ordered]@{
        board_id = Require-Value $baseline 'board_id'
        project = Require-Value $baseline 'project'
        source_sha = Require-Value $baseline 'source_sha'
        source_dirty = Require-Value $baseline 'source_dirty'
        image_elf_sha256 = Require-Value $firmware 'image_elf_sha256'
    }
    if ($identity.board_id -cne $ExpectedBoard -or $identity.project -cne $expectedProjects[$ExpectedBoard] -or
        $identity.source_sha -cnotmatch '^[0-9a-f]{40}$' -or $identity.source_dirty -ne $false -or
        $identity.image_elf_sha256 -cnotmatch '^[0-9a-f]{64}$') { throw 'Wrong board, dirty or invalid image identity' }
    if ($ExpectedSourceSha -and $identity.source_sha -cne $ExpectedSourceSha) { throw 'Wrong expected source SHA' }
    if ($ExpectedElfSha256 -and $identity.image_elf_sha256 -cne $ExpectedElfSha256) { throw 'Wrong expected image ELF SHA' }
    Assert-Status $baseline $identity
    Assert-Identity $firmware $identity $true
    $resourceResponse = Invoke-M3Get '/api/resources'
    Write-RawJsonLine $resourcesWriter $resourceResponse
    if (-not $resourceResponse.Ok) { throw 'Initial resource telemetry request failed' }
    $resources = $resourceResponse.Body | ConvertFrom-Json
    foreach ($field in @('allocation_failures','critical_allocation_failures','stack_min_bytes','stack_sample_ms')) {
        [void](Require-Value $resources $field)
    }
    $baselineCriticalAllocations = [uint64]$resources.critical_allocation_failures
    $previousUptime = [uint64]$baseline.uptime_ms
    $lastStatusSuccess = Get-Date

    $baselineCounters = [ordered]@{
        output_late = [uint64]$baseline.diagnostics.output_late_count
        pcm1 = [uint64]$baseline.diagnostics.pcm_underrun1
        pcm2 = [uint64]$baseline.diagnostics.pcm_underrun2
        uac_dropped = [uint64]$baseline.diagnostics.usb_headphones.dropped_blocks
        uac_overflow = [uint64]$baseline.diagnostics.usb_headphones.overflow_frames
        uac_underflow = [uint64]$baseline.diagnostics.usb_headphones.underflow_frames
        service_log_dropped = [uint64]$baseline.service_log.dropped
    }
    $previousLate = $baselineCounters.output_late

    $metadata = [ordered]@{
        started_at = $StartedAt.ToString('o')
        base_url = $BaseUrl
        mode = $Mode
        duration_seconds = $RunSeconds
        poll_interval_ms = $PollIntervalMs
        library_every = $LibraryEvery
        firmware_every = $FirmwareEvery
        expected_library_tracks = $ExpectedLibraryTracks
        initial_version = $initialVersion
        identity = $identity
        initial_slot = $initialSlot
        initial_ota_state = $initialOtaState
        initial_library_tracks = $lastLibraryCount
        baseline = $baselineCounters
    }
    [System.IO.File]::WriteAllText(
        (Join-Path $OutputDirectory 'metadata.json'),
        ($metadata | ConvertTo-Json -Depth 6), $Utf8NoBom)

    Write-Host "M3 reliability monitor"
    Write-Host "  output:  $OutputDirectory"
    Write-Host "  image:   $initialVersion / $initialSlot"
    Write-Host "  library: $lastLibraryCount tracks"
    Write-Host "  mode:    $Mode"
    Write-Host "  runtime: $RunSeconds s at ${PollIntervalMs} ms"

    $runWatch = [System.Diagnostics.Stopwatch]::StartNew()
    $sampleIndex = 0
    while ($runWatch.Elapsed.TotalSeconds -lt $RunSeconds) {
        $iterationStart = [System.Diagnostics.Stopwatch]::StartNew()
        $sampleIndex++

        $statusResponse = Invoke-M3Get "/api/status"
        Write-RawJsonLine $statusWriter $statusResponse
        if (-not $statusResponse.Ok) {
            $statusErrors++
            Write-HttpError "/api/status" $statusResponse
            if (((Get-Date) - $lastStatusSuccess).TotalSeconds -ge $MaxApiOutageSeconds) {
                $message = "status API unavailable for at least $MaxApiOutageSeconds seconds"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                break
            }
        } else {
            $lastStatusSuccess = Get-Date
            $statusSamples++
            if ($statusResponse.LatencyMs -gt $maxStatusLatencyMs) {
                $maxStatusLatencyMs = $statusResponse.LatencyMs
            }
            $status = $statusResponse.Body | ConvertFrom-Json
            Assert-Status $status $identity
            if ([uint64]$status.uptime_ms -lt $previousUptime) { throw 'Device reboot: uptime decreased' }
            $previousUptime = [uint64]$status.uptime_ms
            foreach ($field in @('output_late_count','pcm_underrun1','pcm_underrun2')) {
                if ([uint64]$status.diagnostics.$field -lt [uint64]$baseline.diagnostics.$field) {
                    throw "Device counter reset: $field"
                }
            }
            $finalStatus = $status

            if ($status.diagnostics.output_late_count -gt $previousLate) {
                $newLateCount = [int]([uint64]$status.diagnostics.output_late_count - $previousLate)
                for ($i = 0; $i -lt $newLateCount; $i++) { $lateEvents.Enqueue((Get-Date)) }
                $previousLate = [uint64]$status.diagnostics.output_late_count
            }
            while ($lateEvents.Count -gt 0 -and
                   (((Get-Date) - $lateEvents.Peek()).TotalSeconds -gt 60)) {
                [void]$lateEvents.Dequeue()
            }
            if ([uint64]$status.diagnostics.output_late_max_us -gt
                    [uint64]$baseline.diagnostics.output_late_max_us -and
                [uint64]$status.diagnostics.output_late_max_us -gt 15000) {
                $message = "output_late_max_us exceeded 15000"
                if (-not $investigations.Contains($message)) { $investigations.Add($message) }
            }
            if ($lateEvents.Count -ge 3) {
                $message = "three or more output-late events occurred within one minute"
                if (-not $investigations.Contains($message)) { $investigations.Add($message) }
            }

            $bothPlaying = [bool]$status.deck1.playing -and [bool]$status.deck2.playing
            if ([uint64]$status.diagnostics.pcm_underrun1 -gt $baselineCounters.pcm1 -or
                [uint64]$status.diagnostics.pcm_underrun2 -gt $baselineCounters.pcm2) {
                $message = "PCM underrun counter increased"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            if ([uint64]$status.diagnostics.usb_headphones.dropped_blocks -gt $baselineCounters.uac_dropped) {
                $message = "UAC dropped-block counter increased"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            if ([uint64]$status.diagnostics.usb_headphones.overflow_frames -gt $baselineCounters.uac_overflow) {
                $message = "UAC overflow counter increased"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            if ($bothPlaying -and
                [uint64]$status.diagnostics.usb_headphones.underflow_frames -gt $baselineCounters.uac_underflow) {
                $message = "UAC underflow counter increased during dual-deck playback"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            if ([bool]$status.diagnostics.usb_headphones.data_loss) {
                $message = "UAC active data_loss became true"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            if ([uint64]$status.service_log.dropped -gt $baselineCounters.service_log_dropped) {
                $message = "service-log dropped counter increased"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }
            $controllerReady = [bool]$status.controller.present -and
                [bool]$status.controller.midi_in -and
                [bool]$status.controller.midi_out -and
                [bool]$status.controller.usb_audio
            if ($controllerReady -and $null -eq $firstControllerReadyS) {
                $firstControllerReadyS = $runWatch.Elapsed.TotalSeconds
            }
            if ($Mode -eq 'TimingSoak' -and -not $controllerReady) {
                $message = "FLX4 lost MIDI In/Out or UAC readiness"
                if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
            }

            if ($Mode -eq 'TimingSoak') {
                $timingWorkloadSamples++
                $ratePairOk =
                    (([uint32]$status.diagnostics.deck_sample_rate1 -eq 44100 -and
                      [uint32]$status.diagnostics.deck_sample_rate2 -eq 48000) -or
                     ([uint32]$status.diagnostics.deck_sample_rate1 -eq 48000 -and
                      [uint32]$status.diagnostics.deck_sample_rate2 -eq 44100))
                $pitchOk = [Math]::Abs([double]$status.deck1.pitch_percent - 5.0) -le 0.15 -and
                           [Math]::Abs([double]$status.deck2.pitch_percent + 5.0) -le 0.15
                $workloadOk = $bothPlaying -and [bool]$status.deck1.master_tempo -and
                    [bool]$status.deck2.master_tempo -and [bool]$status.deck1.loop_active -and
                    [bool]$status.deck2.loop_active -and $ratePairOk -and $pitchOk -and
                    ([uint32]$status.diagnostics.output_sample_rate -eq 48000)
                if (-not $workloadOk) {
                    $timingWorkloadViolations++
                    $message = "TimingSoak workload left the required dual-deck mixed-rate loop/pitch profile"
                    if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                }
            }

            if (($sampleIndex % $LibraryEvery) -eq 0) {
                $libraryResponse = Invoke-M3Get "/api/library"
                Write-RawJsonLine $libraryWriter $libraryResponse
                if ($libraryResponse.Ok) {
                    $lastLibraryLatency = $libraryResponse.LatencyMs
                    if ($lastLibraryLatency -gt $maxLibraryLatencyMs) {
                        $maxLibraryLatencyMs = $lastLibraryLatency
                    }
                    $library = $libraryResponse.Body | ConvertFrom-Json
                    $lastLibraryCount = Get-LibraryCount $library
                    if ($lastLibraryCount -eq $ExpectedLibraryTracks -and
                        $null -eq $firstLibraryReadyS) {
                        $firstLibraryReadyS = $runWatch.Elapsed.TotalSeconds
                    }
                    if ($Mode -eq 'TimingSoak' -and
                        $lastLibraryCount -ne $ExpectedLibraryTracks) {
                        $message = "library count changed from expected $ExpectedLibraryTracks"
                        if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                    }
                } else {
                    $libraryErrors++
                    Write-HttpError "/api/library" $libraryResponse
                }
            }

            if (($sampleIndex % $FirmwareEvery) -eq 0) {
                $firmwareResponse = Invoke-M3Get "/api/firmware"
                Write-RawJsonLine $firmwareWriter $firmwareResponse
                if ($firmwareResponse.Ok) {
                    $lastFirmwareLatency = $firmwareResponse.LatencyMs
                    if ($lastFirmwareLatency -gt $maxFirmwareLatencyMs) {
                        $maxFirmwareLatencyMs = $lastFirmwareLatency
                    }
                    $firmware = $firmwareResponse.Body | ConvertFrom-Json
                    Assert-Identity $firmware $identity $true
                    $resourceResponse = Invoke-M3Get '/api/resources'
                    Write-RawJsonLine $resourcesWriter $resourceResponse
                    if (-not $resourceResponse.Ok) { throw 'Resource telemetry request failed' }
                    $resources = $resourceResponse.Body | ConvertFrom-Json
                    $critical = [uint64](Require-Value $resources 'critical_allocation_failures')
                    if ($critical -gt $baselineCriticalAllocations) {
                        $message = 'Critical allocation failure counter increased'
                        if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                    }
                    if ([string]$firmware.running_version -ne $initialVersion -or
                        [string]$firmware.running_slot -ne $initialSlot) {
                        $message = "firmware version or running slot changed"
                        if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                    }
                    if ([string]$firmware.state -ne 'idle') {
                        $message = "OTA state left idle"
                        if (-not $hardFailures.Contains($message)) { $hardFailures.Add($message) }
                    }
                } else {
                    $firmwareErrors++
                    Write-HttpError "/api/firmware" $firmwareResponse
                }
            }

            Write-CsvRow $csvWriter @(
                $statusResponse.At.ToString('o'),
                [Math]::Round($runWatch.Elapsed.TotalSeconds, 3),
                [Math]::Round($statusResponse.LatencyMs, 3),
                $(if ($null -eq $lastFirmwareLatency) { '' } else { [Math]::Round($lastFirmwareLatency, 3) }),
                $(if ($null -eq $lastLibraryLatency) { '' } else { [Math]::Round($lastLibraryLatency, 3) }),
                $firmware.running_version, $firmware.running_slot, $firmware.state,
                $lastLibraryCount,
                $status.controller.present, $status.controller.midi_in,
                $status.controller.midi_out, $status.controller.usb_audio,
                $status.deck1.title, $status.deck2.title,
                $status.deck1.playing, $status.deck2.playing,
                $status.deck1.loop_active, $status.deck2.loop_active,
                $status.deck1.position_ms, $status.deck2.position_ms,
                $status.diagnostics.deck_sample_rate1,
                $status.diagnostics.deck_sample_rate2,
                $status.deck1.pitch_percent, $status.deck2.pitch_percent,
                $status.diagnostics.output_sample_rate,
                $status.diagnostics.output_late_count,
                $status.diagnostics.output_late_max_us,
                $status.diagnostics.pcm_underrun1,
                $status.diagnostics.pcm_underrun2,
                $status.diagnostics.usb_headphones.dropped_blocks,
                $status.diagnostics.usb_headphones.overflow_frames,
                $status.diagnostics.usb_headphones.underflow_frames,
                $status.diagnostics.usb_headphones.ring_state,
                $status.diagnostics.usb_headphones.data_loss,
                $status.diagnostics.phase_mix_us,
                $status.diagnostics.phase_main_us,
                $status.service_log.queue_depth, $status.service_log.dropped,
                $status.diagnostics.internal_free, $status.diagnostics.psram_free
            )
        }

        $remainingMs = $PollIntervalMs - [int]$iterationStart.Elapsed.TotalMilliseconds
        if ($remainingMs -gt 0) { Start-Sleep -Milliseconds $remainingMs }
    }
    $runWatch.Stop()

    Save-DiagnosticLog $finalLogPath

    if ($null -eq $finalStatus) {
        $hardFailures.Add("no successful status sample was captured")
    }
    if ($Mode -eq 'UsbRecovery') {
        if ($null -eq $firstControllerReadyS) {
            $hardFailures.Add("FLX4 did not become fully ready")
        } elseif ($firstControllerReadyS -gt 15.0) {
            $investigations.Add("FLX4 recovery exceeded 15 seconds")
        }
        if ($lastLibraryCount -ne $ExpectedLibraryTracks) {
            $hardFailures.Add("library did not recover to $ExpectedLibraryTracks tracks")
        } elseif ($null -ne $firstLibraryReadyS -and $firstLibraryReadyS -gt 20.0) {
            $investigations.Add("library recovery exceeded 20 seconds")
        }
    }
    $finalCounters = if ($null -ne $finalStatus) {
        [ordered]@{
            output_late = [uint64]$finalStatus.diagnostics.output_late_count
            pcm1 = [uint64]$finalStatus.diagnostics.pcm_underrun1
            pcm2 = [uint64]$finalStatus.diagnostics.pcm_underrun2
            uac_dropped = [uint64]$finalStatus.diagnostics.usb_headphones.dropped_blocks
            uac_overflow = [uint64]$finalStatus.diagnostics.usb_headphones.overflow_frames
            uac_underflow = [uint64]$finalStatus.diagnostics.usb_headphones.underflow_frames
            service_log_dropped = [uint64]$finalStatus.service_log.dropped
        }
    } else { $null }
    $deltas = if ($null -ne $finalCounters) {
        [ordered]@{
            output_late = $finalCounters.output_late - $baselineCounters.output_late
            pcm1 = $finalCounters.pcm1 - $baselineCounters.pcm1
            pcm2 = $finalCounters.pcm2 - $baselineCounters.pcm2
            uac_dropped = $finalCounters.uac_dropped - $baselineCounters.uac_dropped
            uac_overflow = $finalCounters.uac_overflow - $baselineCounters.uac_overflow
            uac_underflow = $finalCounters.uac_underflow - $baselineCounters.uac_underflow
            service_log_dropped = $finalCounters.service_log_dropped - $baselineCounters.service_log_dropped
        }
    } else { $null }

    if ($hardFailures.Count -gt 0) { $exitCode = 1 }
    elseif ($investigations.Count -gt 0) { $exitCode = 2 }

    $result = [ordered]@{
        result = if ($exitCode -eq 0) { 'pass' } elseif ($exitCode -eq 2) { 'investigate' } else { 'fail' }
        exit_code = $exitCode
        started_at = $StartedAt.ToString('o')
        finished_at = (Get-Date).ToString('o')
        requested_duration_seconds = $RunSeconds
        mode = $Mode
        status_samples = $statusSamples
        http_errors = [ordered]@{
            status = $statusErrors; library = $libraryErrors; firmware = $firmwareErrors
        }
        latency_max_ms = [ordered]@{
            status = [Math]::Round($maxStatusLatencyMs, 3)
            library = [Math]::Round($maxLibraryLatencyMs, 3)
            firmware = [Math]::Round($maxFirmwareLatencyMs, 3)
        }
        firmware = [ordered]@{
            version = $initialVersion; slot = $initialSlot; ota_state = $initialOtaState
        }
        identity = $identity
        physical_operator_acceptance = 'NOT RUN'
        resources = $resources
        library_tracks = $lastLibraryCount
        recovery_seconds = [ordered]@{
            controller_ready = $firstControllerReadyS
            library_ready = $firstLibraryReadyS
        }
        timing_workload = [ordered]@{
            samples = $timingWorkloadSamples
            violations = $timingWorkloadViolations
        }
        baseline = $baselineCounters
        final = $finalCounters
        delta = $deltas
        output_late_max_us = if ($null -ne $finalStatus) { [uint64]$finalStatus.diagnostics.output_late_max_us } else { $null }
        hard_failures = @($hardFailures)
        investigation_triggers = @($investigations)
    }
    [System.IO.File]::WriteAllText($summaryPath,
        ($result | ConvertTo-Json -Depth 8), $Utf8NoBom)
    Write-Host ($result | ConvertTo-Json -Depth 8)
} catch {
    $exitCode = 1
    $failure = [ordered]@{
        result = 'fail'
        exit_code = 1
        started_at = $StartedAt.ToString('o')
        finished_at = (Get-Date).ToString('o')
        mode = $Mode
        identity = $identity
        physical_operator_acceptance = 'NOT RUN'
        hard_failures = @($_.Exception.Message)
    }
    [System.IO.File]::WriteAllText($summaryPath,
        ($failure | ConvertTo-Json -Depth 8), $Utf8NoBom)
    Write-Host ($failure | ConvertTo-Json -Depth 8)
} finally {
    $statusWriter.Dispose()
    $libraryWriter.Dispose()
    $firmwareWriter.Dispose()
    $resourcesWriter.Dispose()
    $csvWriter.Dispose()
    $errorsWriter.Dispose()
    $client.Dispose()
}

exit $exitCode
