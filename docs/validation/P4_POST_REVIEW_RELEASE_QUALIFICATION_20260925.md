# P4 post-review release qualification — 2026-09-25

## Scope and identity

This record covers the post-review maintenance candidate at merge commit
`751d3c6ad2bc92faf4aa6500a8800753250fe985`. The installed CI image reported
`M2.2-37-g751d3c6`, ran from `ota_0` and retained the published M2.2 public OTA
channel. It does not replace or move the immutable `M2.2` tag or its release
assets.

The final live snapshot used for this record was boot 549. USB0 storage was
mounted, DDJ-FLX4 `2B73:0045` was connected on USB1, the
`pioneer_ddj_flx4` profile was active, MIDI IN/OUT and UAC were ready, and
`root_power_mask` was 3. Both decks were left stopped in `READY` state.

## 180-minute combined hardware soak

The exact installed image completed a 180.058-minute dual-deck soak on boot
548. The harness collected 2,071 samples and performed 60 alternating seek,
cue/restart and loop set/clear operations while both decks played.

The firmware version, `ota_0` slot and boot epoch remained unchanged. PCM
underruns, locked-backend reads, UAC data-loss flags, dropped blocks,
overflow, packet failures, lost frames, USB daemon/recovery failures,
service-log drops and TWDT events had zero gated delta. `output_late` increased
by 26 against the acceptance limit of 120; the largest observed late interval
was 28,592 us. The automated result was PASS.

The operator then confirmed that both MAIN and cue remained clean for the
whole run. The combined automated and acoustic result is PASS.

## Real-media latency and catalog persistence

The connected Rekordbox medium contained 324 tracks and library generation 1.
Thirty distinct real-track loads to D1, with D2 playing, produced:

| Metric | Result |
| --- | ---: |
| p50 | 599.410 ms |
| p95 | 782.263 ms |
| maximum | 803.891 ms |

All strict audio, UAC, packet, USB and service-log deltas remained zero;
`output_late` increased by 6. A later same-title repetition in the original
harness used an invalid completion predicate and intentionally received a 409
while a load was busy. That harness-level result does not invalidate the 30
distinct-track samples, but the combined raw file must not be cited as an
overall PASS.

A corrected repeat/warm end-to-end scenario alternated two already loaded
track keys so every completion was observable. Thirty loads produced p50
778.144 ms, p95 1,009.590 ms and maximum 1,044.590 ms, with zero strict fault
delta and `output_late` +5. Metadata still came from USB. Production config
intentionally disables `CONFIG_LIBRARY_ANLZ_CACHE_WRITE`, so this is not a
cache-hit qualification.

A controlled software reboot from boot 548 to 549 preserved all 324 canonical
catalog rows. The sorted row digest was identical before and after reboot:
`9fc36bf205efcffff6cbb4ffcd50650d00d0356e3565a73539e714ff3264f44b`.
The post-reboot journal measured 218 ms from USB mount to library ready. A real
MP3 loaded in 640.978 ms and a real FLAC in 740.376 ms. The retained historical
crash signature did not change.

## Web Remote and MAIN meter precheck

Desktop Chromium in a 390 x 844 mobile viewport rendered all 324 library rows.
With 900 ms deliberately added to each `/api/` request, search selected
`TAINTED DUB - CLIP.mp3`, D1 loaded the correct track, PLAY changed the
authoritative state to `PLAYING`, position advanced and the final state
returned to `READY` without a stale title.

The injected delay exceeded the normal status-poll timeout and therefore
produced expected `AbortError` poll messages. The load/play command and later
authoritative state still converged correctly. This is a slow-network browser
emulation result, not a physical-phone acceptance result.

The MAIN meter API precheck reached a playing peak of 14,790. After STOP it
fell to 1,802 at 100 ms, 100 at 200 ms, 7 at 300 ms and zero at 400 ms, then
remained zero.

The physical-phone follow-up ran on 2026-09-27 against the restored exact
candidate `M2.2-37-g751d3c6`, `ota_0`, boot 552. A phone connected to the
Pajoniiir AP displayed the Web Remote while D1 loaded and played real track key
115, `House Of Confusion.mp3`. Four automated playback checkpoints reported
advancing positions and non-zero MAIN peaks:

| Elapsed | D1 position | MAIN peak |
| ---: | ---: | ---: |
| 1 s | 32,983 ms | 769 |
| 4 s | 36,107 ms | 11,511 |
| 8 s | 40,222 ms | 944 |
| 12 s | 44,303 ms | 14,561 |

After STOP, the API meter fell from 3,911 at 78 ms through 502 at 254 ms, 79
at 426 ms and 18 at 600 ms to zero at 788 ms. It remained zero through the
last 2,045 ms sample. The operator confirmed that the physical phone correctly
showed the title, PLAY state, active MAIN meter and visible decay to zero.

The first monitoring attempt was not counted because one PC-side status request
hit its five-second HTTP timeout before the planned STOP sampling. A separate
cleanup stopped D1 and verified `READY`; no firmware reboot or fault accompanied
the timeout. The bounded retry completed with longer HTTP timeouts and mandatory
cleanup. PCM underruns, `output_late`, UAC data-loss/drop/overflow/packet,
USB daemon/recovery, service-log and TWDT indicators remained clear, and D1 was
left stopped with the meter at zero. This closes the physical-phone and visible
MAIN-meter gate; it is not a new acoustic claim.

## Acceptance status after this run

Closed by this exact-image run:

- 180-minute dual-deck automated soak and operator-confirmed MAIN/cue audio;
- real-media distinct-track load latency with concurrent D2 playback;
- real-media repeat/warm end-to-end load latency without a separately recorded
  concurrent-playback assertion;
- catalog identity across a controlled reboot/remount;
- slow-network mobile-viewport web command convergence;
- API-level MAIN meter decay precheck;
- physical-phone Web Remote state and visible MAIN meter decay; and
- duplicate raw track-ID isolation across two independent Rekordbox exports,
  including separate store/recall positions, remount, software reboot and
  cleanup.

Accepted unrun scope for the M2.3 release decision:

- The operator explicitly removed the real Rekordbox cue A/C and loop
  slot/time comparison from the current release scope on 2026-09-28 because
  it is not currently relevant. It remains **NOT RUN — OPERATOR ACCEPTED** and
  is not reported as a pass.

The duplicate-ID hardware gate passed on 2026-09-28. Two independent exports
shared raw `track_key=1` but recalled isolated Hot Cue positions at 11000 ms
and 22000 ms after remount; A also retained its position across software boot
`553 -> 554`. Both cues were cleared with operator-confirmed LED-off state,
and all strict health counters remained clean. See
[`P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md`](P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.md).

If ANLZ cache writes are enabled later, separately qualify true cold versus
cache-hit loads; current production configuration has no cache-write path to
qualify.

The former timeline item was closed on 2026-09-27 by a default-OFF
instrumented build from `07618a5`: 100 forced wrap/handoff iterations, 200
reader handoffs, a 1 us maximum publication critical section and zero failures.
The same image passed a paired three-minute live dual-deck runtime smoke before
the exact `M2.2-37-g751d3c6` candidate was restored. See
[`P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md`](P4_PCM_TIMELINE_SCHEDULER_PROBE_20260927.md).

All pre-tag gates in the selected scope are now passed or explicitly accepted.
Because a prior candidate image does not qualify a new tagged binary, the
two-media duplicate raw track-ID gate must run again after exact M2.3
installation and before publication. The release decision and remaining
sequence are recorded in
[`M2_3_RELEASE_DECISION_20260928.md`](M2_3_RELEASE_DECISION_20260928.md).
