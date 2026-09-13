# P4 Development Plan

Status: **active P4-only plan, reconciled 2026-09-13**.

## Current position

The direct dual-root product path is implemented:

- USB0 hosts Rekordbox media;
- USB1 hosts the DDJ-FLX4 MIDI and four-channel USB audio interfaces;
- P4 owns controller state, playback, UI, LEDs, MAIN and cue audio;
- `RC2-128-g495947e` is the latest installed exact hardware image on `ota_1`;
- deterministic Group F support is implemented and hardware-qualified: the PDB
  reader releases media ownership after at most 8 KiB, incomplete rebuilds
  fail closed, and a guarded one-shot validation barrier lets the harness
  request removal after the first PDB read instead of relying on operator
  timing; host tests, the ESP-IDF 6.0.2 signed build, exact-image OTA smoke and
  all five Group F hardware cycles pass;
- the earlier `RC2-116-g77d723c` targeted three-hour continuous dual-MP3
  limiter/WDT soak passed without
  reset, underrun, active UAC loss or USB/controller loss;
- 14 rare output-late warnings were investigated as bounded I2S
  pacing/scheduler jitter with no downstream failure, so no code change was
  made;
- the current bench common 5 V and dual-VBUS measurement gate is
  operator-confirmed PASS;
- the 30-minute exact-image dual-active MP3 gate is closed;
- lifecycle Groups A--F pass: 26/50 controlled cycles and 26 planned physical
  attachment/reconnect actions are complete; Group G active load/decode and
  playback removal is next. Deterministic Group G audio-load support is
  committed, signed and installed; its host/build/package gates and focused
  exact-image dual-deck smoke pass, but no Group G physical cycle counts yet.

No further architecture conversion is planned. Remaining work is ordered
release qualification, with implementation only when a measured gate exposes a
real defect.

## Ordered remaining phases

Phases 1 and 2 are complete. Phase 3, the complete dual-USB lifecycle matrix,
is 26/50 cycles complete through Group F; Group G active load/decode and
playback removal is next on the installed exact candidate.
Phases 4--11 remain
deferred for later sessions.

| Phase | Work | Exit criterion |
| --- | --- | --- |
| 1 | Publish the P4-only source of truth, archive legacy active gates and record `c8b2711` versus the installed baseline | Active documents are P4-only, links pass and remote SHA matches |
| 2 | Qualify common 5 V and both downstream VBUS branches | Measured startup/sustained margins, no backfeed, current limiting/protection documented |
| 3 | Run complete dual-USB lifecycle matrix | Cold/warm boots, both insertion orders, repeated USB0/USB1 reconnect, active-decode removal and reboot recovery pass |
| 4 | Qualify bounded media cache with real files | Verified MP3/WAV/FLAC fixtures run simultaneously without audible artefact, locked read or recovery-counter failure |
| 5 | Measure P4 real-time audio margin | Worst-case dual Master Tempo, pitch/sample-rate, scratch and MAIN/cue deadlines remain inside budget |
| 6 | Close functional DSP/FX rows | Near-EOF, STOP/LOAD, mixer/PFL and detailed Beat FX transition/routing checks pass |
| 7 | Close Wi-Fi, web, profile and OTA fault paths | Pull/push OTA, rollback, invalid/slow/interrupted requests and post-reboot USB recovery pass |
| 8 | Run multi-hour combined soak | Defined multi-hour run completes without reset, media/controller loss, latched control or gated counter increase |
| 9 | Qualify final enclosure | Power, temperature, RF, connector strain and wired recovery pass with enclosure closed |
| 10 | Resolve production security | Credentials, signing-key storage/rotation, SBOM and irreversible security decisions are implemented or explicitly accepted |
| 11 | Freeze and release | Clean exact commit, full automated gates, signed artifact, complete manual smoke, validation record, merge and tag |

## Test policy

- Use ESP-IDF v6.0.2 only.
- Do not optimize DSP without a failing on-device deadline measurement.
- Do not infer WAV/FLAC acceptance from database rows; verify the files exist.
- Treat HTTP upload success as transport evidence, not boot or functional
  validity.
- Use the stricter lifecycle requirement when documents disagree: target 50
  controlled USB lifecycle cycles, with at least 20 independent physical
  reconnects and explicit active-load/decode removals.
- Preserve raw counters, firmware version, slot, boot epoch and operator-visible
  or audible results for every acceptance session.
- Do not merge the feature branch before the full recovery gate and remaining
  mandatory qualification pass.

## Deferred work

Non-FLX4 controllers, recorder re-enable, LIBAPTA integration and new optional
features remain separate projects after the first FLX4 release.

The executable session plan and evidence fields are in
[`migration/P4_DUAL_USB_NEXT_SESSION.md`](migration/P4_DUAL_USB_NEXT_SESSION.md).
