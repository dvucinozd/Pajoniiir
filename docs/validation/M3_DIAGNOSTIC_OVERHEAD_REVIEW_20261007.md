# M3 diagnostic overhead review, 2026-10-07

Scope: clean shared-core source `e4536de2ec6fc9fb25a6e8bbdb17e4a4b4143d9a`,
the pinned local ESP-IDF 6.0.2 implementation, and the installed M3 candidate.
This is a code/telemetry review, not a claim that logging explains the entire
dual-waveform regression. See the [waveform record](M3_SHARED_WAVEFORM_REGRESSION_20261006.md).

## Findings

| Mechanism | Current behavior | Assessment |
|---|---|---|
| Structured service events | Fixed-size record, 128-entry queue, zero-wait enqueue; a priority-2 worker formats/writes on microSD | Producer work is bounded. SD write/rotation/fsync still competes through `sd_io_gate`; asynchronous does not mean free |
| Audio block outlier | At most one report burst per two seconds, queued service events; steady output deadline counter does not print | Preserve anomaly evidence; no normal per-block UART or filesystem output |
| Audio WDT / library-load journals | Fixed two-slot retained-RAM records and release publication, no NVS/filesystem work in steady audio/load phase markers | Bounded RAM instrumentation; preserve it |
| UI verbose diagnostics | `UI_DIAGNOSTICS_ENABLED=0`; no override in the compiled M3 commands | Recurring verbose UI timing print paths are disabled |
| Scanout timing | Fixed counters/histograms under a short critical section; ISR stamps refresh time; no allocation or printing | Bounded instrumentation, useful for the current regression; its exact hardware cost has not been independently measured |
| Decoder/preload INFO reports | Synchronous `ESP_LOGI` every 120 decoded batches / 64 preload chunks; decode report is outside the decoder mutex | Avoidable UART work in a producer task. INFO is compiled in and UART0 is 115200 baud, even without a connected reader |
| Transport lifecycle INFO | Some seek/resize/startup/EOF messages are inside `AE_LOCK`; EOF completion can print from the output task | Intermittent but inappropriate for an audio deadline or shared decoder lock; inspect/move these rather than globally muting faults |
| Memory inspection | Every status request and five-second health sample queries the largest internal/DMA free blocks; PSRAM largest is already unavailable | Still performs TLSF pool walks under heap critical sections. Low task priority does not bound interrupt-masking heap work |
| Stack high-water sampling | Task-owned stack scans, limited to once per second; output/decode further gate by block count; LVGL scan occurs after waveform writes | Frequency is bounded, scan cost depends on unused stack size. Cost is included in output/handler measurements; no per-sample log |
| `/api/resources` | Atomically copies allocation/stack observations and the tracked LVGL byte count | Does not walk the LVGL heap |
| `/api/diagnostic-log` | Forces a service-log sync, then reads the file in chunks | Heavy administrative I/O; do not download during timing/listening windows |

Code inspected: `service_log/service_log.c`, `audio_engine/audio_engine.c`,
`audio_engine/audio_engine_memory.c`, `audio_engine/audio_wdt_trace.c`,
`library/library_load_trace.c`, `ui/ui_scanout_timing.c`,
`ui/include/ui_diagnostics.h`, `ui/ui_lvgl_backend.c`,
`ui/ui_psram_allocator.c`, `p4_app/app_main.c`, and
`firmware/common/firmware_health/firmware_resources.c`.

The local IDF `heap_caps_get_largest_free_block` obtains heap information through
`multi_heap_get_info_impl`, which walks the TLSF pool while holding the heap's
critical-section lock. The UART VFS serializes writes and sends characters
through the TX FIFO/driver. A 150-byte 8N1 line occupies about 13 ms on a
115200-baud wire; this is an illustrative wire duration, not a measured caller
stall (FIFO occupancy, driver mode and preemption affect that stall).

The previous 180b008c boot journal contains `HEAP_WALK_SLOW` observations of
15,900 us and 4,529 us at approximately 1,225 seconds uptime, and 6,166 us later.
These measure the complete query including preemption, **not** IRQ-disabled
duration. The four-record reporting limit bounds emitted evidence, not the
number or duration of subsequent queries. Therefore diagnostic sampling can
introduce interference even when its log writer is quiet.

## Installed candidate and short measurements

`M3-dev-ge4536de2ec6f` is installed in VALID `ota_1`, OTA idle. The signed upload
returned HTTP 200 and this installation helper observed the exact new clean
source/ELF and VALID state before its polling deadline expired.

- Image: 2,568,688 B; SHA-256 `d6f7bc5a1dcce28f8b2dba4993d95b187194c91df57ed7aac7501e415cb81c01`.
- Bundle: 2,568,876 B; SHA-256 `6f37c6199e8d93c63f792179deee35d8136dace091ccde3a5b4c25b958bca180`.
- ELF: `d0583591753efc96e49c309f955bbbbf091762cac7e175741dde34f4a42d5111`.

The same 324-track export, 44.1/48-kHz decks, MT, +5/-5 percent pitch, four-beat
loops and eight-beat waveform zoom were used. Three approximately 12-second
windows had only before/after observations; no continuous API polling or log
download occurred inside them. Both decks were stopped on completion.

| Measurement | Stopped | Solo D1 | Dual |
|---|---:|---:|---:|
| Refresh interrupts | 606 | 605 | 606 |
| Coalesced refreshes | 0 | 0 | 65 |
| Service records written (delta) | 0 | 0 | 0 |
| Service queue drops (delta) | 0 | 0 | 0 |
| Wake mean, us | 74.5 | 195.7 | 2,810.3 |
| Overview entry mean, us | 137.9 | 295.7 | 2,915.6 |
| Callback mean, us | 391.8 | 5,403.6 | 17,160.3 |
| Lower waveform finish after refresh mean, us | No redraw | No redraw | 16,247.8 |

Of 542 dual callbacks, 121 lower-waveform finishes exceeded 20 ms. This does not
show a timing repair. PCM underrun, UAC dropped/overflow/underflow and packet-loss
deltas, output-late and service-log drop deltas were all zero in these windows.
The cumulative idle UAC underflow counter is nonzero; zero deltas are not an
absolute zero-loss-since-boot claim. No operator result is inferred from counters.

The journal writer added no records during these windows; the final queue was
empty. That weighs against recurring service-file writes as the direct cause
of continuous dual deformation. It does not exclude earlier pending flushes,
UART reports, stack/heap sampling, or resource contention elsewhere. The earlier
operator reproduction without continuous polling also does not disable the
five-second health sampler or decoder UART reports.

The operator confirmed: solo sharp, dual deformed, clean MAIN/headphones sound.
The e4536de2 physical visual gate is FAIL. The fresh 60-minute soak is NOT RUN.

Raw installation/preflight/measurement evidence is under
`.cache/m3-migration/20261007-e4536de2`; its copied package evidence remains
immutable and separately records physical gates as NOT RUN.

## Canonical history cross-check

Fetched `origin` with prune; canonical master remains `9af99cd2`. Inspected
actual historical diffs, not only commit messages:

- [b5c36341](https://github.com/dvucinozd/Pajoniiir/commit/b5c36341): status/captive
  portal UART traffic was demoted and HTTP pinned to core 0. Current status
  still uses `ESP_LOGD`; HTTP stays on core 0, LVGL on core 1. This historical
  logging fix has not been lost.
- [3d1b6175](https://github.com/dvucinozd/Pajoniiir/commit/3d1b6175abc2b2d0afd9432ba4a58b87fdf8ee97):
  cue fingerprints avoid unconditional one-second cache resets; only changed VU
  segments and transport state invalidate; returning to Overview re-arms blits;
  the LVGL invalidation buffer became 64. These protections are present. The
  actual M3 compile command includes `-DLV_INV_BUF_SIZE=64`.
- [8c17a1b3](https://github.com/dvucinozd/Pajoniiir/commit/8c17a1b3e0e5be8fc10561d3eab66065fc963c4e):
  atomic playhead burn/blit/restore prevents a separate LVGL playhead competing
  with the PPA overlay. The current firmware retains that path.
- [4f0999a6](https://github.com/dvucinozd/Pajoniiir/commit/4f0999a6f7c8102e53dc9a307aafd24ed5860583)
  briefly used a normal LVGL path for the lower waveform. The later RGB565
  overlay work superseded it; restoring it would change the accepted M3 layout/
  scanout strategy, not simply recover a missing fix.
- [dabe0308](https://github.com/dvucinozd/Pajoniiir/commit/dabe03084241d75c47267cfa746c26d64ebe416c)
  limited work to one deck per tick. [651c88a0](https://github.com/dvucinozd/Pajoniiir/commit/651c88a022f46b10c63c9c9beaadf92ca20b1c7f)
  restored a two-redraw dual budget. The canonical donor at 9af99cd2 already
  has that two-redraw policy; it was not introduced accidentally by migration.

The historical M3 `docs/validation/2026-09-01-dsi506-waveform-sync.md` reproduces
the same "in water" symptom, specifically at four/eight-beat dual zoom. Its
accepted repair retained the 109-line VFP, top-to-bottom dual order and moved
waveforms before Library/Status. It explicitly rejected restoring stagger.
These policies are retained by the shared board contract and actual M3 BSP.
The source is frozen at the recorded M3 base:
[M3 display acceptance](https://github.com/dvucinozd/Pajoniiir-M3/blob/e95417c4e2fea007d2c1dcb693790914692c62ba/docs/validation/2026-09-01-dsi506-waveform-sync.md).

The history confirms multiple different causes had similar symptoms; it does
not establish a new cause. No demonstrated lost historical fix was found in
these inspected paths. Current deadline measurements and remaining blocking
loop observations still require repair and an exact-image physical retest.

## Implementation follow-up

1. Finish the exact-image operator result. Do not start a 60-minute acceptance
   soak while the visual gate remains failed.
2. Remove the remaining waveform-phase decoder waits: loop display currently
   queries `audio_engine_deck_get_loop_state` before each waveform and again in
   the armed-loop overlay. The e4536de2 status repair did not remove those calls.
   Observe loop state once without waiting and reuse a session-fenced display
   snapshot; keep transport decisions on authoritative APIs.
3. Keep steady decoder telemetry in bounded RAM or a nonblocking diagnostic
   queue. Perform formatting/UART output in a low-priority consumer; preserve
   counters, error identity and drop accounting. Move output-task/locked
   lifecycle messages off the deadline path.
4. Remove largest-block pool walks from ordinary active status/health sampling.
   Separate bounded counters from explicitly requested slow inspection, and
   expose observation time/availability. Never label a stale startup sample as
   a current active-playback memory gate or weaken missing-evidence failures.
5. Measure stack and RAM-instrumentation cost only if a residual periodic
   outlier remains. Do not remove watchdog/crash evidence just to improve a
   nominal benchmark.

Every firmware change requires a new clean source, host gates, three target
builds, exact-source CI, signed artifacts, installation and the same measured
and operator retest. Current short telemetry is neither a new soak nor a
production PASS. No public channel or release was changed.
