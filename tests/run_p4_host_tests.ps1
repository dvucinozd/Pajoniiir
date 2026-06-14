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

$tests = @(
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
        Name = "audio_output_mixer"
        Dir = "tests/audio_output_mixer"
        Target = "test_audio_output_mixer.exe"
        Args = @(
            "-Wall", "-Wextra", "-Wpedantic", "-Werror=implicit-function-declaration", "-std=c99",
            "-I../../firmware/main-deck-p4/components/audio_engine/include",
            "-o", "test_audio_output_mixer.exe",
            "test_audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_output_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_mixer.c",
            "../../firmware/main-deck-p4/components/audio_engine/audio_resampler.c"
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
            "-I../../firmware/main-deck-p4/components/deck_core/include",
            "-I../../firmware/main-deck-p4/components/control_link/include",
            "-o", "test_deck_core_dual.exe",
            "test_deck_core_dual.c",
            "control_link_stub.c",
            "../../firmware/main-deck-p4/components/deck_core/deck_core.c"
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
