Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")

function Assert-FileContains {
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Pattern
    )

    $fullPath = Join-Path $repoRoot $Path
    if (-not (Test-Path -LiteralPath $fullPath)) {
        throw "Missing required file: $Path"
    }

    $content = Get-Content -Raw -LiteralPath $fullPath
    if ($content -notmatch $Pattern) {
        throw "Expected pattern not found in ${Path}: $Pattern"
    }
}

Assert-FileContains "firmware/main-deck-p4/components/ui/ui.c" '#include "splash_screen\.h"'
Assert-FileContains "firmware/main-deck-p4/components/ui/ui.c" 'static lv_obj_t \*s_main_screen = NULL;'
Assert-FileContains "firmware/main-deck-p4/components/ui/ui.c" 'lv_screen_load\(s_main_screen\);'
Assert-FileContains "firmware/main-deck-p4/components/ui/ui.c" 'splash_screen_show\(ui_splash_screen_finished_cb\);'
Assert-FileContains "firmware/main-deck-p4/components/ui/CMakeLists.txt" '"splash_screen\.c"'
Assert-FileContains "firmware/main-deck-p4/components/ui/CMakeLists.txt" '"Musieer_80\.c"'
Assert-FileContains "firmware/main-deck-p4/components/ui/include/splash_screen.h" 'void splash_screen_show\(void \(\*loaded_cb\)\(void\)\);'
Assert-FileContains "firmware/main-deck-p4/components/ui/splash_screen.c" 'static const char \*splash_text = "Pajoniiir";'
Assert-FileContains "firmware/main-deck-p4/components/control_link/control_link_uart.c" 'xTaskCreate\(uart_rx_task, "ctrl_rx", 4096, NULL, 5, NULL\)'

Write-Host "splash port tests passed"
