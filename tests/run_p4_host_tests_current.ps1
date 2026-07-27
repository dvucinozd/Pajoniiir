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

$text = Get-Content -LiteralPath $source -Raw
if (-not $text.Contains($oldQueue)) {
    throw "stale P4 queue assertion block was not found"
}
if (-not $text.Contains($oldOta)) {
    throw "stale OTA password-presence assertion block was not found"
}
$text = $text.Replace($oldQueue, $newQueue)
$text = $text.Replace($oldOta, $newOta)

try {
    Set-Content -LiteralPath $temp -Value $text -Encoding utf8NoBOM
    & $temp -KeepArtifacts:$KeepArtifacts
    exit $LASTEXITCODE
} finally {
    Remove-Item -LiteralPath $temp -Force -ErrorAction SilentlyContinue
}
