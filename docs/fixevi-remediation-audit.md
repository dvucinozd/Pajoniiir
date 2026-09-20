# P4 Release Remediation Audit

Status: **active P4-only tracker, reconciled 2026-09-20**.

The full legacy audit is retained in
[`ARCHIVE_FIXEVI_REMEDIATION_AUDIT.md`](ARCHIVE_FIXEVI_REMEDIATION_AUDIT.md).
Only the rows below remain relevant to the active product.

## Review remediation closed in source at `c8b2711`

The `c8b2711` review checkpoint fixes audio worker teardown/ownership,
output-path lock blocking, controller connection delivery convergence, UAC
cleanup/write races, per-packet USB loss accounting and invalid low hardware
sample-rate selection. The full P4 host suite, UI simulator, keylock soak,
ESP-IDF 6.0.2 product/harness builds and signed-package verification pass. Its
source protections are retained by the later installed `77d723c` checkpoint;
the remaining fault-injection, worst-case timing and listening gates stay open.

Evidence:
[`validation/CODE_REVIEW_P4_REMEDIATION_20260906.md`](validation/CODE_REVIEW_P4_REMEDIATION_20260906.md).

## Focused fix closed at `af597d8`

| Finding | Software | Exact-image hardware result |
| --- | --- | --- |
| A web transport request can wake the screensaver but lose the first action | `deck_core_queue_remote_event()` wakes without consuming the command; PLAY waits for authoritative state change and returns conflict when unchanged | PASS after more than 120 seconds idle: first request returned HTTP 200 and Deck 1 entered PLAYING |
| Web UAC `data_loss` uses lifetime idle underflows | Health monitor latches playback-session loss flags; status exposes `data_loss_flags` and keeps lifetime counters diagnostic-only | PASS: lifetime underflow remained nonzero while `data_loss=false`, flags `0`; 30-second dual-active deltas were zero |

Evidence:
[`validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md`](validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md).

## Limiter telemetry WDT regression closed at `77d723c`

The limiter-statistics path no longer holds the audio-engine lock while
updating shared telemetry. The complete P4 host suite, clean ESP-IDF v6.0.2
signed build and signed-package verification passed. Exact image
`RC2-116-g77d723c` was installed on `ota_1` and completed a targeted three-hour
continuous dual-MP3 loop on one boot epoch without watchdog reset, PCM
underrun or active UAC loss and without observable output or USB/controller
failure.

Fourteen `AUDIO_OUTPUT_LATE` warnings were recorded over 1,999,090 submitted
UAC blocks. The maximum was `12,169 us` against an `11,610 us` warning
threshold. Source and phase-counter analysis supports bounded blocking-I2S
pacing/scheduler jitter, while no downstream failure supports changing code.
The broader combined-load timing/listening gate remains open.

Evidence:
[`validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md`](validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md).

## UAC idle-continuity regression closed in the current candidate

The first final combined soak on `RC2-153-g66b5fee-dirty` reproduced a strict
FLX4 UAC underflow after D2 loop clear plus D1 CUE/restart. The output task had
slept without producing frames while both deck renderers were temporarily
inactive, although the isochronous FLX4 endpoint continued draining its ring.
The output task now clocks explicit silent MAIN/headphone blocks through that
transition. The complete host suite and ESP-IDF v6.0.2 signed build pass, and
the exact OTA image completed 126 fully sampled focused transitions plus one
partially sampled natural-EOF transition with zero UAC, PCM, packet or reboot
delta.

Evidence:
[`validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md`](validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md).

The following fresh combined soak passed for 180.156 minutes with 60 scheduled
operations, zero strict counter delta, no reboot/TWDT and operator-confirmed
clean audio. This closes the time-based regression gate. Evidence:
[`validation/P4_FINAL_COMBINED_SOAK_20260920.md`](validation/P4_FINAL_COMBINED_SOAK_20260920.md).

## M2 beta remediation complete

The rows below preserve the original remediation-to-gate mapping. All mandatory
M2 beta gates represented by them were subsequently closed or explicitly
waived and documented before merge. The current production-only boundary is in
[`DOCUMENTATION_STATUS.md`](DOCUMENTATION_STATUS.md) and
[`validation/M2_POST_MERGE_20260920.md`](validation/M2_POST_MERGE_20260920.md).

| Area | Implemented protection | Remaining gate |
| --- | --- | --- |
| Deck/load lifecycle | Actor-owned state, generation barriers, bounded worker completion and stop/join ordering | USB0 removal during load/decode, repeated STOP/LOAD/EJECT and post-stop task/resource check |
| Controller identity and reconnect | Exact FLX4 identity gate, profile epoch validation, one bounded recovery epoch, held-state convergence and LED resync | Repeated physical FLX4 reconnect, queue pressure and held-control release |
| USB0 ownership and recovery | Desired/current reconciliation, indexed idle-only root recovery, callback-safe teardown and lifetime 8 KiB DMA transfer | Cold/warm boot, both insertion orders and repeated mount/disconnect recovery |
| Audio precision and output | Wide-float channel path, post-sum limiter, bounded I2S writes, session UAC health and diagnostics | Loud real material, blocked-I2S STOP/reload, worst-case P4 deadline and listening acceptance |
| Bounded media cache | Eight 32 KiB pages per deck, exact reads, fault epochs and 64-bit LRU stamps | Verified real WAV/FLAC plus sustained mixed-format dual-deck and USB fault testing |
| Pull and push OTA | Signed manifests, version/channel binding, bounded parsing, transition lease and rollback path | Hardened pull round trip, slow/interrupted client, invalid offer/bundle and post-reboot dual-USB recovery |
| UI ownership | Controller browse/load commands execute in the LVGL task; bounded paginated Library | Full final-candidate display/touch/product smoke under combined load |
| Recorder safety | Producer gate and transactional finalization | Not a release gate while recorder remains compiled out |

## Mandatory acceptance order

1. 50-cycle dual-USB lifecycle/recovery matrix;
2. real MP3/WAV/FLAC bounded-cache stress;
3. P4 audio deadline/listening and detailed DSP/FX smoke;
4. guarded web/profile/OTA fault matrix;
5. multi-hour combined-load run;
6. closed-enclosure power/thermal/RF/service acceptance, including repetition
   of the passed bench electrical measurements;
7. exact final-candidate full smoke and release record.

The current bench protected 5 V/VBUS gate passed by operator report on
2026-09-11. See
[`validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md`](validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md).

No further source optimization is authorized solely by an old audit note; a
new code change must be tied to a reproducible current P4 failure or a measured
budget violation.
