# P4 Overview DSI-Synchronised Waveform Smoke

Document status: focused hardware validation record, reviewed 2026-07-17.

- Date: 2026-07-17
- P4 serial port: COM15
- Final P4 image: `RC1-132-g2b0cfd59-dirty`, factory slot, wired flash
- S3 image: signed `RC1-131-gc391e306`, `ota_0 / valid`
- ESP-IDF: v5.5

## Scope

Diagnose rare full-screen flashes and non-fluid motion while both Overview main
waveforms are visible and playing. Accept the retained P4 fix only if waveform
motion is visually fluid, the operator sees no flash, the DSI driver reports no
external-memory underrun, monitor PCM does not drop, and the P4 does not panic,
reset, brown out or trip a watchdog.

This is a focused P4 display-path smoke. It is not signed-release acceptance
and does not close the broader Phase 20, controller-profile or Beat FX hardware
rows.

## Baseline and A/B diagnosis

The original full-cadence path updated both playing waveforms from a 16 ms UI
timer while the 34 MHz panel timing produced approximately 60.48 Hz. Each
visible flash in the baseline capture matched this driver error:

```text
E lcd.dsi.dpi: can't fetch data from external memory fast enough, underrun happens
```

No matching audio-late, monitor-PCM drop, panic, watchdog, brownout or reset was
observed. Temporary memory diagnostics confirmed that both 904 x 141 RGB565
waveform strips were PSRAM-backed (254,976 allocated bytes each). The evidence
therefore identifies a DSI/PPA external-memory bandwidth and scheduling-phase
collision, rather than audio decode or waveform-cache generation, as the cause
of the flashes.

| Variant | Serial result | Operator observation | Decision |
| --- | --- | --- | --- |
| Original approximately 60.48 Hz, both decks every 16 ms | Three DSI underruns in the first 60-second capture | Three flashes | Rejected |
| One waveform per 16 ms UI tick | Zero DSI underruns in the capture | No flash, but clearly watery motion | Rejected |
| One waveform per 8 ms UI tick | One DSI underrun | Water effect smaller, one flash | Rejected |
| Full cadence plus DW-GDMA AXI read QoS priority | Two DSI underruns during approximately 127 seconds | Two flashes | Rejected; override removed |
| Refresh-synchronised 49.981 Hz, full two-deck budget | Zero DSI underruns during approximately 132 seconds | Fluid, no watery motion, no flash | Accepted for focused smoke |

Diagnostic timing, memory-location logging and the QoS experiment were removed
from the retained firmware. The two-deck Overview scheduler budget remains at
two redraws when both decks are playing.

## Retained implementation

- Keep the 34 MHz DPI pixel clock and use vertical front porch `371`:

  ```text
  Htotal = 480 + 12 + 42 + 42 = 576
  Vtotal = 800 + 2 + 8 + 371 = 1181
  refresh = 34,000,000 / (576 * 1181) = 49.981 Hz
  ```

- Register the DPI `on_refresh_done` callback.
- Keep the ISR limited to an LVGL-task notification.
- Coalesce refreshes that arrive while the task is busy so stale UI updates do
  not queue.
- Run firmware `ui_update()` once for a delivered pending refresh from the
  existing LVGL task, before normal `lv_timer_handler()` work.
- Preserve independent bounded LVGL timeouts for touch, timers and animations.
- Retain the historical 16 ms timer only in the WIN32 simulator.

## Accepted result

Both decks played on Overview for approximately 132 seconds. COM15 showed zero
DSI underruns and no panic, watchdog, brownout or reset. The final live
monitor-link sample was:

```text
MONITOR_PCM_LINK tx submitted=22882 dropped=0 sent=22882
```

The operator first confirmed that the display was fluid and no longer had the
watery effect, then explicitly confirmed that no flash occurred during the
completed observation window.

## Software verification

- P4 `idf.py build`: PASS; application binary `0x209b30`, approximately 49% of
  the app partition free.
- P4 host-test wrapper: PASS; audio engine `363 PASS / 0 FAIL` and final
  `P4 host tests passed`.
- The wrapper could not find `python`, so its standalone OTA-signing Python
  subset was skipped. This display/BSP change does not touch OTA code, but a
  clean signed release build remains required before release deployment.
- S3 build was not run because no S3 or shared-protocol code changed.

The three raw diagnostic monitor files were kept outside the repository under
`C:\tmp\DDJ-FFL4-waveform-diagnostics-20260717`.

## Next acceptance boundary

Create a clean commit and signed P4 release candidate, deploy that exact image,
and repeat the focused dual-waveform COM15 smoke. Only then continue with the
remaining full release checklist.
