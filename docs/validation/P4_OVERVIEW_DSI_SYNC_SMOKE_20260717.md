# P4 Overview DSI-Synchronised Waveform Smoke

Document status: focused hardware validation record, reviewed 2026-07-17.

- Date: 2026-07-17
- P4 serial port: COM15
- Retained source commit: `bd5e43cef448aab8701363bf2b23f8ddea74c0f8`
- Final P4 candidate: `RC1-133-gbd5e43ce`, factory slot, wired COM15 flash
- P4 payload: 2,137,344 bytes, SHA-256
  `b3dedb8c8bab9782962867d16c179e5e0cabe9d2f83f5816ce1e873551f71b8e`
- Signing: key ID `rel-001`, ECDSA-P256-SHA256
- S3 image left installed: signed `RC1-131-gc391e306`, `ota_0 / valid`
- ESP-IDF: v5.5

## Scope

Diagnose rare full-screen flashes and non-fluid motion while both Overview main
waveforms are visible and playing. Accept the retained P4 fix only if waveform
motion is visually fluid, the operator sees no flash, the DSI driver reports no
external-memory underrun, monitor PCM does not drop, and the P4 does not panic,
reset, brown out or trip a watchdog.

This is focused P4 display-path acceptance. The final pass uses the exact P4
payload copied into a cryptographically verified signed release bundle. The
user requested a wired full flash, so this run does not exercise the P4 HTTP
OTA upload or an OTA-slot transition. It also does not close the broader Phase
20, controller-profile or Beat FX hardware rows.

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
| Refresh-synchronised 49.981 Hz, full two-deck budget, development build `RC1-132-g2b0cfd59-dirty` | Zero DSI underruns during approximately 132 seconds | Fluid, no watery motion, no flash | Accepted as the A/B winner |
| Clean signed candidate payload `RC1-133-gbd5e43ce` | Zero DSI underruns during more than 71 seconds of active playback | Fluid, no jitter, no flash | Accepted for exact-image focused smoke |

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

## Accepted results

### Development A/B winner

Both decks played on Overview for approximately 132 seconds. COM15 showed zero
DSI underruns and no panic, watchdog, brownout or reset. The final live
monitor-link sample was:

```text
MONITOR_PCM_LINK tx submitted=22882 dropped=0 sent=22882
```

The operator first confirmed that the display was fluid and no longer had the
watery effect, then explicitly confirmed that no flash occurred during the
completed observation window.

### Exact signed-candidate re-smoke

Both targets were rebuilt from clean source commit `bd5e43ce` as
`RC1-133-gbd5e43ce`. The canonical release packager signed the P4 and S3
bundles and the outer manifest with key ID `rel-001`. Independent verification
accepted both bundles and the manifest. The P4 release `.bin` and the P4 build
`.bin` were byte-for-byte identical and had the SHA-256 value recorded above.

That exact P4 payload was then written to the factory slot over COM15. Esptool
verified every written region, and the next boot reported:

```text
fw_health: running main-deck-p4 RC1-133-gbd5e43ce slot=factory
```

The dual-deck monitor-PCM stream became active at approximately 17.7 seconds
and remained active through the final sample at approximately 88.8 seconds,
providing more than 71 seconds under playback load. The final sample was:

```text
MONITOR_PCM_LINK tx submitted=13392 dropped=0 sent=13391
```

One submitted buffer was in flight when capture stopped. No DSI
external-memory underrun, panic, watchdog, brownout or unexpected reset was
reported. The operator confirmed there was neither a visible flash nor any
waveform jitter.

## Software verification

- Development A/B P4 `idf.py build`: PASS; application binary `0x209b30`,
  approximately 49% of the app partition free.
- Development A/B P4 host-test wrapper: PASS; audio engine
  `363 PASS / 0 FAIL` and final `P4 host tests passed`.
- During that earlier wrapper run, `python` was not found and the standalone
  OTA-signing Python subset was skipped. It was rerun directly with the ESP-IDF
  Python environment after packaging: PASS, `6 tests`, including tampered
  manifest/image, wrong-key and truncated/extended-bundle rejection cases.
- Clean P4 candidate build: PASS; `main-deck-p4.bin` size `0x209d00`, with 49%
  of the smallest app partition free.
- Clean S3 packaging build: PASS; `control-board-s3.bin` size `0xe6500`, with
  52% of the smallest app partition free. S3 was rebuilt only because the
  canonical paired-release packager requires both targets; it was not flashed.
- Signed release packaging: PASS for both `.ddjota` bundles and the outer
  manifest. Independent public-key verification also passed for all three
  signatures.
- Exact P4 payload check: PASS; build and release `.bin` files were
  byte-for-byte identical.
- Boot-time control-link report for the unchanged installed S3:
  `RC1-131-gc391e306 slot=1 state=3` (`ota_0 / valid`).

The three raw diagnostic monitor files were kept outside the repository under
`C:\tmp\DDJ-FFL4-waveform-diagnostics-20260717`.

## Next acceptance boundary

The clean commit, signed paired release package, exact P4 payload deployment
and focused dual-waveform COM15 re-smoke are complete. Continue with the
remaining full release checklist: targeted Phase 20 recovery/mutation/UART
rows, controller-profile update acceptance, Flanger/Delay physical acceptance,
longer audio/key-lock and enclosure soaks. Exercise the P4 signed HTTP OTA
transport again only when an OTA-slot deployment is specifically required; it
was outside this wired-flash run.
