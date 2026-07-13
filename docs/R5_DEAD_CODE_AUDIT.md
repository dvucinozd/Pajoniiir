# R5 Dead-Code And Legacy-Path Audit

Status: R5A-R5F complete. Final host/build gates, matching-version wired flashes
and the dual-target scratch soak were accepted on 2026-07-14. This document is
the evidence and decision log for cleanup batches R5A-R5F.

## Confirmed Call Graph

| Candidate | Production evidence | R5 disposition |
|---|---|---|
| `AE_SDL` | Defined once and never read | Removed in R5B |
| single-deck play/pause/stop/seek/set-loop API | Definitions only; tests used part of the facade | Tests migrated and facade removed in R5B |
| `audio_engine_clear_loop()` | Was called by `ui_library.c` for track load | Migrated to `audio_engine_deck_clear_loop(req.deck)` in R5B |
| `audio_output_mixer_next()` | No firmware caller; tests exercised it | Removed in R5C |
| `audio_output_mixer_next_full()` | Wrapper used only by tests | Tests migrated to `_with_headphone_level()` and wrapper removed in R5C |
| `s_scratch_storage` | Was a second PSRAM allocation and decode-copy path when canonical timeline allocation failed | Removed in R5E; normal ring playback remains and scratch declines into platter-hold |
| S3 `router_task` / `panel_io` / `midi_compat` / `calibration` | Compiled legacy CDJ panel/TinyUSB device configuration still built at R5A | Explicitly retired and removed in R5D |
| `anlz_walk_usbanlz()` / `TODO Phase 6` | No caller; obsolete because `export.pdb` supplies each track's direct `anlz_path` | API, PC walker and firmware stub removed in R5F |

R5D proved that `control_link_init()` used its queue argument only as a legacy
LED-fallback boolean and that `control_link_send_event()` merely translated four
panel event variants into the existing semantic sender. Both couplings were
removed before deleting the legacy components.

## R5A Size Baseline

All values come from ESP-IDF v5.5 `idf.py size` after commit `5782c58a` plus
the audit-only legacy defaults file.

| Target/profile | Application binary | Total image | Internal RAM summary | Slot headroom |
|---|---:|---:|---:|---:|
| P4 `build_signed` | `0x205B70` | 2,120,177 B | DIRAM 263,189 B (45.66%) | 49% of 4 MiB app slot free |
| S3 `build_signed` | `0xE61E0` | 942,437 B | DIRAM 156,139 B (45.69%), IRAM 16,384 B (100%) | 52% of 1.875 MiB app slot free |
| S3 `build_legacy_audit` | `0xE6070` | 942,081 B | DIRAM 156,139 B (45.69%), IRAM 16,384 B (100%) | 10% of legacy 1 MiB app slot free |

The legacy image being only 356 bytes smaller than the shipping image did not
mean its panel path was free. R5D removed the entire alternative configuration
and its direct TinyUSB dependency; the signed product image remains well inside
its OTA slot.

## Retired Legacy Build Baseline

R5A used the following audit-only profile to prove the mode was real before the
product decision. R5D deleted both that profile and the components, so this
command is intentionally no longer reproducible:

```powershell
cd firmware\control-board-s3
idf.py -B build_legacy_audit `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.legacy.defaults" build
```

The recorded result remains evidence for why explicit approval was required.

## Batch Gates

- R5B may remove only facade symbols with no remaining firmware/test callers.
- R5C must preserve master, PFL, headphone, FX and limiter assertions through
  the canonical full mixer entry point.
- R5D pauses for explicit approval before deleting the working legacy S3 mode.
- R5E must keep playback alive and select platter-hold fallback when canonical
  scratch history is unavailable.
- R5F compares final size output against this baseline and records hardware
  acceptance.

## R5E Result

The independent `s_scratch_storage` PSRAM allocation, per-frame decode copy,
newest-position metadata update and decoder-seek scratch-release branches were
removed. `audio_scratch_buffer_t` is now only a read-head/metadata view over the
canonical timeline. If that timeline cannot be allocated, the deck continues
to load and play through `s_pcm_rings[]`; scratch begin returns false and the
existing deck-core policy freezes the live playhead in platter-hold until touch
release.

Acceptance coverage includes an audio-engine allocation-failure simulation that
proves ring load/play remains available and scratch is rejected, plus a
scratch-enabled deck-core test that proves begin failure holds, scrubs and
releases without arming scratch handoff.

Acceptance on 2026-07-13:

- complete P4 host suite passes, including 330/330 `audio_engine` assertions,
  the scratch-enabled deck-core suite and the zero-symbol R5 audit;
- ESP-IDF P4 signed build passes; `main-deck-p4.bin` is `0x2056E0` bytes with
  49% free in the smallest app partition;
- relative to R5D, removal of the duplicate fallback path saves `0x410` (1,040)
  application bytes; no extra PSRAM allocation is attempted after a canonical
  timeline allocation failure.

## R5F Software Result

The final call-graph audit resolved the stale `TODO Phase 6` in
`rekordbox_anlz.c`. `anlz_walk_usbanlz()` had no caller and was obsolete because
the production library already reads each track's direct `anlz_path` from
`export.pdb`. Its public declaration, standalone PC directory walker, firmware
stub and `dirent` helpers were removed. The R5 audit now rejects that symbol in
addition to every compatibility symbol and legacy S3 path retired in R5B-R5E.

Final software acceptance on 2026-07-14:

- complete P4 and S3 host suites pass;
- P4 includes 330/330 `audio_engine` assertions and both canonical-allocation
  failure/platter-hold tests;
- both clean signed-layout ESP-IDF builds pass; the hardware-accepted rebuild is
  `RC1-121-gb7ac66a5` on both processors;
- P4: application `0x2056E0`, total image 2,119,001 B, DIRAM 263,177 B
  (45.65%), 49% app-slot free;
- S3: application `0xE60E0`, total image 942,185 B, DIRAM 155,651 B (45.54%),
  IRAM 16,384 B (100%), 52% app-slot free.

Relative to the R5A signed baseline, the final P4 application is 1,168 B
smaller and uses 12 B less DIRAM; the final S3 application is 256 B smaller and
uses 488 B less DIRAM. The P4 clean image was wired-flashed on COM15 with hash
verification and remained reset/panic/watchdog-free during 55 seconds of runtime
monitoring. Both clean images were then wired-flashed with hash verification.
During the final 45-second simultaneous P4/S3 capture, the user loaded tracks
and scratch-stressed both platters with fast forward/reverse and release/re-grab
gestures. No reset, panic, stack overflow, watchdog, PCM underrun/overrun, link
gap or CRC error occurred. The S3 PCM link reached 6,914 received blocks and the
FLX4 USB audio path completed 35,000 transfers without an underrun.

## R5B Result

The public `audio_engine` contract is now deck-authoritative. The compatibility
deck constant, single-deck transport/state/error/loop wrappers and unused
`AE_SDL` state were removed. Library load clears the loop on the requested deck,
and Overview loading state is read from the active deck rather than implicitly
from Deck 1.

Acceptance on 2026-07-13:

- complete P4 host suite passes, including 319/319 `audio_engine` assertions;
- the static audit confirms that all removed facade symbols have zero firmware
  declarations, definitions or callers;
- ESP-IDF P4 signed build passes; `main-deck-p4.bin` is `0x205AF0` bytes with
  49% free in the smallest app partition.

## R5C Result

`audio_output_mixer_next_full_with_headphone_level()` is now the only public
mixer render entry point. The master-only and implicit-full-headphone-level
wrappers were removed, while host tests preserve their previous expectations by
calling the canonical path with an explicit maximum headphone level.

Acceptance on 2026-07-13:

- complete P4 host suite passes, including the master, PFL, headphone mode,
  headphone level, FX, source-consumption and limiter assertions;
- the static audit confirms that both removed mixer wrappers have zero firmware
  declarations, definitions or callers;
- ESP-IDF P4 signed build passes; `main-deck-p4.bin` remains `0x205AF0` bytes
  with 49% free in the smallest app partition.

## R5D Result

The user explicitly approved permanent retirement of the S3 legacy mode on
2026-07-13. `panel_io`, `midi_compat`, `calibration`, `router_task`, the
`CONFIG_DDJ_FLX4_HOST_MODE` branch, its audit defaults and the direct
`esp_tinyusb` manifest dependency were removed. The raw logger remains available
by disabling only `CONFIG_DDJ_FLX4_TRANSLATE_TO_P4`; USB OTG always stays in
host role.

Active wire LED IDs 0-4 were moved from the deleted panel header into the S3
`control_link` header and are parity-tested against P4. The S3 control-link API
no longer exposes `panel_event_t` or depends on `panel_io`.

Acceptance on 2026-07-13:

- complete S3 host suite passes, including FLX4 input/LED/profile parity and
  shared control-link protocol tests;
- complete P4 host suite passes, including 319/319 `audio_engine` assertions;
- clean ESP-IDF S3 signed build passes without `esp_tinyusb`; binary is
  `0xE60E0`, total image is 942,185 B, DIRAM is 155,651 B (45.54%), and 52% of
  the OTA app slot remains free;
- ESP-IDF P4 signed build passes; binary remains `0x205AF0` with 49% free;
- relative to R5A, S3 saves 256 binary bytes, 252 total-image bytes and 488
  DIRAM bytes. The main value is removal of unsupported product surface rather
  than size reduction.
