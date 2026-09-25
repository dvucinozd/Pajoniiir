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
remained zero. Physical display decay still requires visual confirmation.

## Acceptance status after this run

Closed by this exact-image run:

- 180-minute dual-deck automated soak and operator-confirmed MAIN/cue audio;
- real-media distinct and repeat/warm load latency with concurrent playback;
- catalog identity across a controlled reboot/remount;
- slow-network mobile-viewport web command convergence;
- API-level MAIN meter decay precheck.

Still open before promoting this maintenance candidate to a new immutable
production release:

1. Compare cue A/C and loop slots/times from a real Rekordbox export with the
   Rekordbox display.
2. Exercise two media/export identities that deliberately share the same raw
   numeric track ID, including remount and reboot isolation.
3. Measure the P4 timeline publication critical-section maximum and execute at
   least 100 forced wrap/handoff repetitions in an instrumented hardware build.
   The 309 host timeline tests cover wrap/reset/handoff logic but cannot prove
   real scheduler timing.
4. Run the Wi-Fi Remote on a physical phone connected to the Pajoniiir AP and
   visually confirm MAIN meter decay on that client.
5. If ANLZ cache writes are enabled later, separately qualify true cold versus
   cache-hit loads; current production configuration has no cache-write path to
   qualify.

No new release tag or public `latest.json` change is justified until the
applicable open gates are either passed or explicitly accepted with a recorded
scope decision.
