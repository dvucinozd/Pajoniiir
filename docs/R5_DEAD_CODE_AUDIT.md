# R5 Dead-Code And Legacy-Path Audit

Status: R5A baseline, R5B P4 facade removal and R5C mixer consolidation
completed 2026-07-13. This document is the evidence and decision log for
cleanup batches R5A-R5F; it is not authorization to remove a working
compatibility path without its batch acceptance gate.

## Confirmed Call Graph

| Candidate | Production evidence | R5 disposition |
|---|---|---|
| `AE_SDL` | Defined once and never read | Removed in R5B |
| single-deck play/pause/stop/seek/set-loop API | Definitions only; tests used part of the facade | Tests migrated and facade removed in R5B |
| `audio_engine_clear_loop()` | Was called by `ui_library.c` for track load | Migrated to `audio_engine_deck_clear_loop(req.deck)` in R5B |
| `audio_output_mixer_next()` | No firmware caller; tests exercised it | Removed in R5C |
| `audio_output_mixer_next_full()` | Wrapper used only by tests | Tests migrated to `_with_headphone_level()` and wrapper removed in R5C |
| `s_scratch_storage` | Real PSRAM allocation fallback when canonical timeline allocation fails | Replace with explicit safe degraded-mode in R5E; not dead today |
| S3 `router_task` / `panel_io` / `midi_compat` / `calibration` | Compiled legacy CDJ panel/TinyUSB device configuration still builds | R5D requires an explicit product-support decision |
| `TODO Phase 6` in `rekordbox_anlz.c` | Stale comment requires context audit | Resolve wording in R5F |

`control_link` on S3 still includes `panel_io.h`, exports
`control_link_send_event(const panel_event_t *)`, and declares `panel_io` as a
component dependency. The shipping FLX4 path therefore retains legacy coupling
even though it passes `NULL` to `control_link_init()`.

## R5A Size Baseline

All values come from ESP-IDF v5.5 `idf.py size` after commit `5782c58a` plus
the audit-only legacy defaults file.

| Target/profile | Application binary | Total image | Internal RAM summary | Slot headroom |
|---|---:|---:|---:|---:|
| P4 `build_signed` | `0x205B70` | 2,120,177 B | DIRAM 263,189 B (45.66%) | 49% of 4 MiB app slot free |
| S3 `build_signed` | `0xE61E0` | 942,437 B | DIRAM 156,139 B (45.69%), IRAM 16,384 B (100%) | 52% of 1.875 MiB app slot free |
| S3 `build_legacy_audit` | `0xE6070` | 942,081 B | DIRAM 156,139 B (45.69%), IRAM 16,384 B (100%) | 10% of legacy 1 MiB app slot free |

The legacy image being only 356 bytes smaller than the shipping image does not
mean its panel path is free: shared component dependencies still pull much of
the legacy surface into both configurations. R5D should measure the result only
after decoupling `control_link` from `panel_io`.

## Reproducible Legacy Build

The shipping defaults remain FLX4 host + translator. The audit profile is
loaded afterward and disables host-only audio/link features:

```powershell
cd firmware\control-board-s3
idf.py -B build_legacy_audit `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.legacy.defaults" build
```

The profile is for compile-time retirement decisions only and must never be
flashed onto the installed DDJ-FLX4 control board.

## Batch Gates

- R5B may remove only facade symbols with no remaining firmware/test callers.
- R5C must preserve master, PFL, headphone, FX and limiter assertions through
  the canonical full mixer entry point.
- R5D pauses for explicit approval before deleting the working legacy S3 mode.
- R5E must keep playback alive and select platter-hold fallback when canonical
  scratch history is unavailable.
- R5F compares final size output against this baseline and records hardware
  acceptance.

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
