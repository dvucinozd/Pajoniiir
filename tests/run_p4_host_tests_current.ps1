param(
    [switch]$KeepArtifacts
)

$ErrorActionPreference = "Stop"
$source = Join-Path $PSScriptRoot "run_p4_host_tests.ps1"
$temp = Join-Path $PSScriptRoot ".run_p4_host_tests.current.tmp.ps1"

$oldQueue = @'
Assert-FileContains `
    -Name "p4 priority touch supersedes stale edges and survives button-only saturation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("event_is_jog_touch(&cur) && cur.id == ev->id", "queued older edge must never execute after the latest level", "button-only saturation", "xQueueSendToFront(s_event_queue, ev, portMAX_DELAY)")
'@

$newQueue = @'
Assert-FileContains `
    -Name "p4 control queue preserves edges and coalesces only continuous values" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("store_pending_continuous", "Button/state edges are lossless", "xQueueSend(s_event_queue, ev, portMAX_DELAY)")

Assert-FileDoesNotContain `
    -Name "p4 UART producer never drains or reorders the deck queue" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/control_link/control_link_uart.c") `
    -LiteralPatterns @("xQueueReceive(s_event_queue")
'@

$oldOta = @'
Assert-FileContains `
    -Name "p4 settings log the OTA passphrase's presence, never its value" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/app_settings/app_settings.c") `
    -LiteralPatterns @('s_ota_pass[0] ? "set" : "none"')
'@

$newOta = @'
Assert-FileContains `
    -Name "p4 settings log the transactional OTA passphrase snapshot's presence, never its value" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/app_settings/app_settings.c") `
    -LiteralPatterns @('next_pass[0] ? "set" : "none"')
'@

$extraAuditGates = @'

Assert-FileContains `
    -Name "p4 USB storage reconciles desired/current state and retries mount failures" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/usb_storage/usb_storage.c") `
    -LiteralPatterns @("storage_desired_t", "s_desired.epoch++", "ulTaskNotifyTake", "MOUNT_RETRY_MAX_MS", "retrying in %u ms", "Disconnect is level state")

Assert-FileDoesNotContain `
    -Name "p4 USB disconnect is not dependent on a finite event queue" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/usb_storage/usb_storage.c") `
    -LiteralPatterns @("xQueueSend(s_queue", "s_event_drop_count")

Assert-FileContains `
    -Name "p4 public ANLZ load preserves nonzero PDB audio duration" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/library_duration_fixed.c") `
    -LiteralPatterns @("catalog_duration_ms", "catalog_duration_ms != 0u", "track->duration_ms = catalog_duration_ms")

Assert-FileContains `
    -Name "p4 display shares one authoritative framebuffer count" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/bsp_jc4880/include/bsp_jc4880.h") `
    -LiteralPatterns @("BSP_LCD_FRAMEBUFFER_COUNT 1")

Assert-FileContains `
    -Name "p4 display allocates only the framebuffer the backend actually uses" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880_single_fb.c") `
    -LiteralPatterns @(".num_fbs = BSP_LCD_FRAMEBUFFER_COUNT", "single framebuffer")

Assert-FileContains `
    -Name "p4 LVGL backend requests the shared framebuffer count" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_lvgl_backend_single_fb.c") `
    -LiteralPatterns @("BSP_LCD_FRAMEBUFFER_COUNT", "esp_lcd_dpi_panel_get_frame_buffer")

Assert-FileContains `
    -Name "p4 firmware status strings are escaped before JSON formatting" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server_fixed.c") `
    -LiteralPatterns @("web_bridge_p4_ota_get_status", "web_bridge_control_link_get_s3_firmware_report", "web_firmware_json_escape_in_place")

Assert-FileContains `
    -Name "p4 OTA handlers consume fragmented request bodies completely" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/web_server/web_server.c") `
    -LiteralPatterns @("while (len < wanted)", "wanted - len", "while (manifest_received < sizeof(manifest_header))")

Assert-FileContains `
    -Name "p4 product defaults explicitly disable the recorder" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/sdkconfig.defaults") `
    -LiteralPatterns @("# CONFIG_AUDIO_RECORDER_ENABLED is not set")

Assert-FileContains `
    -Name "p4 recorder cannot be enabled without a dedicated safety remediation" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_recorder/CMakeLists.txt") `
    -LiteralPatterns @("if(CONFIG_AUDIO_RECORDER_ENABLED)", "Recorder is release-disabled pending STOP/finalize safety remediation")

Assert-FileDoesNotContain `
    -Name "confirmed dead scratch APIs stay removed" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_scratch_buffer.h") `
    -LiteralPatterns @("audio_scratch_buffer_push", "audio_scratch_buffer_index_for_ms", "audio_scratch_buffer_read(")

Assert-FileDoesNotContain `
    -Name "retired output timing APIs stay removed" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_output_timing.h") `
    -LiteralPatterns @("audio_output_block_period_ms", "audio_output_remaining_delay_ms")

Assert-FileDoesNotContain `
    -Name "test-only stereo mixer API stays out of production" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/include/audio_mixer.h") `
    -LiteralPatterns @("audio_mixer_mix_stereo")

Assert-FileContains `
    -Name "scratch transport test uses a local decode-writer bridge" `
    -Path (Join-Path $RepoRoot "tests/audio_scratch/test_audio_scratch_current.c") `
    -LiteralPatterns @("test_audio_scratch_writer_push", "#define audio_scratch_buffer_push test_audio_scratch_writer_push")

Assert-FileContains `
    -Name "production ANLZ walker rejects partial section envelopes" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/rekordbox_anlz_fixed.c") `
    -LiteralPatterns @("walk_sections_for_tag_legacy", "advance > file_len - pos", "pos == file_len ? TAG_WALK_ABSENT : TAG_WALK_MALFORMED")

Assert-FileContains `
    -Name "ANLZ host corpus covers DAT and EXT truncation boundaries" `
    -Path (Join-Path $RepoRoot "tests/anlz/test_anlz_current.c") `
    -LiteralPatterns @("dat_truncation_corpus_rejects_partial_structures", "ext_truncation_corpus_retains_previous_metadata", "test_truncated.dat", "test_truncated.ext")

Assert-FileContains `
    -Name "library duration test covers preservation and beatgrid fallback" `
    -Path (Join-Path $RepoRoot "tests/library_anlz/test_library_anlz_current.c") `
    -LiteralPatterns @("test_nonzero_pdb_duration_survives_anlz_enrichment", "test_zero_pdb_duration_falls_back_to_last_beat", "213456u", "7000u")

Assert-FileContains `
    -Name "production library exposes selected-track API names" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/include/library.h") `
    -LiteralPatterns @("library_set_selected_track_index", "library_selected_track_index")

Assert-FileDoesNotContain `
    -Name "public library header no longer exports mock selected-track aliases" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/include/library.h") `
    -LiteralPatterns @("mock_library_load_track_to_deck", "mock_library_get_current_track_index", "Temporary source-compatibility aliases")

Assert-FileContains `
    -Name "library wrapper emits production selected-track symbols from the legacy source" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/library_duration_fixed.c") `
    -LiteralPatterns @("#define mock_library_load_track_to_deck      library_set_selected_track_index", "#define mock_library_get_current_track_index library_selected_track_index", "no public/linkable compatibility aliases remain")

Assert-FileDoesNotContain `
    -Name "library wrapper no longer emits public mock compatibility functions" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/library/library_duration_fixed.c") `
    -LiteralPatterns @("void mock_library_load_track_to_deck", "int mock_library_get_current_track_index")

Assert-FileContains `
    -Name "firmware UI compiles through the production library API bridge" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/CMakeLists.txt") `
    -LiteralPatterns @("ui_library_selected_api.c")
'@

$text = Get-Content -LiteralPath $source -Raw
if (-not $text.Contains($oldQueue)) {
    throw "stale P4 queue assertion block was not found"
}
if (-not $text.Contains($oldOta)) {
    throw "stale OTA password-presence assertion block was not found"
}
$text = $text.Replace($oldQueue, $newQueue)
$text = $text.Replace($oldOta, $newOta)

$deckCoreSource = '"../../firmware/main-deck-p4/components/deck_core/deck_core.c"'
$deckCoreWrapper = '"deck_core_test_snapshot_wrapper.c"'
$deckCoreSourceCount = $text.Split($deckCoreSource).Count - 1
if ($deckCoreSourceCount -ne 2) {
    throw "expected two deck_core_dual source entries, found $deckCoreSourceCount"
}
$text = $text.Replace($deckCoreSource, $deckCoreWrapper)

$scratchTestSource = '"test_audio_scratch.c"'
if (($text.Split($scratchTestSource).Count - 1) -ne 1) {
    throw "expected one audio_scratch test source entry"
}
$text = $text.Replace($scratchTestSource, '"test_audio_scratch_current.c"')

$webTestSource = '"test_web_api_helpers.c"'
if (($text.Split($webTestSource).Count - 1) -ne 1) {
    throw "expected one web_api_helpers test source entry"
}
$text = $text.Replace($webTestSource, '"test_web_api_helpers_current.c"')

$webHelperSource = '"../../firmware/main-deck-p4/components/web_server/web_api_helpers.c"'
if (($text.Split($webHelperSource).Count - 1) -ne 1) {
    throw "expected one web_api_helpers implementation entry"
}
$webHelperSources = $webHelperSource + ",`n            " + '"../../firmware/main-deck-p4/components/web_server/web_firmware_json.c"'
$text = $text.Replace($webHelperSource, $webHelperSources)

$anlzTestSource = '"test_anlz.c"'
if (($text.Split($anlzTestSource).Count - 1) -ne 1) {
    throw "expected one ANLZ test source entry"
}
$text = $text.Replace($anlzTestSource, '"test_anlz_current.c"')

$anlzImplSource = '"../../firmware/main-deck-p4/components/library/rekordbox_anlz.c"'
if (($text.Split($anlzImplSource).Count - 1) -ne 1) {
    throw "expected one ANLZ implementation source entry"
}
$text = $text.Replace($anlzImplSource, '"../../firmware/main-deck-p4/components/library/rekordbox_anlz_fixed.c"')

$libraryTestSource = '"test_library_anlz.c"'
if (($text.Split($libraryTestSource).Count - 1) -ne 1) {
    throw "expected one library_anlz test source entry"
}
$text = $text.Replace($libraryTestSource, '"test_library_anlz_current.c"')

$libraryImplSource = '"../../firmware/main-deck-p4/components/library/library.c"'
if (($text.Split($libraryImplSource).Count - 1) -ne 1) {
    throw "expected one library implementation source entry"
}
$text = $text.Replace($libraryImplSource, '"../../firmware/main-deck-p4/components/library/library_duration_fixed.c"')

$text += $extraAuditGates

try {
    Set-Content -LiteralPath $temp -Value $text -Encoding utf8NoBOM
    & $temp -KeepArtifacts:$KeepArtifacts
    exit $LASTEXITCODE
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
