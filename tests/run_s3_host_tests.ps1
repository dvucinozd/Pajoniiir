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

function Assert-FileNotContains {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$Patterns
    )

    Write-Host "==> static $Name"
    foreach ($pattern in $Patterns) {
        if (Select-String -LiteralPath $Path -Pattern $pattern -SimpleMatch) {
            throw "$Name found forbidden pattern '$pattern' in $Path"
        }
    }
}

function Assert-FilePatternOrder {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$First,
        [Parameter(Mandatory = $true)][string]$Second
    )

    Write-Host "==> static $Name"
    $text = Get-Content -LiteralPath $Path -Raw
    $firstIndex = $text.IndexOf($First, [StringComparison]::Ordinal)
    $secondIndex = $text.IndexOf($Second, [StringComparison]::Ordinal)
    if ($firstIndex -lt 0) {
        throw "$Name missing first pattern '$First' in $Path"
    }
    if ($secondIndex -lt 0) {
        throw "$Name missing second pattern '$Second' in $Path"
    }
    if ($firstIndex -ge $secondIndex) {
        throw "$Name expected '$First' before '$Second' in $Path"
    }
}

function Assert-CFunctionDoesNotContain {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$FunctionSignature,
        [Parameter(Mandatory = $true)][string]$ForbiddenPattern
    )

    Write-Host "==> static $Name"
    $text = Get-Content -LiteralPath $Path -Raw
    $start = $text.IndexOf($FunctionSignature, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "$Name missing function signature '$FunctionSignature' in $Path"
    }
    $brace = $text.IndexOf("{", $start, [StringComparison]::Ordinal)
    if ($brace -lt 0) {
        throw "$Name missing function body for '$FunctionSignature' in $Path"
    }

    $depth = 0
    $end = -1
    for ($i = $brace; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq "{") {
            $depth++
        } elseif ($text[$i] -eq "}") {
            $depth--
            if ($depth -eq 0) {
                $end = $i
                break
            }
        }
    }
    if ($end -lt 0) {
        throw "$Name found unterminated function body for '$FunctionSignature' in $Path"
    }

    $body = $text.Substring($brace, $end - $brace + 1)
    if ($body.IndexOf($ForbiddenPattern, [StringComparison]::Ordinal) -ge 0) {
        throw "$Name found forbidden pattern '$ForbiddenPattern' in '$FunctionSignature'"
    }
}

function Assert-CFunctionPatternOrder {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$FunctionSignature,
        [Parameter(Mandatory = $true)][string]$First,
        [Parameter(Mandatory = $true)][string]$Second
    )

    Write-Host "==> static $Name"
    $text = Get-Content -LiteralPath $Path -Raw
    $start = $text.LastIndexOf($FunctionSignature, [StringComparison]::Ordinal)
    if ($start -lt 0) {
        throw "$Name missing function signature '$FunctionSignature' in $Path"
    }
    $brace = $text.IndexOf("{", $start, [StringComparison]::Ordinal)
    if ($brace -lt 0) {
        throw "$Name missing function body for '$FunctionSignature' in $Path"
    }

    $depth = 0
    $end = -1
    for ($i = $brace; $i -lt $text.Length; $i++) {
        if ($text[$i] -eq "{") {
            $depth++
        } elseif ($text[$i] -eq "}") {
            $depth--
            if ($depth -eq 0) {
                $end = $i
                break
            }
        }
    }
    if ($end -lt 0) {
        throw "$Name found unterminated function body for '$FunctionSignature' in $Path"
    }

    $body = $text.Substring($brace, $end - $brace + 1)
    $firstIndex = $body.IndexOf($First, [StringComparison]::Ordinal)
    $secondIndex = $body.IndexOf($Second, [StringComparison]::Ordinal)
    if ($firstIndex -lt 0) {
        throw "$Name missing first pattern '$First' in '$FunctionSignature'"
    }
    if ($secondIndex -lt 0) {
        throw "$Name missing second pattern '$Second' in '$FunctionSignature'"
    }
    if ($firstIndex -ge $secondIndex) {
        throw "$Name expected '$First' before '$Second' in '$FunctionSignature'"
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

Assert-FileContains `
    -Name "control link semantic send propagates UART errors" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("static esp_err_t send_frame_checked", "return send_frame_checked(frame, `"semantic event`")")

Assert-FileContains `
    -Name "flx4 continuous controls are coalesced" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("CTRL_ID_CH1_TRIM", "CTRL_ID_CH2_TRIM", "CTRL_ID_CH1_EQ_HIGH", "CTRL_ID_CH2_EQ_HIGH", "CTRL_ID_CH1_EQ_MID", "CTRL_ID_CH2_EQ_MID", "CTRL_ID_CH1_EQ_LOW", "CTRL_ID_CH2_EQ_LOW", "CTRL_ID_CH1_FILTER", "CTRL_ID_CH2_FILTER", "CTRL_ID_MASTER_VOLUME", "CTRL_ID_HEADPHONE_MIX", "CTRL_ID_HEADPHONE_LEVEL", "CTRL_ID_BEAT_FX_DEPTH")

Assert-FileContains `
    -Name "p4 audio link stats use atomic counters" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c") `
    -Patterns @("__atomic_fetch_add", "stats_load(&s_stats")

Assert-FileContains `
    -Name "xiao user status led uses gpio21 active low" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/status_led/status_led.c") `
    -Patterns @("#define STATUS_LED_GPIO        21", "gpio_set_level(STATUS_LED_GPIO, active ? 0 : 1)", "XIAO ESP32S3 user LED on GPIO%d")

Assert-FileNotContains `
    -Name "xiao user status led avoids ws2812 strip driver" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/status_led/status_led.c") `
    -Patterns @("led_strip", "LED_MODEL_WS2812", "GPIO48")

Assert-FileContains `
    -Name "wifi debug log component is optional and async" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/wifi_debug_log/wifi_debug_log.c") `
    -Patterns @("CONFIG_WIFI_DEBUG_LOG_ENABLED", "esp_log_set_vprintf", "xQueueSend", "sendto", "wifi_debug_log_task", "wifi_debug_log_connect_task", "xTaskCreate(wifi_debug_log_connect_task")

Assert-CFunctionPatternOrder `
    -Name "wifi debug log opens UDP socket after netif init" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/wifi_debug_log/wifi_debug_log.c") `
    -FunctionSignature "static esp_err_t wifi_debug_log_start_wifi(void)" `
    -First "esp_netif_init()" `
    -Second "wifi_debug_log_open_udp_socket()"

Assert-CFunctionDoesNotContain `
    -Name "wifi debug log init does not open UDP socket before netif" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/wifi_debug_log/wifi_debug_log.c") `
    -FunctionSignature "esp_err_t wifi_debug_log_init(void)" `
    -ForbiddenPattern "socket(AF_INET, SOCK_DGRAM, IPPROTO_IP)"

Assert-FileContains `
    -Name "wifi debug log keeps credentials in local config" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/wifi_debug_log/Kconfig") `
    -Patterns @("WIFI_DEBUG_LOG_SSID", "WIFI_DEBUG_LOG_PASSWORD", "WIFI_DEBUG_LOG_HOST", "WIFI_DEBUG_LOG_PORT")

Assert-FileContains `
    -Name "app starts wifi debug log before subsystems" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @('#include "wifi_debug_log.h"', "wifi_debug_log_init();")

Assert-FilePatternOrder `
    -Name "app logs boot before starting wifi debug log" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -First 'ESP_LOGI(TAG, "DDJ-FFL4 control board firmware starting");' `
    -Second "wifi_debug_log_init();"

Assert-FileContains `
    -Name "s3 debug ap control link dispatch" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("CTRL_ID_S3_DEBUG_AP", "s3_debug_ap_request")

Assert-FileContains `
    -Name "s3 debug ap status callback registered after control link" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("s3_debug_ap_init", "s3_debug_ap_set_status_callback", "CTRL_ID_S3_DEBUG_AP")

Assert-FileContains `
    -Name "s3 debug ap runtime hosts open softap" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("WIFI_MODE_AP", "PajoNiiiR-S3-DEBUG", "192.168.4.1", "esp_wifi_start")

Assert-FileContains `
    -Name "s3 debug ap exposes read only http log page" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("httpd_start", "S3 Debug Log", "/events", "text/event-stream")

Assert-FileContains `
    -Name "s3 debug ap log hook remains non blocking" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c") `
    -Patterns @("esp_log_set_vprintf", "s_prev_vprintf", "s3_debug_log_ring_append")

# The FLX4 controller-profile fixture must stay in sync with profile.json.
# Regenerate through the compiler and byte-compare against the committed
# .s3bin (skipped with a warning when python is unavailable).
$python = Get-Command python -ErrorAction SilentlyContinue
if ($python) {
    Write-Host "==> controller profile fixture is up to date"
    $fixtureJson = Join-Path $RepoRoot "controllers/pioneer_ddj_flx4/profile.json"
    $fixtureBin = Join-Path $RepoRoot "controllers/pioneer_ddj_flx4/profile.s3bin"
    $fixtureTmp = Join-Path $RepoRoot "tests/controller_profile/profile_regen.s3bin"
    & $python.Source (Join-Path $RepoRoot "tools/controller_profile/compile_profile.py") `
        $fixtureJson -o $fixtureTmp | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "controller profile fixture regeneration failed"
    }
    try {
        $expected = [System.IO.File]::ReadAllBytes($fixtureBin)
        $actual = [System.IO.File]::ReadAllBytes($fixtureTmp)
        if ($expected.Length -ne $actual.Length -or
            -not [System.Linq.Enumerable]::SequenceEqual($expected, $actual)) {
            throw "controllers/pioneer_ddj_flx4/profile.s3bin is stale; rerun tools/controller_profile/compile_profile.py"
        }
    } finally {
        Remove-Item -LiteralPath $fixtureTmp -Force -ErrorAction SilentlyContinue
    }
} else {
    Write-Warning "python not found; skipping controller profile fixture freshness check"
}

Assert-FileContains `
    -Name "s3 descriptor report accompanies connection state" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/flx4_midi_host/flx4_midi_host.c") `
    -Patterns @("desc_report_send_if_valid", "CTRL_DESC_CAP_MIDI_IN", "control_link_send_descriptor_report")

Assert-FileContains `
    -Name "s3 receives profile transfer frames and stores the blob" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("handle_profile_frame", "cp_xfer_rx_begin", "cp_xfer_rx_chunk", "cp_xfer_rx_end", "send_profile_reply_ack", "control_link_get_stored_profile")

Assert-FileContains `
    -Name "s3 routes MIDI through the dynamic profile with FLX4 fallback" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/main/app_main.c") `
    -Patterns @("controller_profile_runtime_active", "controller_profile_runtime_map", "flx4_map_message", "control_link_set_profile_activate_cb")

Assert-FileContains `
    -Name "s3 LED output prefers the dynamic profile with FLX4 fallback" `
    -Path (Join-Path $RepoRoot "firmware/control-board-s3/components/control_link/control_link_uart.c") `
    -Patterns @("controller_profile_runtime_map_led", "flx4_led_midi_build_packet")

# The 0xA6 bulk codec and the profile-transfer receiver are kept byte-for-byte
# identical on the S3 and P4 sides so the link cannot disagree on the wire.
function Assert-FilesIdentical {
    param([string]$Name, [string]$A, [string]$B)
    Write-Host "==> static $Name"
    $a = [System.IO.File]::ReadAllBytes((Join-Path $RepoRoot $A))
    $b = [System.IO.File]::ReadAllBytes((Join-Path $RepoRoot $B))
    if ($a.Length -ne $b.Length -or -not [System.Linq.Enumerable]::SequenceEqual($a, $b)) {
        throw ("{0}: {1} and {2} differ (must be byte-identical)" -f $Name, $A, $B)
    }
}
Assert-FilesIdentical -Name "ctrl_bulk.c canonical on both sides" `
    -A "firmware/control-board-s3/components/control_link/ctrl_bulk.c" `
    -B "firmware/main-deck-p4/components/control_link/ctrl_bulk.c"
Assert-FilesIdentical -Name "cp_xfer.c canonical on both sides" `
    -A "firmware/control-board-s3/components/control_link/cp_xfer.c" `
    -B "firmware/main-deck-p4/components/control_link/cp_xfer.c"

# ctrl_bulk: exercise the canonical codec + transfer receiver, driving the real
# FLX4 fixture through build -> parse -> reassemble -> cp_profile_parse.
Write-Host "==> build ctrl_bulk"
$bulkDir = Join-Path $RepoRoot "tests/ctrl_bulk"
Push-Location $bulkDir
try {
    $inc = @(
        "-I../control_link_protocol/stubs",
        "-I../../firmware/main-deck-p4/components/control_link/include",
        "-I../../firmware/control-board-s3/components/controller_profile/include"
    )
    $gccArgs = @("-Wall", "-Wextra", "-Wpedantic", "-std=c99") + $inc + @(
        "-o", "test_ctrl_bulk.exe",
        "test_ctrl_bulk.c",
        "../../firmware/main-deck-p4/components/control_link/ctrl_bulk.c",
        "../../firmware/main-deck-p4/components/control_link/cp_xfer.c",
        "../../firmware/control-board-s3/components/controller_profile/controller_profile.c"
    )
    & $Gcc.Source @gccArgs
    if ($LASTEXITCODE -ne 0) { throw "ctrl_bulk compile failed" }
    Write-Host "==> run ctrl_bulk"
    & ".\test_ctrl_bulk.exe"
    if ($LASTEXITCODE -ne 0) { throw "ctrl_bulk tests failed" }
} finally {
    Remove-Item "test_ctrl_bulk.exe" -ErrorAction SilentlyContinue
    Pop-Location
}

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
        Name = "controller_profile"
        Dir = "tests/controller_profile"
        Target = "test_controller_profile.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-I../../firmware/control-board-s3/components/controller_profile/include",
            "-o", "test_controller_profile.exe",
            "test_controller_profile.c",
            "../../firmware/control-board-s3/components/controller_profile/controller_profile.c"
        )
    },
    @{
        Name = "controller_profile_parity"
        Dir = "tests/controller_profile"
        Target = "test_controller_profile_parity.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DFLX4_MIDI_HOST_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/controller_profile/include",
            "-I../../firmware/control-board-s3/components/flx4_midi_host/include",
            "-I../../firmware/control-board-s3/components/control_link/include",
            "-I../../firmware/control-board-s3/components/panel_io/include",
            "-o", "test_controller_profile_parity.exe",
            "test_controller_profile_parity.c",
            "../../firmware/control-board-s3/components/controller_profile/controller_profile.c",
            "../../firmware/control-board-s3/components/flx4_midi_host/flx4_map.c",
            "../../firmware/control-board-s3/components/control_link/flx4_led_midi.c"
        )
    },
    @{
        Name = "controller_profile_runtime"
        Dir = "tests/controller_profile_runtime"
        Target = "test_controller_profile_runtime.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-DCONTROLLER_PROFILE_RUNTIME_PC_TEST",
            "-I../../firmware/control-board-s3/components/controller_profile_runtime/include",
            "-I../../firmware/control-board-s3/components/controller_profile/include",
            "-o", "test_controller_profile_runtime.exe",
            "test_controller_profile_runtime.c",
            "../../firmware/control-board-s3/components/controller_profile_runtime/controller_profile_runtime.c",
            "../../firmware/control-board-s3/components/controller_profile/controller_profile.c"
        )
    },
    @{
        Name = "s3_debug_log_ring"
        Dir = "tests/s3_debug_ap"
        Target = "test_s3_debug_log_ring.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-DS3_DEBUG_AP_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/s3_debug_ap/include",
            "-o", "test_s3_debug_log_ring.exe",
            "test_s3_debug_log_ring.c",
            "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c"
        )
    },
    @{
        Name = "s3_debug_ap_state"
        Dir = "tests/s3_debug_ap"
        Target = "test_s3_debug_ap_state.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-DS3_DEBUG_AP_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/s3_debug_ap/include",
            "-o", "test_s3_debug_ap_state.exe",
            "test_s3_debug_ap_state.c",
            "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_ap.c",
            "../../firmware/control-board-s3/components/s3_debug_ap/s3_debug_log_ring.c"
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
    },
    @{
        Name = "flx4_usb_audio_runtime"
        Dir = "tests/flx4_usb_audio"
        Target = "test_flx4_usb_audio.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-DFLX4_USB_AUDIO_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/flx4_usb_audio/include",
            "-o", "test_flx4_usb_audio.exe",
            "test_flx4_usb_audio.c",
            "../../firmware/control-board-s3/components/flx4_usb_audio/flx4_usb_audio.c",
            "../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_descriptors.c",
            "../../firmware/control-board-s3/components/flx4_usb_audio/flx4_uac_packetizer.c"
        )
    },
    @{
        Name = "p4_audio_link"
        Dir = "tests/p4_audio_link"
        Target = "test_p4_audio_link.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/control-board-s3/components/p4_audio_link/include",
            "-o", "test_p4_audio_link.exe",
            "test_p4_audio_link.c",
            "../../firmware/control-board-s3/components/p4_audio_link/p4_audio_link.c"
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
