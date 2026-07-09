param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
$Gcc = Get-Command gcc -ErrorAction Stop

function Assert-FileDoesNotContain {
    param(
        [string]$Name,
        [string]$Description,
        [Parameter(Mandatory = $true)][string]$Path,
        [string[]]$LiteralPatterns,
        [Alias("Pattern")][string]$RegexPattern
    )

    if (-not $Name) {
        $Name = $Description
    }
    if (-not $Name) {
        $Name = "file guard"
    }
    if (-not $PSBoundParameters.ContainsKey("LiteralPatterns")) {
        $LiteralPatterns = @()
    }
    if (-not $PSBoundParameters.ContainsKey("RegexPattern")) {
        $RegexPattern = $null
    }

    Write-Host "==> static $Name"
    foreach ($pattern in $LiteralPatterns) {
        $matches = Select-String -LiteralPath $Path -Pattern $pattern -SimpleMatch
        if ($matches) {
            $first = $matches | Select-Object -First 1
            throw "$Name contains forbidden selector pattern '$pattern' at $($first.Path):$($first.LineNumber)"
        }
    }
    if ($RegexPattern) {
        $matches = Select-String -LiteralPath $Path -Pattern $RegexPattern
        if ($matches) {
            $first = $matches | Select-Object -First 1
            throw "$Name contains forbidden pattern '$RegexPattern' at $($first.Path):$($first.LineNumber)"
        }
    }
}

function Assert-FileContains {
    param(
        [string]$Name,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string[]]$LiteralPatterns
    )

    if (-not $Name) {
        $Name = "file guard"
    }

    Write-Host "==> static $Name"
    foreach ($pattern in $LiteralPatterns) {
        $matches = Select-String -LiteralPath $Path -Pattern $pattern -SimpleMatch
        if (-not $matches) {
            throw "$Name missing required pattern '$pattern' in $Path"
        }
    }
}

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

function Assert-OverviewInactiveGuardBeforeCacheUpdate {
    $Path = Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c"
    Write-Host "==> static overview inactive tab does not validate waveform cache"
    $text = Get-Content -LiteralPath $Path -Raw
    $start = $text.IndexOf("static void ui_render_overview_main_waveform")
    if ($start -lt 0) {
        throw "ui_render_overview_main_waveform not found"
    }
    $end = $text.IndexOf("#else", $start)
    if ($end -lt 0) {
        throw "ui_render_overview_main_waveform WIN32 split not found"
    }
    $body = $text.Substring($start, $end - $start)
    $guard = $body.IndexOf("s_overview_active_tab != 0")
    $update = $body.IndexOf("ui_overview_wave_cache_update")
    if ($update -lt 0) {
        throw "ui_overview_wave_cache_update not found in ui_render_overview_main_waveform"
    }
    if ($guard -lt 0 -or $guard -gt $update) {
        throw "ui_render_overview_main_waveform can validate waveform cache while overview tab is inactive"
    }
}

function Assert-OverviewMainRenderCommitGuard {
    $Path = Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c"
    Write-Host "==> static overview main waveform commits only after successful render"
    $text = Get-Content -LiteralPath $Path -Raw
    $start = $text.IndexOf("static void ui_render_overview_main_waveform")
    if ($start -lt 0) {
        throw "ui_render_overview_main_waveform not found"
    }
    $end = $text.IndexOf("/* Render the overview waveform", $start)
    if ($end -lt 0) {
        throw "ui_render_overview_main_waveform end marker not found"
    }
    $body = $text.Substring($start, $end - $start)
    $commit = $body.IndexOf("panel->last_wave_center_ms = center_ms")
    if ($commit -lt 0) {
        throw "ui_render_overview_main_waveform does not commit last_wave_center_ms"
    }
    $guard = $body.LastIndexOf("if (!main_wave_rendered)", $commit)
    if ($guard -lt 0) {
        throw "ui_render_overview_main_waveform can mark a waveform rendered after a skipped/failed blit"
    }
}

function Assert-OverviewLoadDefersMainWaveRender {
    $Path = Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c"
    Write-Host "==> static overview load defers main waveform render to scheduler"
    $text = Get-Content -LiteralPath $Path -Raw
    $start = $text.IndexOf("void ui_overview_load_waveform_data")
    if ($start -lt 0) {
        throw "ui_overview_load_waveform_data not found"
    }
    $end = $text.IndexOf("void ui_overview_update_cue_markers", $start)
    if ($end -lt 0) {
        throw "ui_overview_load_waveform_data end marker not found"
    }
    $body = $text.Substring($start, $end - $start)
    if ($body.Contains("ui_render_overview_main_waveform")) {
        throw "ui_overview_load_waveform_data direct-renders the main waveform during track load"
    }
    if (-not $body.Contains("ui_overview_arm_all_wave_reblits")) {
        throw "ui_overview_load_waveform_data must re-arm all deck waveform overlays after any track load"
    }
}

Assert-FileDoesNotContain `
    -Name "audio_engine explicit deck state" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("s_active_eng", "#define s_eng", "select_engine", "restore_engine")

Assert-FileContains `
    -Name "s3 debug ap p4 sends state frames" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("void control_link_send_state", "CTRL_TYPE_STATE")

Assert-FileContains `
    -Name "s3 debug ap status reaches settings ui" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("deck_core_set_s3_debug_ap_status_cb", "CTRL_ID_S3_DEBUG_AP")

Assert-FileContains `
    -Name "s3 debug ap settings ui toggle wiring" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/main/app_main.c") `
    -LiteralPatterns @("ui_settings_set_s3_debug_ap_toggle_cb", "control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0)")

Assert-FileDoesNotContain `
    -Name "audio_engine per-deck firmware decode PCM buffers" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("static int16_t s_decode_pcm[MINIMP3_MAX_SAMPLES_PER_FRAME * 2];")

Assert-FileDoesNotContain `
    -Name "overview main RGB565 runtime path" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("if (!overlay_rendered)")

Assert-FileDoesNotContain `
    -Name "overview runtime avoids full RGB565 redraw" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("ui_overview_renderer_draw_main_rgb565(overlay")

Assert-OverviewInactiveGuardBeforeCacheUpdate
Assert-OverviewMainRenderCommitGuard
Assert-OverviewLoadDefersMainWaveRender

Assert-FileDoesNotContain `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c") `
    -Pattern "memmove\s*\(" `
    -Description "overview waveform cache must not use CPU memmove for steady scroll"

Assert-FileDoesNotContain `
    -Name "overview mini waveform avoids full canvas invalidation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("lv_obj_invalidate(panel->mini_wave_canvas);")

Assert-FileDoesNotContain `
    -Name "overview title avoids continuous LVGL marquee invalidation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("LV_LABEL_LONG_SCROLL_CIRCULAR", "LV_LABEL_LONG_DOT")

Assert-FileDoesNotContain `
    -Name "overview timer avoids frame-rate LVGL text invalidation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("remain_ms / 10u")

Assert-FileContains `
    -Name "P4 Overview shows elapsed + remaining time on the BPM row at BPM font size" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_TIME_X",
        "OVERVIEW_ELAPSED_W",
        "OVERVIEW_REMAIN_X",
        "OVERVIEW_REMAIN_W",
        "_Static_assert(OVERVIEW_REMAIN_X + OVERVIEW_REMAIN_W <= OVERVIEW_BPM_X",
        "panel->label_time_elapsed = ui_overview_value_label(panel->panel, &lv_font_montserrat_24,",
        "ui_overview_format_elapsed_time",
        "ui_overview_format_remaining_time"
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview blue title strip is title-only (no time pill)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_TITLE_TIME_X",
        "OVERVIEW_TITLE_TIME_W"
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview title strip avoids split timer labels" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "label_time_secs",
        "label_time_fraction",
        "OVERVIEW_TITLE_TIMER_MAIN_W",
        "OVERVIEW_TITLE_TIMER_FRACTION_X",
        "OVERVIEW_TITLE_TIMER_FRACTION_W"
    )

Assert-FileContains `
    -Name "P4 Overview beat strip geometry derives from waveform center" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_BEAT_STRIP_CENTER_GAP_PX",
        "OVERVIEW_BEAT_STRIP_STEP_PX",
        "OVERVIEW_WAVE_CENTER_X + ui_overview_beat_strip_offset_x(i)",
        "OVERVIEW_DECK2_WAVE_Y + OVERVIEW_CV_H"
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview beat strip avoids legacy hardcoded dot positions" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "pulse_x = 358",
        "pulse_x = 382",
        "pulse_x = 418",
        "pulse_x = 442",
        "pulse_y = (deck_idx == CTRL_DECK_1) ? 288 : 300"
    )

Assert-FileContains `
    -Name "P4 Overview beat strip uses interpolated deck position" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("ui_update_overview_beat_strip(deck, elapsed_ms)")

Assert-FileDoesNotContain `
    -Name "P4 Overview removes disabled legacy phase meter" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "ui_create_overview_phase_meter",
        "ui_update_phase_meter",
        "s_phase_meter_label",
        "OVERVIEW_PHASE_W",
        "OVERVIEW_PHASE_X",
        "OVERVIEW_PHASE_Y"
    )

Assert-FileContains `
    -Name "P4 Overview tempo cluster keeps pitch readable" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_PITCH_CHIP_W",
        "lv_font_montserrat_18",
        "lv_obj_set_style_bg_color(panel->label_pitch, COL_PANEL_DK",
        "lv_obj_set_style_border_color(panel->label_pitch, COL_GREEN"
    )

Assert-FileContains `
    -Name "P4 Overview tempo cluster displays decimal effective BPM" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "ui_overview_base_bpm_x100",
        "bpm_centi",
        "%u.%02u"
    )

Assert-FileContains `
    -Name "P4 Overview shows deck-local VU meters beside transport controls" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_TRANSPORT_X",
        "OVERVIEW_TRANSPORT_W",
        "OVERVIEW_VU_X",
        "_Static_assert(OVERVIEW_VU_X >= (OVERVIEW_TRANSPORT_X + OVERVIEW_TRANSPORT_W + 4)",
        "OVERVIEW_VU_H",
        "vu_segment",
        "ui_overview_update_vu_meter",
        "ctx->mixer_snapshot.deck_peak_display[deck]"
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview VU meters avoid transport button overlap" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "#define OVERVIEW_VU_X 67",
        "panel->play_button = ui_overview_compact_button(panel->panel, deck, 4, top_y + 60, 76",
        "ui_overview_compact_button(panel->panel, deck, 4, top_y + 102, 76, `"CUE`""
    )

Assert-FileContains `
    -Name "P4 Overview deck badges stay clear of VU meters" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_DECK_BADGE_W",
        "_Static_assert(OVERVIEW_DECK_BADGE_X + OVERVIEW_DECK_BADGE_W + 4 <= OVERVIEW_VU_X",
        'deck == CTRL_DECK_1 ? "D1" : "D2"'
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview deck badges avoid legacy wide labels" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        '"DECK 1"',
        '"DECK 2"',
        "lv_obj_set_size(panel->label_deck, 76, 38)"
    )

Assert-FileContains `
    -Name "P4 Overview deck badges match play/cue size and Library LOAD fill colours" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "_Static_assert(OVERVIEW_DECK_BADGE_W == OVERVIEW_TRANSPORT_W",
        "_Static_assert(OVERVIEW_DECK_BADGE_H == OVERVIEW_SIDE_BTN_H",
        "lv_color_t bg = (idx == CTRL_DECK_1) ? COL_ACCENT : COL_GREEN;",
        "lv_obj_set_style_text_color(panel->label_deck, COL_ON_ACCENT, LV_PART_MAIN);"
    )

Assert-FileContains `
    -Name "P4 Overview Beat FX uses compact right rail" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_FX_PANEL_H",
        "OVERVIEW_FX_DEPTH_BAR_W",
        "depth_fill",
        "status_bar"
    )

Assert-FileDoesNotContain `
    -Name "P4 Overview Beat FX avoids deck2 title overlap" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "lv_obj_set_size(s_overview_fx_panel, fx_w, 316)",
        "s_overview_fx.enabled_bar = ui_overview_bar(s_overview_fx_panel, row_x, 258, row_w, 40"
    )

Assert-FileDoesNotContain `
    -Name "deck_core UI-only buttons use semantic button mapping" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("on_button(DECK_CORE_COMPAT_DECK, (button_id_t)ev.id")

Assert-FileDoesNotContain `
    -Name "audio output pacing includes render time" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("write_elapsed_us")

Assert-FileDoesNotContain `
    -Name "P4 local UI excludes removed Key Shift screen" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui.c") `
    -LiteralPatterns @('"KEY SHIFT"', "ui_performance_tabs_create_key_shift")

Assert-FileDoesNotContain `
    -Name "P4 performance tabs exclude removed Key Shift controls" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_performance_tabs.c") `
    -LiteralPatterns @("KEY TRANSPOSE", "NO TRANSPOSITION", "ui_performance_tabs_create_key_shift")

Assert-FileDoesNotContain `
    -Name "P4 performance tabs API excludes removed Key Shift screen" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/include/ui_performance_tabs.h") `
    -LiteralPatterns @("ui_performance_tabs_create_key_shift", "toggle_master_tempo")

Assert-FileDoesNotContain `
    -Name "P4 Settings excludes retired monitor speaker switch" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_settings.c") `
    -LiteralPatterns @("MONITOR SPEAKER", "audio_out_event_cb", "monitor_route_label")

Assert-FileDoesNotContain `
    -Name "P4 Settings update avoids removed tab index" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_settings.c") `
    -LiteralPatterns @("active_tab != 6")

Assert-FileContains `
    -Name "P4 Settings wireless switches use dark off-state styling" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_settings.c") `
    -LiteralPatterns @("ui_settings_style_wireless_switch", "LV_PART_INDICATOR", "LV_PART_KNOB", "COL_PANEL_DK", "P4 REMOTE: ", "S3 DEBUG AP: ")

Assert-FileContains `
    -Name "P4 Settings mixer status strip keeps title clear of controls" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_settings.c") `
    -LiteralPatterns @(
        'ui_settings_section(screen, 30, 356, 740, 64, "MIXER STATUS")',
        "mixer_section, 18, 34, 110, 22",
        "lv_obj_set_size(btn_cue, 142, 22);",
        "lv_obj_set_pos(btn_cue, 570, 34);"
    )

Assert-FileContains `
    -Name "p4 bulk descriptor frames dispatch to a callback" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("ctrl_bulk_parser_feed", "CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR", "control_link_set_descriptor_report_cb")

Assert-FileContains `
    -Name "p4 app wires descriptor reports to the profile manager" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/main/app_main.c") `
    -LiteralPatterns @("control_link_set_descriptor_report_cb", "controller_profile_manager_on_descriptor_report")

Assert-FileContains `
    -Name "p4 manager streams the matched profile to the S3 off the RX task" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c") `
    -LiteralPatterns @("cpm_sender_task", "control_link_send_profile_begin", "control_link_send_profile_chunk", "cp_xfer_crc32", "CTRL_BULK_TYPE_PROFILE_ACTIVATE")

Assert-FileContains `
    -Name "p4 dispatches profile ACK/NACK replies to a callback" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("ctrl_bulk_decode_profile_ack", "ctrl_bulk_decode_profile_nack", "s_profile_reply_cb")

$tests = @(
    @{
        Name = "audio_diag"
        Dir = "tests/audio_diag"
        Target = "test_audio_diag.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_diag.exe",
            "test_audio_diag.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_diag.c"
        )
    },
    @{
        Name = "audio_mixer"
        Dir = "tests/audio_mixer"
        Target = "test_audio_mixer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_mixer.exe",
            "test_audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c"
        )
    },
    @{
        Name = "audio_format"
        Dir = "tests/audio_format"
        Target = "test_audio_format.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_format.exe",
            "test_audio_format.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_format.c"
        )
    },
    @{
        Name = "audio_wav_decoder"
        Dir = "tests/audio_wav_decoder"
        Target = "test_audio_wav_decoder.exe"
        Cleanup = @("test_stereo.wav", "test_mono.wav", "test_24bit.wav")
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DAUDIO_DECODER_PC_TEST", "-DMEDIA_IO_GATE_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-I../../firmware/main-deck-p4/components/media_io_gate/include",
            "-o", "test_audio_wav_decoder.exe",
            "test_audio_wav_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_format.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_wav_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_flac_decoder.c",
            "../../firmware/main-deck-p4/components/media_io_gate/media_io_gate.c"
        )
    },
    @{
        Name = "audio_engine"
        Dir = "tests/audio_engine"
        Target = "test_audio_engine.exe"
        Args = @(
            "-Wall", "-Wextra", "-std=c99",
            "-DAUDIO_ENGINE_PC_TEST", "-DAUDIO_DECODER_PC_TEST", "-DMEDIA_IO_GATE_STANDALONE_TEST", "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/audio_engine",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-I../../firmware/main-deck-p4/components/monitor_pcm_link/include",
            "-I../../firmware/main-deck-p4/components/media_io_gate/include",
            "-I../control_link_protocol/stubs",
            "-o", "test_audio_engine.exe",
            "test_audio_engine.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_engine.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_format.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_wav_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_flac_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_diag.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eq.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pcm_ring.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_preload.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_context.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c",
            "../../firmware/main-deck-p4/components/media_io_gate/media_io_gate.c",
            "../../firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c",
            "-lm"
        )
    },
    @{
        Name = "audio_eq"
        Dir = "tests/audio_eq"
        Target = "test_audio_eq.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_eq.exe",
            "test_audio_eq.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eq.c",
            "-lm"
        )
    },
    @{
        Name = "audio_filter"
        Dir = "tests/audio_filter"
        Target = "test_audio_filter.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_filter.exe",
            "test_audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "-lm"
        )
    },
    @{
        Name = "audio_smart_cfx"
        Dir = "tests/audio_smart_cfx"
        Target = "test_audio_smart_cfx.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_smart_cfx.exe",
            "test_audio_smart_cfx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c"
        )
    },
    @{
        Name = "audio_fw_task_plan"
        Dir = "tests/audio_fw_task_plan"
        Target = "test_audio_fw_task_plan.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_fw_task_plan.exe",
            "test_audio_fw_task_plan.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c"
        )
    },
    @{
        Name = "audio_fw_runtime"
        Dir = "tests/audio_fw_runtime"
        Target = "test_audio_fw_runtime.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_fw_runtime.exe",
            "test_audio_fw_runtime.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c"
        )
    },
    @{
        Name = "usb_media_partition"
        Dir = "tests/usb_media_partition"
        Target = "test_usb_media_partition.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/usb_storage/include",
            "-o", "test_usb_media_partition.exe",
            "test_usb_media_partition.c",
            "../../firmware/main-deck-p4/components/usb_storage/usb_media_partition.c"
        )
    },
    @{
        Name = "audio_fw_preload"
        Dir = "tests/audio_fw_preload"
        Target = "test_audio_fw_preload.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_fw_preload.exe",
            "test_audio_fw_preload.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_preload.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c"
        )
    },
    @{
        Name = "audio_fw_task_context"
        Dir = "tests/audio_fw_task_context"
        Target = "test_audio_fw_task_context.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_fw_task_context.exe",
            "test_audio_fw_task_context.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_context.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c"
        )
    },
    @{
        Name = "audio_delay_fx"
        Dir = "tests/audio_delay_fx"
        Target = "test_audio_delay_fx.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_delay_fx.exe",
            "test_audio_delay_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c"
        )
    },
    @{
        Name = "audio_pad_fx"
        Dir = "tests/audio_pad_fx"
        Target = "test_audio_pad_fx.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_pad_fx.exe",
            "test_audio_pad_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c",
            "-lm"
        )
    },
    @{
        Name = "audio_output_mixer"
        Dir = "tests/audio_output_mixer"
        Target = "test_audio_output_mixer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_output_mixer.exe",
            "test_audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eq.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c"
        )
    },
    @{
        Name = "audio_output_timing"
        Dir = "tests/audio_output_timing"
        Target = "test_audio_output_timing.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_output_timing.exe",
            "test_audio_output_timing.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_output_timing.c"
        )
    },
    @{
        Name = "monitor_pcm_link"
        Dir = "tests/monitor_pcm_link"
        Target = "test_monitor_pcm_link.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/main-deck-p4/components/monitor_pcm_link/include",
            "-o", "test_monitor_pcm_link.exe",
            "test_monitor_pcm_link.c",
            "../../firmware/main-deck-p4/components/monitor_pcm_link/monitor_pcm_link.c"
        )
    },
    @{
        Name = "beat_jump"
        Dir = "tests/beat_jump"
        Target = "test_beat_jump.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../deck_core_dual/stubs",
            "-I../../firmware/main-deck-p4/components/beat_jump/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_beat_jump.exe",
            "test_beat_jump.c",
            "../../firmware/main-deck-p4/components/beat_jump/beat_jump.c"
        )
    },
    @{
        Name = "deck_core_dual"
        Dir = "tests/deck_core_dual"
        Target = "test_deck_core_dual.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-Wno-unused-variable", "-Wno-unused-parameter",
            "-DDECK_CORE_PC_TEST",
            "-Istubs",
            "-I../../firmware/main-deck-p4/components/beat_jump/include",
            "-I../../firmware/main-deck-p4/components/deck_core/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-I../../firmware/main-deck-p4/components/hot_cue_store/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_deck_core_dual.exe",
            "test_deck_core_dual.c",
            "control_link_stub.c",
            "hot_cue_store_stub.c",
            "../../firmware/main-deck-p4/components/beat_jump/beat_jump.c",
            "../../firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c",
            "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
        )
    },
    @{
        Name = "flx4_led_snapshot"
        Dir = "tests/flx4_led_snapshot"
        Target = "test_flx4_led_snapshot.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-o", "test_flx4_led_snapshot.exe",
            "test_flx4_led_snapshot.c",
            "../../firmware/main-deck-p4/components/control_link/flx4_led_snapshot.c"
        )
    },
    @{
        Name = "ui_library"
        Dir = "tests/ui_library"
        Target = "test_ui_library.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DUI_LIBRARY_HOST_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-o", "test_ui_library.exe",
            "test_ui_library.c",
            "../../firmware/main-deck-p4/components/ui/ui_library.c"
        )
    },
    @{
        Name = "ui_settings"
        Dir = "tests/ui_settings"
        Target = "test_ui_settings.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DUI_SETTINGS_HOST_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-I../control_link_protocol/stubs",
            "-o", "test_ui_settings.exe",
            "test_ui_settings.c",
            "../../firmware/main-deck-p4/components/ui/ui_settings.c"
        )
    },
    @{
        Name = "ui_status"
        Dir = "tests/ui_status"
        Target = "test_ui_status.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DUI_STATUS_HOST_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-I../../firmware/main-deck-p4/components/deck_core/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-I../deck_core_dual/stubs",
            "-o", "test_ui_status.exe",
            "test_ui_status.c",
            "../../firmware/main-deck-p4/components/ui/ui_status.c"
        )
    },
    @{
        Name = "ui_beat_fx_format"
        Dir = "tests/ui_beat_fx_format"
        Target = "test_ui_beat_fx_format.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/deck_core/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-I../deck_core_dual/stubs",
            "-o", "test_ui_beat_fx_format.exe",
            "test_ui_beat_fx_format.c",
            "../../firmware/main-deck-p4/components/ui/ui_beat_fx_format.c"
        )
    },
    @{
        Name = "web_api_helpers"
        Dir = "tests/web_api_helpers"
        Target = "test_web_api_helpers.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/web_server/include",
            "-o", "test_web_api_helpers.exe",
            "test_web_api_helpers.c",
            "../../firmware/main-deck-p4/components/web_server/web_api_helpers.c"
        )
    },
    @{
        Name = "dns_reply"
        Dir = "tests/dns_reply"
        Target = "test_dns_reply.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/web_server/include",
            "-o", "test_dns_reply.exe",
            "test_dns_reply.c",
            "../../firmware/main-deck-p4/components/web_server/dns_reply.c"
        )
    },
    @{
        Name = "ui_overview_motion"
        Dir = "tests/ui_overview_motion"
        Target = "test_ui_overview_motion.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_ui_overview_motion.exe",
            "test_ui_overview_motion.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_motion.c"
        )
    },
    @{
        Name = "ui_overview_window"
        Dir = "tests/ui_overview_window"
        Target = "test_ui_overview_window.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-o", "test_ui_overview_window.exe",
            "test_ui_overview_window.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_window.c"
        )
    },
    @{
        Name = "ui_overview_scheduler"
        Dir = "tests/ui_overview_scheduler"
        Target = "test_ui_overview_scheduler.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-o", "test_ui_overview_scheduler.exe",
            "test_ui_overview_scheduler.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_scheduler.c"
        )
    },
    @{
        Name = "ui_overview_grid"
        Dir = "tests/ui_overview_grid"
        Target = "test_ui_overview_grid.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_ui_overview_grid.exe",
            "test_ui_overview_grid.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_grid.c"
        )
    },
    @{
        Name = "ui_overview_renderer"
        Dir = "tests/ui_overview_renderer"
        Target = "test_ui_overview_renderer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-std=c99",
            "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_ui_overview_renderer.exe",
            "test_ui_overview_renderer.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_renderer.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_grid.c",
            "../../firmware/main-deck-p4/components/ui/ui_waveform_model.c"
        )
    },
    @{
        Name = "ui_overview_wave_cache"
        Dir = "tests/ui_overview_wave_cache"
        Target = "test_ui_overview_wave_cache.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DANLZ_STANDALONE_TEST", "-DUI_OVERVIEW_WAVE_CACHE_TESTING",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_ui_overview_wave_cache.exe",
            "test_ui_overview_wave_cache.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_wave_cache.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_renderer.c",
            "../../firmware/main-deck-p4/components/ui/ui_waveform_model.c",
            "../../firmware/main-deck-p4/components/ui/ui_overview_grid.c"
        )
    },
    @{
        Name = "anlz"
        Dir = "tests/anlz"
        Target = "test_anlz.exe"
        Cleanup = @("test_synth.dat", "test_synth.ext")
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic",
            "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_anlz.exe",
            "test_anlz.c",
            "../../firmware/main-deck-p4/components/library/rekordbox_anlz.c"
        )
    },
    @{
        Name = "rekordbox_pdb"
        Dir = "tests/rekordbox_pdb"
        Target = "test_pdb.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic",
            "-DREKORDBOX_PDB_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_pdb.exe",
            "test_pdb.c",
            "../../firmware/main-deck-p4/components/library/rekordbox_pdb.c"
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
        Name = "controller_profile_manager"
        Dir = "tests/controller_profile_manager"
        Target = "test_controller_profile_manager.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DCONTROLLER_PROFILE_MANAGER_PC_TEST",
            "-I../control_link_protocol/stubs",
            "-I../../firmware/main-deck-p4/components/controller_profile_manager/include",
            "-o", "test_controller_profile_manager.exe",
            "test_controller_profile_manager.c",
            "../../firmware/main-deck-p4/components/controller_profile_manager/controller_profile_manager.c"
        )
    }
)

$created = New-Object System.Collections.Generic.List[string]

foreach ($test in $tests) {
    $dir = Join-Path $RepoRoot $test.Dir
    $target = Join-Path $dir $test.Target
    Invoke-Step -Name "build $($test.Name)" -WorkingDirectory $dir -Executable $Gcc.Source -Arguments $test.Args
    $created.Add($target)
    Invoke-Step -Name "run $($test.Name)" -WorkingDirectory $dir -Executable $target -Arguments @()
}

if (-not $KeepArtifacts) {
    foreach ($path in $created) {
        if (Test-Path -LiteralPath $path) {
            Remove-Item -LiteralPath $path -Force
        }
    }
    foreach ($test in $tests) {
        if (-not $test.ContainsKey("Cleanup")) {
            continue
        }
        $dir = Join-Path $RepoRoot $test.Dir
        foreach ($name in $test.Cleanup) {
            $path = Join-Path $dir $name
            if (Test-Path -LiteralPath $path) {
                Remove-Item -LiteralPath $path -Force
            }
        }
    }
}

Write-Host "P4 host tests passed."
