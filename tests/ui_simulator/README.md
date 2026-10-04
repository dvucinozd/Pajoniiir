# Pajoniiir LVGL UI simulator E2E gate

This gate builds the real P4 LVGL UI against a pinned upstream LVGL commit,
runs it on a headless 800x480 framebuffer and drives navigation through the
actual LVGL button callbacks. No P4, S3, FLX4, SDL window or media device is
required.

Covered screenshots:

- Overview with Deck 1 selected;
- Overview after the scripted Deck 2 selection;
- Library with deterministic track fixtures;
- Hot Cues;
- Settings;
- idle screensaver;
- Settings restored after dismissing the screensaver.

Interaction checks also cover playlist navigation/export order, playing-deck
load lock and the active-deck VINYL/CDJ touch selector, including D1/D2
isolation and label refresh after a controller semantic mode event. PC
events publish the deck snapshot after dispatch, matching the firmware task.
Live-duration checks exercise the production UI getter with shorter/longer
files, unchanged analysis span, stale session rejection and unloaded fallback.

The reference is a SHA-256 manifest over the complete RGB framebuffer. A
one-pixel change therefore fails the gate and leaves the generated PPM captures
under `.cache/ui_simulator/screenshots` for review.

Run:

```powershell
.\tests\ui_simulator\run_ui_simulator_e2e.ps1
```

The first run downloads the pinned LVGL source into the ignored `.cache`
directory. To use an already available exact checkout:

```powershell
.\tests\ui_simulator\run_ui_simulator_e2e.ps1 `
    -LvglPath .\lv_port_pc_vscode\lvgl `
    -KeepArtifacts
```

After an intentional and visually reviewed UI change, regenerate the hash
manifest:

```powershell
.\tests\ui_simulator\run_ui_simulator_e2e.ps1 -UpdateBaselines -KeepArtifacts
```

Screenshot approval is a PC rendering regression gate. It does not replace the
P4 DSI/PPA fluidity, touch-coordinate, visibility-at-distance or panel-timing
hardware acceptance.

Package H runs seven presentations. `legacy`, `product-compact` and
`product-wide` compile the actual previous product presentation, at 800x480 or
1024x600, with thirteen captures including the bounded memory-cue list and its
scroll state. `runtime-compact`/`runtime-wide` compile the actual optional
preview plus the shared product Settings (eleven captures each). The standalone
`native-compact`/`native-wide` donor demos have nine captures each and do not
prove runtime integration.

```powershell
foreach ($mode in @('legacy','product-compact','product-wide',
                    'runtime-compact','runtime-wide','native-compact','native-wide')) {
    .\tests\ui_simulator\run_ui_simulator_e2e.ps1 -Presentation $mode -KeepArtifacts
}
```

Runtime assertions also cover one selected widget tree, paired D1/D2 PLAY/CUE
and pad holds, cancellation on navigation, tombstone precedence in waveform
rendering, deletion without triggering a cue and explicit long-held restore.
Baseline manifests are updated only after reviewing the generated captures.
