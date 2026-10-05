# M2.5 USB warm-reboot investigation

> Historical / scenario-specific record, indexed 2026-10-06. Results, hashes
> and pending items below apply to the named images and sessions. They are not
> a current installed-device or public-channel claim. See the
> [v91 release](JC4880_V91_RELEASE_20261005.md) and [current status](../DOCUMENTATION_STATUS.md) for later acceptance;
> no NOT RUN or waived scenario is converted into PASS by this reconciliation.

Status: **unchanged M2.4 software-reboot and signed OTA checks passed on corrected supply; operator audio/control acceptance passed**.

## Scope

M2.5 is planned to include automatic post-OTA dual-root USB recovery and
physically qualified additional DJ controllers. This investigation addresses
the reboot path first. M2.4 remains the published production release.

## Baseline reproduction

On 2026-10-01 the device reported M2.4 on `ota_1`, OTA idle, both decks idle,
USB0 mounted and FLX4 MIDI/UAC active. The service journal identified boot 561
(`POWERON`) and a 324-track library. USB daemon/recovery failures were zero.

A bodyless marked POST to `/api/validation/reboot` acknowledged the software
restart. The AP disappeared and did not return. The operator observed repeated
reboots and restored the device with a full power cycle. The next journal
header was boot 567 (`POWERON`); boots 562-566 had no durable journal records.
The missing records do not identify the failing subsystem or reset reason.

Boot 567 restored USB0 at 1409 ms, the 324-track library at 1632 ms, FLX4 at
1928 ms and Wi-Fi at 3358 ms. USB daemon/recovery failures remained zero.
The retained crash summary (`bus_init_internal sdio_drv.c:1530 (sdio_handle)`,
task `wifi_link`) matched the pre-test snapshot exactly. It is not evidence
that the observed reboot loop was caused by that assertion.

Local raw evidence is saved under the ignored `tmp/m25-usb-recovery/` directory
as `baseline-*`, `powercycle-*` and `reconnected-*` JSON and journal snapshots.

## Changed physical assumption

The operator subsequently identified that the device was using a weaker power
supply, and reported that it may also have been used during the M2.3/M2.4 OTA
installations. The historical supply identity is uncertain. The operator then
replaced it with the correct supply. Boot 568 (`POWERON`) restored both USB
devices and the 324-track library with zero USB daemon/recovery failures.

The weak-supply run is not evidence that a firmware change is needed. Prepared
diagnostic changes were removed from active firmware before installation;
neither a new binary nor any diagnostic firmware was installed. Interrupted
build/host-test runs for that discarded experiment are not claimed as passes.
The local diagnostic patch is retained only in the ignored evidence directory.

Retest the unchanged signed M2.4 image with both roots occupied: controlled
software reboot, signed opposite-slot OTA, catalog count, strict fault counters,
dual-deck playback and operator MAIN/cue confirmation. Keep the published M2.4
limitation until the corrected-supply evidence supports a narrower conclusion.

## Corrected-supply software restart

The first retest runner timed out reconnecting the PC to Wi-Fi. Later journal
inspection showed that boot 569 (`SW`) had actually restored the library and
FLX4 before 2 seconds. A subsequent boot 570 was `POWERON`, so that window is
not a complete hands-free acceptance result. The runner was corrected to avoid
reissuing Wi-Fi association requests every few seconds.

The next controlled restart passed telemetry on unchanged M2.4:

- boot 570 -> 571, reset `SW`, same `ota_1` slot;
- USB0 mounted at 1628 ms, all 324 tracks available at 1847 ms;
- FLX4 initially attached at 1340 ms, disconnected at 1457 ms and recovered
  automatically at 1920 ms; profile active at 1929 ms;
- Wi-Fi started at 3361 ms;
- post-boot strict checks passed, including USB daemon/recovery and active UAC;
- 15-second dual-deck window advanced 15286/15285 ms with 2866 submitted audio
  blocks and no playback-gate failures; both decks stopped afterward.

Evidence: local `good-sw2-result.json`. Acoustic acceptance remains separate.

## Corrected-supply signed OTA

The existing M2.4 bundle was independently signature-verified before upload.
Its SHA-256 was
`c76b9160bdf9757e04b4b44e92a4f034bf6e913ac35caef7b4fa12092c3e35c4`, matching
the published release record. No firmware logic or release assets changed.

- Signed push OTA acknowledged success, boot 571 -> 572, reset `SW`.
- M2.4 switched from `ota_1` to `ota_0`; OTA returned idle with no error.
- USB0 mounted at 1627 ms; the full 324-track library returned at 1848 ms.
- FLX4 attached at 1339 ms, briefly disconnected at 1456 ms and automatically
  returned at 1919 ms with the profile active at 1926 ms.
- Wi-Fi started at 3363 ms. No power cycle or cable removal was requested.
- Strict post-boot USB/UAC checks passed. A 15-second dual-deck window advanced
  both decks 15307 ms with 2870 submitted blocks and no playback-gate failures.
- The runner stopped both decks after the telemetry window.

Evidence: local `good-ota1-result.json`. After the OTA test the operator
confirmed clean MAIN/cue and normal physical controls/LEDs without a power cycle
or cable removal: "sve je cisto i uredno". The automated JSON retains its
original `acoustic=NOT RUN`; this later confirmation supplies the separate
operator acceptance rather than modifying the machine-captured result.

## Interpretation

The corrected-supply runs demonstrate automatic software-reboot and signed
OTA recovery on the existing M2.4 binary. A firmware fix is not justified by
these results. The operator's supply diagnosis is supported, but historical
power conditions were not measured and the exact old failure cannot be assigned
exclusively to supply quality from these two runs. Preserve the dated M2.4
failure record and qualify the final M2.5 image and new controllers separately.
