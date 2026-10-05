# P4 duplicate raw track-ID hardware acceptance — 2026-09-28

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

## Result

**PASS** on installed firmware `M2.2-37-g751d3c6`, slot `ota_0`.

Two independently generated Rekordbox exports exposed the same non-zero raw
`track_key=1` while retaining distinct observable titles:

- medium A: `Come To The Light (Original Mix).mp3`, 396000 ms;
- medium B: `Kome to do lajt.mp3`, 396000 ms.

The test used D1 Hot Cue pad 8. Medium A stored 11000 ms and medium B stored
22000 ms. A recalled 11000 ms after remount and again after a firmware-owned
software reboot. B recalled 22000 ms after remount. Both test cues were then
cleared and the operator confirmed pad 8 LED OFF on each medium. Medium A was
left inserted.

## Identity and lifecycle evidence

The initial catalog observations were:

| Medium | Catalog generation | Raw track key | Title |
| --- | ---: | ---: | --- |
| A | 8 | 1 | `Come To The Light (Original Mix).mp3` |
| B | 10 | 1 | `Kome to do lajt.mp3` |

The acceptance run began with A remounted at generation 12. Every store,
recall and clear action produced a physical MIDI packet delta and a semantic
event delta. The recall probes began 10000 ms away from the saved position:

| Stage | Before | Expected/observed after | MIDI / semantic delta |
| --- | ---: | ---: | ---: |
| A store | 11000 ms | 11000 ms | 2 / 2 |
| B store | 22000 ms | 22000 ms | 2 / 2 |
| A remount recall | 1000 ms | 11000 ms | 2 / 2 |
| A post-reboot recall | 1000 ms | 11000 ms | 2 / 2 |
| B remount recall | 12000 ms | 22000 ms | 2 / 2 |
| B clear | 22000 ms | 22000 ms | 1 / 1 |
| A clear | 0 ms | 0 ms | 1 / 1 |

The software reboot advanced boot epoch `553 -> 554` with reset reason `SW`.
Firmware version and slot were unchanged. USB0 storage and USB1 FLX4 returned
automatically within the harness recovery window before the post-reboot recall.

## Health evidence

All monitored stages retained:

- FLX4 profile, MIDI IN/OUT and UAC active;
- USB0 mounted and `root_power_mask=3`;
- OTA idle with an empty error;
- zero PCM underrun and `output_late` deltas;
- zero active UAC data-loss flags, dropped blocks, overflow frames, packet
  failures and packet-lost frames;
- zero topology, interface-claim, transfer-allocation, probe-event, daemon,
  runtime-queue, recovery-failure, recovery-queue-drop and service-log-drop
  errors; and
- no current TWDT ISR indication.

The first attempt stopped before media replacement because the harness treated
`recovery_requests == recovery_successes` as the only valid accounting. Live
diagnostics showed the firmware's valid terminal split instead:
`37 requests = 27 successes + 10 suppressed-active`, with zero failures and
queue drops. The harness was corrected to account for successful,
suppressed-active, coalesced and failed terminal outcomes while retaining a
strict zero-failure policy. PowerShell 5.1 and 7 self-tests cover balanced
suppressed-active and rejected unaccounted cases. The acceptance run then
completed without bypassing any hardware step.

The complete machine-readable record is
[`P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.json`](P4_DUPLICATE_TRACK_ID_ACCEPTANCE_20260928.json).
The execution procedure remains
[`P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md`](P4_DUPLICATE_TRACK_ID_ACCEPTANCE_PROCEDURE.md).
