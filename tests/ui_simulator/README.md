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
