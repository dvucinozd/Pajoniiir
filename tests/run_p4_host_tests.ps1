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
    -Name "p4 priority touch supersedes stale edges and survives button-only saturation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("event_is_jog_touch(&cur) && cur.id == ev->id", "queued older edge must never execute after the latest level", "button-only saturation", "xQueueSendToFront(s_event_queue, ev, portMAX_DELAY)")

Assert-FileContains `
    -Name "s3 debug ap status reaches settings ui" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("deck_core_set_s3_debug_ap_status_cb", "CTRL_ID_S3_DEBUG_AP")

Assert-FileContains `
    -Name "s3 debug ap settings ui toggle wiring" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/main/app_main.c") `
    -LiteralPatterns @("ui_settings_set_s3_debug_ap_toggle_cb", "control_link_send_state(CTRL_ID_S3_DEBUG_AP, 0)")

Assert-FileContains `
    -Name "P4 OTA requires signed bundle before flash begin" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @(
        "ddj_ota_manifest_parse",
        "ddj_ota_manifest_verify_signature",
        "p4_ota_begin(&manifest)",
        "Invalid OTA manifest signature"
    )

Assert-FileContains `
    -Name "P4 OTA UI selects signed bundle" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web/index.html") `
    -LiteralPatterns @(".ddjota", "signed P4")

Assert-FileContains `
    -Name "P4 Wi-Fi Remote uses the accepted default WPA2 credential" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/wifi_link/include/wifi_link.h") `
    -LiteralPatterns @('WIFI_LINK_PASSWORD    "PajoNiiiR"')

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
        "lv_obj_set_style_border_color(panel->label_pitch, COL_GREEN",
        "pitch_centipct < 0 ? COL_RED : COL_GREEN"
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
    -Name "P4 Overview Beat FX uses compact right rail with effect-colour coding" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @(
        "OVERVIEW_FX_PANEL_H",
        "OVERVIEW_FX_DEPTH_BAR_H",
        "ui_overview_fx_effect_color",
        "effect_chip",
        "pill_bg",
        "depth_fill"
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
    -Name "P4 local UI excludes removed Loop and Beat Jump screens" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui.c") `
    -LiteralPatterns @('"BEAT JUMP"', "UI_TAB_LOOP", "UI_TAB_BEAT_JUMP", "ui_performance_tabs_create_beat_loop", "ui_performance_tabs_create_beat_jump")

Assert-FileDoesNotContain `
    -Name "P4 performance tabs exclude removed Loop and Beat Jump controls" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_performance_tabs.c") `
    -LiteralPatterns @("ui_performance_tabs_create_beat_loop", "ui_performance_tabs_create_beat_jump", "loop_btn_event_cb", "jump_btn_event_cb", "EXIT LOOP")

Assert-FileDoesNotContain `
    -Name "P4 performance tabs API excludes removed Loop and Beat Jump screens" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/include/ui_performance_tabs.h") `
    -LiteralPatterns @("ui_performance_tabs_create_beat_loop", "ui_performance_tabs_create_beat_jump", "ui_performance_tabs_update_loop_screen_state")

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
    -Name "p4 receives and displays S3 firmware reports" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("CTRL_BULK_TYPE_FIRMWARE_REPORT", "ctrl_bulk_decode_firmware_report", "control_link_get_s3_firmware_report")

Assert-FileContains `
    -Name "p4 OTA validates chip and project before activation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/p4_ota/p4_ota.c") `
    -LiteralPatterns @("P4_OTA_PROJECT_NAME", "wrong firmware project", "esp_ota_set_boot_partition")

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

Assert-FileContains `
    -Name "p4 audio_engine exposes a per-deck platter-hold mute (vinyl phase 1)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("s_deck_hold", "audio_engine_deck_set_hold", "if (atomic_load_bool(&s_deck_hold[deck])) return false;")

Assert-FileContains `
    -Name "p4 deck_core enters platter-hold on jog touch and scrubs while touched" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("case CTRL_DECK_CTL_JOG_TOUCH:", "handle_jog_touch", "audio_engine_deck_set_hold(deck, true)", "s_jog_touched[deck]")

Assert-FileContains `
    -Name "p4 audio_engine exposes canonical PCM timeline to scratch without a second PCM copy" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("audio_scratch_buffer.h", "init_scratch_buffers", "sync_scratch_view_from_timeline", "scratch begin D%u unavailable: canonical timeline not allocated -> platter hold")

Assert-FileDoesNotContain `
    -Name "p4 legacy independent scratch PCM store stays retired" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("s_scratch_storage", "audio_scratch_buffer_push(scratch", "audio_scratch_buffer_mark_newest_ms(scratch", "legacy fallback")

Assert-FileContains `
    -Name "p4 audio_scratch DSP engine renders bidirectional interpolated scratch (vinyl phase 3)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_scratch.c") `
    -LiteralPatterns @("audio_scratch_render", "audio_scratch_buffer_read_frame_back", "head_back -= s->velocity", "past_new_edge", "past_old_edge")

Assert-FileContains `
    -Name "p4 audio_engine routes a scratching deck to the scratch engine (vinyl phase 4)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("ae_scratch_render_cb", "audio_engine_deck_scratch_begin", "audio_engine_deck_scratch_move", "audio_engine_deck_scratch_end", ".scratch_active = atomic_load_bool(&s_scratch_playing")

Assert-FileContains `
    -Name "p4 audio_engine cross-fades the scratch->forward release handoff (vinyl phase 4b)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("AE_SCRATCH_HANDOFF_FADE_OUT", "AE_SCRATCH_HANDOFF_FADE_IN", "AE_SCRATCH_HANDOFF_RING", "AE_SCRATCH_XFADE_STEP")

Assert-FileContains `
    -Name "p4 deck_core gates jog touch to scratch behind CONFIG_AUDIO_SCRATCH_ENABLED (vinyl phase 4)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("#if CONFIG_AUDIO_SCRATCH_ENABLED", "audio_engine_deck_scratch_begin(deck)", "audio_engine_deck_scratch_move(deck, delta)", "audio_engine_deck_scratch_end(deck)")

Assert-FileContains `
    -Name "p4 mixer supports an optional scratch frame source (vinyl phase 4)" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c") `
    -LiteralPatterns @("deck->scratch_active && deck->scratch_render", "deck->scratch_render(deck->scratch_ctx")

Assert-FileContains `
    -Name "p4 ships with vinyl scratch enabled" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/sdkconfig.defaults") `
    -LiteralPatterns @("CONFIG_AUDIO_SCRATCH_ENABLED=y")

# The decoder runs ~2 s ahead of playback, so a loop wrap must withdraw whatever
# it already published past the out point. Without the trim the loop's first
# pass plays that lead — about four beats, off the grid at most tempos.
Assert-FileContains `
    -Name "p4 loop wrap withdraws decoded frames published past the loop out point" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("deck_pcm_drop_newest", "publish_frames", "uint64_t published = eng->frames_since_seek", "AE_LOOP_TRIM_MIN_RUNWAY_FRAMES")

# The one rule that must not rot: the service-network passphrase is never
# returned over the network, and never reaches a query string where it would
# be logged. Status reports only whether one is stored.
Assert-FileContains `
    -Name "p4 OTA config status reports only whether a password is stored" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @('app_settings_ota_has_password()', '\"has_password\":%s')

Assert-FileDoesNotContain `
    -Name "p4 web server never serialises the OTA passphrase" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("app_settings_ota_copy_password")

Assert-FileContains `
    -Name "p4 settings log the OTA passphrase's presence, never its value" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/app_settings/app_settings.c") `
    -LiteralPatterns @('s_ota_pass[0] ? "set" : "none"')

# An AP-to-STA switch must stop the AP service without tearing down the C6
# link; if these are ever folded back into one all-or-nothing teardown the
# transition silently becomes a full radio restart.
Assert-FileContains `
    -Name "p4 Wi-Fi teardown keeps the AP service and the C6 transport separable" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/wifi_link/wifi_link.c") `
    -LiteralPatterns @("static void stop_ap_services(void)", "static void stop_hosted_transport(void)", "static void stop_ap_netif(void)")

Assert-FileContains `
    -Name "p4 Wi-Fi start retries are bounded" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/wifi_link/wifi_link.c") `
    -LiteralPatterns @("wifi_link_retry_note_failure(&retry)", "giving up, radio stays off")

# Every exit from the STA visit must end back on the AP; the AP is the only way
# the deck is reachable at all, so a path that leaves it down is unrecoverable
# without a wired flash.
Assert-FileContains `
    -Name "p4 STA visit keeps the C6 transport and always has a way back" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/wifi_link/wifi_link.c") `
    -LiteralPatterns @("wifi_link_restore_ap", "STA_BIT_GOT_IP", "xEventGroupWaitBits")

Assert-FileDoesNotContain `
    -Name "p4 STA switch does not tear down ESP-Hosted" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/wifi_link/wifi_link.c") `
    -RegexPattern 'wifi_link_switch_to_sta[\s\S]*?stop_hosted_transport\(\)[\s\S]*?^\}'

# Pull OTA must gain no authority from having arrived over TLS: the same signed
# manifest, verified by the same code, before anything reaches flash.
Assert-FileContains `
    -Name "p4 pull OTA verifies the bundle signature before writing flash" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c") `
    -LiteralPatterns @("ddj_ota_manifest_verify_signature(header, sizeof(header))", "rc = p4_ota_begin(&manifest);")

Assert-FileContains `
    -Name "p4 pull OTA installs only a release a check offered and the caller names back" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/p4_ota_pull/p4_ota_pull.c") `
    -LiteralPatterns @("s_status.state != P4_OTA_PULL_AVAILABLE", "strncmp(expected_release, s_status.available_release")

# The recorder is off by default: its write latency is dominated by the microSD
# card rather than by the firmware, and chasing that cost a great deal of bench
# time for something off the critical path. These guards keep it that way, and
# keep the audio hot path from paying for a feature that is not built.
Assert-FileContains `
    -Name "p4 gates the recorder master tap behind CONFIG_AUDIO_RECORDER_ENABLED" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("#if !defined(AUDIO_ENGINE_PC_TEST) && CONFIG_AUDIO_RECORDER_ENABLED")

Assert-FileContains `
    -Name "p4 gates the recording endpoints behind CONFIG_AUDIO_RECORDER_ENABLED" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("#if CONFIG_AUDIO_RECORDER_ENABLED")

Assert-FileDoesNotContain `
    -Name "p4 ships with the microSD recorder disabled" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/sdkconfig.defaults") `
    -LiteralPatterns @("CONFIG_AUDIO_RECORDER_ENABLED=y")

Assert-FileContains `
    -Name "p4 audio_engine tears down scratch playback on reload/stop and when a deck stops mid-scratch" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("clear_scratch_playback_state", "atomic_load_bool(&s_scratch_playing[d]) && !deck_output_active(d)")

Assert-FileContains `
    -Name "p4 audio_engine permits loaded cue scratch and no-ops an unmatched release" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("if (!eng->loaded || eng->sample_rate == 0u || eng->loading)", "s_scratch_started_paused", "if (!atomic_load_bool(&s_scratch_playing[deck])) {")

Assert-FileContains `
    -Name "p4 audio_engine publishes the scratch handoff phase with release/acquire" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("scratch_handoff_store", "scratch_handoff_load", "__ATOMIC_ACQUIRE", "__ATOMIC_RELEASE")

Assert-FileContains `
    -Name "p4 scratch begin supports a fast re-grab during release handoff" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("scratch re-grab", "s_scratch_regrab_requested[deck], true", "scratch_handoff_store(&s_scratch_handoff[deck], AE_SCRATCH_HANDOFF_NONE)")

Assert-FileContains `
    -Name "p4 UI position follows the audible scratch head" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("scratch_head_snapshot(deck)", "audio_scratch_track_position_ms", "scratch_position_authoritative ? 0.0f")

Assert-FileContains `
    -Name "p4 waveform disables forward interpolation for scratch-authoritative position" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_overview.c") `
    -LiteralPatterns @("scratch_position_authoritative", "? 0u", "mixer_snapshot.scratch_position_authoritative")

Assert-FileContains `
    -Name "p4 paused seek pre-roll centers cue scratch history and future" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("timeline_preroll_pending", "decode_target_ms = target_ms - pre_ms", "cue pre-roll D%u ready", "audio_pcm_timeline_set_playhead")

Assert-FileContains `
    -Name "p4 scratch position and release wrap inside active loops" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("audio_scratch_track_position_ms", "eng->loop_active", "s_engines[deck].loop_start_ms", "s_engines[deck].loop_end_ms")

Assert-FileContains `
    -Name "p4 pitch changes are deferred until scratch fade-out reaches silence" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("s_pending_pitch_factor_bits", "s_pending_pitch_valid", "apply_pending_pitch(deck)", "atomic_load_bool(&s_scratch_playing[deck])")

Assert-FileContains `
    -Name "p4 pitch and jog state use atomic float bits across control and output tasks" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @(
        "pitch_factor_bits",
        "s_jog_bend_bits",
        "__atomic_compare_exchange_n(&s_jog_bend_bits[deck]",
        "engine_pitch_load(deck)"
    )

Assert-FileContains `
    -Name "p4 canonical PCM timeline drives decode, output and frame-accurate scratch release" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("AE_TIMELINE_FORWARD_MS", "deck_pcm_push", "pop_deck_source", "sync_scratch_view_from_timeline", "audio_pcm_timeline_set_playhead_frames_back")

Assert-FileContains `
    -Name "p4 decoder EOF drains pending PCM before natural transport completion" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @(
        "complete_eof_drain_if_ready",
        "audio_eof_policy_should_finish",
        "playback_finished",
        "Decoder EOF is not transport EOF"
    )

Assert-FileDoesNotContain `
    -Name "p4 transport flags use atomic access across decode output and control tasks" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -RegexPattern "eng->(playing|paused|eof|playback_finished)\s*="

Assert-FileDoesNotContain `
    -Name "p4 key-lock hot path avoids software-emulated double arithmetic" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_keylock.c") `
    -RegexPattern "\bdouble\s+[A-Za-z_][A-Za-z0-9_]*\s*(?:[=,;()]|\[)"

Assert-FileContains `
    -Name "p4 continuous audio output periodically gives IDLE0 a watchdog tick" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("audio_output_should_force_idle", "vTaskDelay(pdMS_TO_TICKS(1))", "IDLE0 one real tick")

Assert-FileContains `
    -Name "p4 scratch freeze promptly releases an in-flight canonical timeline writer" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("capture_interrupted", "scratch_writer_needs_cpu", "scratch freeze is waiting for its writer flag")

Assert-FileContains `
    -Name "p4 USB DWC BNA without CHHLTD uses the recoverable HCD pipe-error path" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/usb_storage/usb_dwc_hal_compat.c") `
    -LiteralPatterns @("USB_DWC_LL_INTR_CHAN_BNAINTR", "USB_DWC_HAL_CHAN_ERROR_BNA", "USB_DWC_HAL_CHAN_EVENT_ERROR", "if (!halted && !bna_without_halt)", "abort()")

Assert-FileContains `
    -Name "p4 links the narrow USB DWC interrupt decoder compatibility wrapper" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/usb_storage/CMakeLists.txt") `
    -LiteralPatterns @("usb_dwc_hal_compat.c", "--wrap=usb_dwc_hal_chan_decode_intr")

Assert-FileDoesNotContain `
    -Name "p4 PCM timeline random reads do not depend on a racy oldest physical index" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_pcm_timeline.h") `
    -LiteralPatterns @("oldest_index")

Assert-FileContains `
    -Name "p4 estimated seek rejects a missing file source" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("else if (eng->fp)", "Estimate seek rejected: no file source")

Assert-FileContains `
    -Name "p4 deck_core quantize binary-searches the beatgrid" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("uint16_t mid = (uint16_t)(lo + (hi - lo) / 2u);", "meta->beats[mid].time_ms < position_ms")

Assert-FileContains `
    -Name "p4 pdb row-slot iterator validates the whole group before subtraction" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/rekordbox_pdb.c") `
    -LiteralPatterns @(
        "if (ptr > p->data_len || ptr < pb || ptr - pb < group_bytes) break;",
        "if (slot < pb || slot + 2u > p->data_len) continue;",
        "ptr -= group_bytes;"
    )

Assert-FileContains `
    -Name "p4 web library stream aborts when the client disconnects" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("send_rc = httpd_resp_send_chunk(req, chunk, chunk_len);", "if (send_rc != ESP_OK) {")

Assert-FileContains `
    -Name "p4 web mutations require fixed-host marked POST requests and report queue pressure" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @(
        "api_request_allowed(req, true)",
        'strcmp(host, "192.168.4.1")',
        '"X-DDJ-Control"',
        "queue_rc = deck_core_queue_event(&ev);",
        '"503 Service Unavailable"',
        '.method = HTTP_POST'
    )

Assert-FileContains `
    -Name "p4 DSP applies control-task FX commands on output block boundaries" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @(
        "audio_output_apply_pending_fx_commands();",
        "publish_echo_command(deck, &config)",
        "publish_flanger_command(deck, &config)",
        "pack_pad_fx_command(config)"
    )

Assert-FileContains `
    -Name "p4 filter skips stable coefficient recomputation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_filter.c") `
    -LiteralPatterns @("!position_changed", "!filter->coefficients_dirty")

Assert-FileDoesNotContain `
    -Name "p4 filter does not recompute invariant logarithms" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_filter.c") `
    -LiteralPatterns @("logf(")

Assert-FileDoesNotContain `
    -Name "p4 web server does not expose wildcard CORS" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("Access-Control-Allow-Origin")

Assert-FileDoesNotContain `
    -Name "p4 web mutations do not use permissive atoi parsing" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("atoi(")

$tests = @(
    @{
        Name = "audio_eof_policy"
        Dir = "tests/audio_eof_policy"
        Target = "test_audio_eof_policy.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_eof_policy.exe",
            "test_audio_eof_policy.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eof_policy.c"
        )
    },
    @{
        Name = "audio_keylock"
        Dir = "tests/audio_keylock"
        Target = "test_audio_keylock.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_keylock.exe",
            "test_audio_keylock.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_keylock.c",
            "-lm"
        )
    },
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
        Name = "audio_recorder_wav"
        Dir = "tests/audio_recorder_wav"
        Target = "test_audio_recorder_wav.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_recorder/include",
            "-o", "test_audio_recorder_wav.exe",
            "test_audio_recorder_wav.c",
            "../../firmware/main-deck-p4/components/audio_recorder/audio_recorder_wav.c"
        )
    },
    @{
        Name = "audio_recorder_pipeline"
        Dir = "tests/audio_recorder_pipeline"
        Target = "test_audio_recorder_pipeline.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_recorder/include",
            "-o", "test_audio_recorder_pipeline.exe",
            "test_audio_recorder_pipeline.c",
            "../../firmware/main-deck-p4/components/audio_recorder/audio_recorder_ring.c",
            "../../firmware/main-deck-p4/components/audio_recorder/audio_recorder_writer.c"
        )
    },
    @{
        Name = "sd_io_gate"
        Dir = "tests/sd_io_gate"
        Target = "test_sd_io_gate.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-DSD_IO_GATE_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/sd_io_gate/include",
            "-o", "test_sd_io_gate.exe",
            "test_sd_io_gate.c",
            "../../firmware/main-deck-p4/components/sd_io_gate/sd_io_gate.c"
        )
    },
    @{
        Name = "service_log"
        Dir = "tests/service_log"
        Target = "test_service_log.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/service_log/include",
            "-o", "test_service_log.exe",
            "test_service_log.c",
            "../../firmware/main-deck-p4/components/service_log/service_log_format.c"
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
            "../../firmware/main-deck-p4/components/audio_engine/audio_eof_policy.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_format.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_wav_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_flac_decoder.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_diag.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_keylock.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eq.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_smart_cfx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pcm_ring.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pcm_timeline.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_scratch.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_delay_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_flanger_fx.c",
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
        Name = "audio_flanger_fx"
        Dir = "tests/audio_flanger_fx"
        Target = "test_audio_flanger_fx.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_flanger_fx.exe",
            "test_audio_flanger_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_flanger_fx.c",
            "-lm"
        )
    },
    @{
        Name = "audio_scratch_buffer"
        Dir = "tests/audio_scratch_buffer"
        Target = "test_audio_scratch_buffer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_scratch_buffer.exe",
            "test_audio_scratch_buffer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
            "-lm"
        )
    },
    @{
        Name = "audio_scratch"
        Dir = "tests/audio_scratch"
        Target = "test_audio_scratch.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_scratch.exe",
            "test_audio_scratch.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_scratch.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_scratch_buffer.c",
            "-lm"
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
            "../../firmware/main-deck-p4/components/audio_engine/audio_flanger_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pad_fx.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c",
            "-lm"
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
        Name = "audio_pcm_timeline"
        Dir = "tests/audio_pcm_timeline"
        Target = "test_audio_pcm_timeline.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_pcm_timeline.exe",
            "test_audio_pcm_timeline.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pcm_timeline.c"
        )
    },
    @{
        Name = "deck_core_dual_scratch"
        Dir = "tests/deck_core_dual"
        Target = "test_deck_core_dual_scratch.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-Wno-unused-variable", "-Wno-unused-parameter",
            "-DDECK_CORE_PC_TEST", "-DCONFIG_AUDIO_SCRATCH_ENABLED=1",
            "-Istubs",
            "-I../../firmware/main-deck-p4/components/beat_jump/include",
            "-I../../firmware/main-deck-p4/components/deck_core/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-I../../firmware/main-deck-p4/components/hot_cue_store/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_deck_core_dual_scratch.exe",
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
        Name = "p4_ota_policy"
        Dir = "tests/p4_ota"
        Target = "test_p4_ota_policy.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-I../../firmware/main-deck-p4/components/p4_ota/include",
            "-o", "test_p4_ota_policy.exe",
            "test_p4_ota_policy.c",
            "../../firmware/main-deck-p4/components/p4_ota/p4_ota_policy.c"
        )
    },
    @{
        Name = "ota_manifest"
        Dir = "tests/ota_manifest"
        Target = "test_ota_manifest.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror", "-std=c99",
            "-I../../firmware/common/ota_manifest/include",
            "-o", "test_ota_manifest.exe",
            "test_ota_manifest.c",
            "../../firmware/common/ota_manifest/ota_manifest.c"
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
            "-I../../firmware/main-deck-p4/components/audio_recorder/include",
            "-I../../firmware/main-deck-p4/components/service_log/include",
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
        Name = "wifi_link_retry"
        Dir = "tests/wifi_link_retry"
        Target = "test_wifi_link_retry.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/wifi_link/include",
            "-o", "test_wifi_link_retry.exe",
            "test_wifi_link_retry.c",
            "../../firmware/main-deck-p4/components/wifi_link/wifi_link_retry.c"
        )
    },
    @{
        Name = "p4_ota_pull_config"
        Dir = "tests/p4_ota_pull_config"
        Target = "test_p4_ota_pull_config.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/p4_ota_pull_core/include",
            "-o", "test_p4_ota_pull_config.exe",
            "test_p4_ota_pull_config.c",
            "../../firmware/main-deck-p4/components/p4_ota_pull_core/p4_ota_pull_config.c"
        )
    },
    @{
        Name = "p4_ota_pull_manifest"
        Dir = "tests/p4_ota_pull_manifest"
        Target = "test_p4_ota_pull_manifest.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/p4_ota_pull_core/include",
            "-o", "test_p4_ota_pull_manifest.exe",
            "test_p4_ota_pull_manifest.c",
            "../../firmware/main-deck-p4/components/p4_ota_pull_core/p4_ota_pull_manifest.c"
        )
    },
    @{
        Name = "ui_idle"
        Dir = "tests/ui_idle"
        Target = "test_ui_idle.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/ui/include",
            "-o", "test_ui_idle.exe",
            "test_ui_idle.c",
            "../../firmware/main-deck-p4/components/ui/ui_idle.c"
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
        Name = "library_anlz"
        Dir = "tests/library_anlz"
        Target = "test_library_anlz.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c11",
            "-Istubs",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-I../../firmware/main-deck-p4/components/media_io_gate/include",
            "-o", "test_library_anlz.exe",
            "test_library_anlz.c",
            "../../firmware/main-deck-p4/components/library/library.c"
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

# Prefer the ESP-IDF virtualenv interpreter: it is the one guaranteed to carry a
# working `cryptography`. A bare `python` from PATH may be an unrelated install
# whose cryptography bindings fail to load, which would fail this suite for
# reasons that have nothing to do with the code under test.
$pythonSource = $null
foreach ($candidate in @($env:IDF_PYTHON_ENV_PATH, "C:\Espressif\python_env\idf5.5_py3.11_env")) {
    if ($candidate) {
        $exe = Join-Path $candidate "Scripts\python.exe"
        if (Test-Path $exe) { $pythonSource = $exe; break }
    }
}
if (-not $pythonSource) {
    $python = Get-Command python -ErrorAction SilentlyContinue
    if ($python) { $pythonSource = $python.Source }
}
if ($pythonSource) {
    Invoke-Step -Name "run ota_signing" `
        -WorkingDirectory (Join-Path $RepoRoot "tests/ota_signing") `
        -Executable $pythonSource `
        -Arguments @("test_ota_signing.py")
} else {
    Write-Warning "python not found; skipping OTA signing Python tests"
}

$pwsh = Get-Command pwsh -ErrorAction Stop
# The call-graph audit greps the tree with ripgrep. Without `rg` it cannot run at
# all, and hard-failing there would also skip every step below it. Skip loudly
# instead, the same way a missing python skips the signing tests.
if (Get-Command rg -ErrorAction SilentlyContinue) {
    Invoke-Step -Name "run R5 dead-code call-graph audit" `
        -WorkingDirectory $RepoRoot `
        -Executable $pwsh.Source `
        -Arguments @("-NoProfile", "-File", "tests/r5_dead_code_audit.ps1")
} else {
    Write-Warning "ripgrep (rg) not found; SKIPPING the R5 dead-code call-graph audit"
}
Invoke-Step -Name "run OTA release helper tests" `
    -WorkingDirectory $RepoRoot `
    -Executable $pwsh.Source `
    -Arguments @("-NoProfile", "-File", "tests/ota_packaging/test_ota_release_helpers.ps1")

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
