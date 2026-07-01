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

Assert-FileDoesNotContain `
    -Name "audio_engine explicit deck state" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("s_active_eng", "#define s_eng", "select_engine", "restore_engine")

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

Assert-FileDoesNotContain `
    -Name "deck_core UI-only buttons use semantic button mapping" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/deck_core/deck_core.c") `
    -LiteralPatterns @("on_button(DECK_CORE_COMPAT_DECK, (button_id_t)ev.id")

Assert-FileDoesNotContain `
    -Name "audio output pacing includes render time" `
    -Path (Join-Path $RepoRoot "firmware/main-deck-p4/components/audio_engine/audio_engine.c") `
    -LiteralPatterns @("write_elapsed_us")

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
        Name = "audio_engine"
        Dir = "tests/audio_engine"
        Target = "test_audio_engine.exe"
        Args = @(
            "-Wall", "-Wextra", "-std=c99",
            "-DAUDIO_ENGINE_PC_TEST", "-DANLZ_STANDALONE_TEST",
            "-I../../firmware/main-deck-p4/components/audio_engine",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-I../../firmware/main-deck-p4/components/library/include",
            "-o", "test_audio_engine.exe",
            "test_audio_engine.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_engine.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_diag.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_eq.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_filter.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_pcm_ring.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_preload.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_runtime.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_context.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_task_plan.c",
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
        Name = "audio_fw_preload"
        Dir = "tests/audio_fw_preload"
        Target = "test_audio_fw_preload.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_fw_preload.exe",
            "test_audio_fw_preload.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_fw_preload.c"
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
