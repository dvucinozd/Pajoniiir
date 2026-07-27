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
    -Name "p4 display allocates only the framebuffer the backend actually uses" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/bsp_jc4880/bsp_jc4880_single_fb.c") `
    -LiteralPatterns @(".num_fbs = 1", "single framebuffer")

Assert-FileContains `
    -Name "p4 LVGL backend requests one DPI framebuffer" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/ui/ui_lvgl_backend_single_fb.c") `
    -LiteralPatterns @("esp_lcd_dpi_panel_get_frame_buffer(panel, 1", "s_dsi_active_fb_idx = 0")
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
$text += $extraAuditGates

try {
    Set-Content -LiteralPath $temp -Value $text -Encoding utf8NoBOM
    & $temp -KeepArtifacts:$KeepArtifacts
    exit $LASTEXITCODE
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
