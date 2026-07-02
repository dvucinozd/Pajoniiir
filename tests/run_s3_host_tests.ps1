param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Gcc = Get-Command gcc -ErrorAction Stop

function Invoke-Step {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$Executable,
        [string[]]$Arguments = @()
    )

    Write-Host "==> $Name"
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments
        if ($LASTEXITCODE -ne 0) {
            throw "$Name failed with exit code $LASTEXITCODE"
        }
    } finally {
        Pop-Location
    }
}

function Assert-FileContains {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Patterns
    )

    Write-Host "==> static $Name"
    foreach ($pattern in $Patterns) {
        if (-not (Select-String -LiteralPath $Path -Pattern $pattern -SimpleMatch)) {
            throw "$Name missing expected pattern '$pattern' in $Path"
        }
    }
}

Assert-FileContains `
    -Name "flx4 mode split" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/flx4_midi_host/Kconfig") `
    -Patterns @("DDJ_FLX4_TRANSLATE_TO_P4")

Assert-FileContains `
    -Name "app mode split" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("CONFIG_DDJ_FLX4_TRANSLATE_TO_P4", "mode: DDJ-FLX4 USB MIDI host raw logger", "mode: DDJ-FLX4 USB MIDI translator")

Assert-FileContains `
    -Name "flx4 host callback API" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/flx4_midi_host/include/flx4_midi_host.h") `
    -Patterns @("flx4_midi_message_cb_t", "flx4_midi_host_set_message_callback")

Assert-FileContains `
    -Name "flx4 translator queue" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("s_flx4_event_queue", "flx4_translator_task", "s_flx4_coalesced_count")

Assert-FileContains `
    -Name "legacy panel coalescing" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/panel_io/panel_io.c") `
    -Patterns @("s_pending_jog", "s_pending_browse", "panel_flush_pending_motion")

Assert-FileContains `
    -Name "legacy encoder pull config" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/panel_io/panel_encoder.c") `
    -Patterns @("GPIO_PULLUP_ENABLE", "configure_encoder_gpio")

Assert-FileContains `
    -Name "legacy midi burst limit" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/midi_compat/midi_compat.c") `
    -Patterns @("MIDI_COMPAT_MAX_ENCODER_BURST")

Assert-FileContains `
    -Name "flx4 led feedback skips uninitialised legacy panel fallback" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("s_panel_led_fallback_enabled", "panel_event_queue != NULL", "if (s_panel_led_fallback_enabled && id < LED_COUNT)")

Assert-FileContains `
    -Name "control link rx stack covers extended led bursts" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("CTRL_RX_TASK_STACK", "4096", 'xTaskCreate(uart_rx_task, "ctrl_rx", CTRL_RX_TASK_STACK')

$tests = @(
    @{
        Name = "flx4_midi_host"
        Dir = "tests/flx4_midi_host"
        Target = "test_flx4_midi_host.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DFLX4_MIDI_HOST_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/flx4_midi_host/include",
            "-o", "test_flx4_midi_host.exe",
            "test_flx4_midi_host.c",
            "../../firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c"
        )
    },
    @{
        Name = "flx4_map"
        Dir = "tests/flx4_midi_host"
        Target = "test_flx4_map.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DFLX4_MIDI_HOST_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/flx4_midi_host/include",
            "-I../../firmware/control-board-s3/components/control_link/include",
            "-I../../firmware/control-board-s3/components/panel_io/include",
            "-o", "test_flx4_map.exe",
            "test_flx4_map.c",
            "../../firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c",
            "../../firmware/control-board-s3/components/flx4_midi_host/flx4_map.c"
        )
    },
    @{
        Name = "flx4_led_midi"
        Dir = "tests/flx4_midi_host"
        Target = "test_flx4_led_midi.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DFLX4_MIDI_HOST_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/flx4_midi_host/include",
            "-I../../firmware/control-board-s3/components/control_link/include",
            "-I../../firmware/control-board-s3/components/panel_io/include",
            "-o", "test_flx4_led_midi.exe",
            "test_flx4_led_midi.c",
            "../../firmware/control-board-s3/components/control_link/flx4_led_midi.c"
        )
    },
    @{
        Name = "control_link_protocol"
        Dir = "tests/control_link_protocol"
        Target = "test_control_link_protocol.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-Istubs",
            "-I../../firmware/control-board-s3/components/panel_io/include",
            "-I../../firmware/control-board-s3/components/control_link/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-o", "test_control_link_protocol.exe",
            "test_control_link_protocol.c",
            "s3_constants.c",
            "p4_constants.c"
        )
    },
    @{
        Name = "flx4_usb_audio"
        Dir = "tests/flx4_usb_audio"
        Target = "test_flx4_uac_descriptors.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-I../../firmware/control-board-s3/components/flx4_usb_audio/include",
            "-o", "test_flx4_uac_descriptors.exe",
            "test_flx4_uac_descriptors.c",
            "../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c"
        )
    },
    @{
        Name = "flx4_uac_packetizer"
        Dir = "tests/flx4_usb_audio"
        Target = "test_flx4_uac_packetizer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-I../../firmware/control-board-s3/components/flx4_usb_audio/include",
            "-o", "test_flx4_uac_packetizer.exe",
            "test_flx4_uac_packetizer.c",
            "../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_packetizer.c"
        )
    }
)

$created = New-Object System.Collections.Generic.List[string]

foreach ($test in $tests) {
    $dir = Join-Path $RepoRoot $test.Dir
    $target = Join-Path $dir $test.Target
    Invoke-Step -Name "build $($test.Name)" -WorkingDirectory $dir -Executable $Gcc.Source -Arguments $test.Args
    $created.Add($target)
    Invoke-Step -Name "run $($test.Name)" -WorkingDirectory $dir -Executable $target
}

if (-not $KeepArtifacts) {
    foreach ($path in $created) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
}

Write-Host "S3 host tests passed."
