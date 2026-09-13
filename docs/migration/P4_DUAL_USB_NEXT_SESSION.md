# P4 dual-USB next-session handoff

Saved: **2026-09-14**

Status: **active P4-only operational handoff**.

## Repository and device checkpoint

- Repository: `https://github.com/dvucinozd/Pajoniiir.git`
- Branch: `feat/p4-dual-usb-host`
- Validated firmware checkpoint:
  `034cd7617c0493b87874ea928d7090ff16105c01`
- Validated firmware version: `RC2-136-g034cd76`
- Current signed bundle: `2,457,836` bytes, SHA-256
  `989005f2e619d17faf53a7745683f1ac9ec70968be762366edb2c9001ac5627e`
- Current application: `2,457,648` bytes, SHA-256
  `be8bcda3b37ec72ed15073c06aab765112b61f5a1b7873f1c72934323da9fa31`
- Current bundle path:
  `releases/pajoniiir-RC2-136-g034cd76/main-deck-p4.ddjota`
- Firmware validation: complete P4 host suite, clean ESP-IDF v6.0.2 signed
  build, package verification, signed OTA and lifecycle Groups A--H plus
  I1/I2/J1 and K1/K2 pass; the earlier `RC2-116-g77d723c` passed the targeted
  three-hour limiter/WDT soak
- Required SDK: ESP-IDF v6.0.2
- Latest installed version: `RC2-136-g034cd76` from commit `034cd76`
- Installed slot: `ota_1`
- OTA state: `idle`, empty `last_error`
- Application: `2,457,648` bytes
- Application SHA-256:
  `be8bcda3b37ec72ed15073c06aab765112b61f5a1b7873f1c72934323da9fa31`
- Signed bundle: `2,457,836` bytes
- Signed bundle SHA-256:
  `989005f2e619d17faf53a7745683f1ac9ec70968be762366edb2c9001ac5627e`
- Bundle path:
  `releases/pajoniiir-RC2-136-g034cd76/main-deck-p4.ddjota`

During the latest captured hardware run, USB0 remained mounted and the direct
FLX4 profile, MIDI IN/OUT and UAC remained active, with zero USB host daemon
errors and zero service-log drops. Both decks continuously looped MP3 material.

## Earlier firmware soak gate

Commit `77d723c` removes the limiter-telemetry lock cycle that could deadlock
the audio task and trigger its watchdog. The exact image was installed through
signed OTA and then ran continuous dual-MP3 playback for approximately three
hours four minutes on boot epoch `389`.

On the exact image:

- no watchdog reset, panic or brownout occurred;
- PCM underruns, UAC drops/overflows/packet failures, active UAC loss,
  controller disconnects and USB daemon errors remained zero, and no output
  failure was observed;
- 1,999,090 UAC blocks were submitted;
- 14 rare output-late warnings reached at most `12,169 us` against an
  `11,610 us` warning threshold, without any downstream failure;
- source and phase-counter inspection supports bounded blocking-I2S
  pacing/scheduler jitter rather than a limiter or DSP defect, so no firmware
  change was made.

This closes the targeted limiter/WDT regression only. The full combined-load
soak remains open because scratch, Master Tempo, FX, web traffic, mixed formats
and controlled reconnects were not all exercised.

Evidence:
[`../validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md`](../validation/P4_RC2_116_LIMITER_WDT_OTA_SOAK_20260910.md).

## Current release gate

The current bench common 5 V rail and both downstream VBUS branches passed the
requested electrical measurements by operator report on 2026-09-11. The
earlier P0 bench blocker is closed for the unchanged current wiring. Raw
numeric readings were not preserved, so the test must be repeated if the
wiring or supply changes and with the final enclosure configuration.

The next release gate is the complete 50-cycle dual-USB lifecycle/recovery
matrix. Do not merge the branch until that and the other mandatory P1 gates
pass.

## Session 1 — electrical qualification — PASS for current bench wiring

Required equipment: at minimum a trusted multimeter or USB power meter capable
of observing voltage/current on each branch. An oscilloscope is preferable for
startup dips.

1. Disconnect power and both downstream devices.
2. Verify common ground continuity.
3. Verify native device-side VBUS is isolated on USB0 and USB1.
4. Verify no short and correct polarity on both protected outputs.
5. Verify neither independent source can backfeed another source or the P4.
6. Power the P4 from the intended regulated common supply.
7. Enable and measure USB0 VBUS alone, then USB1 VBUS alone.
8. Measure idle, cold-start, FLX4 enumeration, USB mount, Library load and
   sustained dual-deck values.
9. Record minimum voltage, maximum current, continuous current and protection
   trip behavior.
10. Repeat with final cable lengths and enclosure distribution hardware.

Acceptance:

- polarity and ground correct;
- no backfeed in any unpowered-source condition;
- both branches current-limited/protected;
- no brownout or reset;
- measured rail remains inside the device and board limits throughout startup
  and sustained load.

Result on 2026-09-11: operator-confirmed PASS for all requested measurements;
readings were reported as ideal or inside the allowed limits. See
[`../validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md`](../validation/P4_POWER_VBUS_ACCEPTANCE_20260911.md).
Final-enclosure repetition remains open.

## Session 2 — complete dual-USB lifecycle matrix

Checkpoint 2026-09-13: Groups A--H pass. Progress is 36/50 controlled cycles
and 36 planned physical attachment/reconnect actions. Group F ran on exact
image `RC2-127-g06c0e85`, `ota_0`, boot epoch 415. F1--F5 each reached the
guarded `holding` state, changed to `media_removed` on the controlled USB0
removal, kept Library empty while absent, restored all 100 tracks and passed
dual-deck playback plus physical FLX4 verification. The initially incomplete
F3 operator check did not count and was rerun cleanly. Accepted per-cycle
details are in
[`../validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md`](../validation/P4_DUAL_USB_LIFECYCLE_MATRIX_20260911.md).

Implementation checkpoint 2026-09-13: Group F no longer depends on that manual
timing window. `rekordbox_pdb` now reads one bounded page at a time and releases
`media_io_gate` after at most 8 KiB; a media loss aborts the rebuild instead of
publishing a partial Library. A guarded, one-shot validation endpoint can be
armed only while USB0 is absent and both decks are idle. The next PDB load then
pauses immediately after its first header read until USB0 is removed, the gate
is canceled, or its 60-second bound expires. The Group F harness detects the
holding state before asking for removal, verifies the `media_removed` result,
then requires a normal remount and coherent 100-track Library before playback.
The complete P4 host suite, ESP-IDF v6.0.2 signed build, exact-image OTA smoke
and F1--F5 hardware execution pass.

Group G implementation checkpoint 2026-09-13: a separate one-shot audio-load
gate can be armed for either deck only through the guarded service API while
USB0 is mounted and neither deck is loading or playing. The selected loader
reads its first bounded 32 KiB compressed-cache page, enters `holding` before
publishing `load_done`, and exits as `media_removed` when USB0 disappears.
`tools/run_p4_lifecycle_cycle.ps1` alternates decks across G1--G5, waits for the
gate before requesting removal, requires an empty Library while absent, then
requires a coherent 100-track remount and the standard dual-playback/controller
checks. The new gate unit test, harness self-test, complete P4 host suite and
ESP-IDF v6.0.2 host and clean signed builds pass. Exact image
`RC2-128-g495947e` is 2,456,528 bytes with 41% of the smallest app partition
free; its signed bundle verifies, is installed on `ota_1`, and passed focused
dual-deck smoke with zero critical audio deltas. This establishes readiness,
not Group G cycle evidence by itself. G1--G5 subsequently passed on boot 416:
the gate alternated D1/D2, every removal produced `media_removed`, every normal
remount restored 100 tracks, every recovery request matched a success and all
critical controller/audio deltas stayed zero. One first G4 attempt had an
extra fast physical USB0 disconnect/remount after reinsertion and was rejected;
the clean repeated G4 passed. Group H then continued with USB1 idle
disconnect/reconnect while USB0 remained mounted.

Group H completion checkpoint 2026-09-13: the lifecycle runner guides one
idle FLX4 disconnect/reconnect while USB0 remains untouched. It requires the
100-track Library throughout the controller absence, exactly one controller
disconnect/connect, active profile plus MIDI/UAC recovery, zero to two matched
host recoveries, at most one controller fault epoch and the standard
playback/operator checks. Its self-test is included in the complete P4 host
runner. H1--H4 passed on boot 416 and H5 passed on boot 417 after a controlled
clean reboot, all on exact installed image `RC2-128-g495947e`. Each accepted
cycle retained USB0 and 100 tracks, restored FLX4 profile/MIDI/LED/UAC and
passed dual playback plus audible MAIN/cue confirmation with zero critical
fault deltas. The initial H1 harness-policy attempt and H5 attempts containing
unintended cable/USB0 actions remain rejected and do not count.

Group I/J closure checkpoint 2026-09-14: stream-epoch remediation image
`RC2-134-g09f7efc` was installed into `ota_0`. I1, I2 and jog-held J1 passed on
boot 420 with USB0 and 100 tracks retained, exact disconnect/connect event
pairs, continued dual playback, audible MAIN/cue and zero critical fault
deltas. I3--I5 and J2--J5 are explicitly operator-waived, are not passes and
will not be resumed. The guarded `POST /api/validation/reboot` trigger and
`tools/run_p4_lifecycle_k.ps1` K1/K2 harness are now implemented and pass host
self-tests plus the ESP-IDF v6.0.2 firmware build. Exact-image K1/K2 passed on
boots 424 and 426 with both roots occupied and no manual reinsert. Boot 425 was
a technically clean second run but is not counted because its local evidence
was mislabeled K1 by a shared-script variable collision; the corrected harness
preserves and self-tests `Cycle=2`. Next action is Group L OTA-reboot recovery.

The requested release-prefix migration from `RC2` to `M2` is intentionally
deferred until after K/L and must be handled as a separate version/OTA-policy
change.

Use the exact candidate or a newer exact committed image. Record version, slot,
boot epoch and baseline counters before the first cycle.

Run all combinations:

1. cold boot with both devices already attached;
2. warm/software reboot with both devices attached;
3. boot empty, attach USB0 then USB1;
4. boot empty, attach USB1 then USB0;
5. USB0 remove/reinsert while idle;
6. USB0 remove during Library load;
7. USB0 remove during audio load/decode and active playback;
8. USB1 disconnect/reconnect while idle;
9. USB1 disconnect/reconnect during dual-deck playback;
10. disconnect while holding jog touch, Shift, Censor, Pad FX or roll;
11. verify MIDI, LEDs and UAC after reconnect;
12. software reboot and signed OTA with both roots occupied.

Target 50 controlled lifecycle cycles in total, including at least 20
independent physical reconnects. Spread cycles across both roots and the active
load/decode cases rather than repeating only the easiest idle case.

For every cycle require:

- no reset, panic or brownout;
- USB0 returns without manual workaround when it should remain present;
- Library generation and loaded-media state are coherent;
- FLX4 returns with one bounded recovery epoch;
- USB0 remains mounted during a USB1-only fault;
- no held control remains latched;
- authoritative LED state is replayed;
- MAIN and cue audio resume;
- no new host daemon, recovery-failure, queue-drop or active UAC loss flag.

## Session 3 — real media and bounded-cache qualification

1. Export real MP3, PCM16 WAV and FLAC fixtures through Rekordbox.
2. Verify each referenced file physically exists below `Contents`.
3. Include short, long, mono/stereo and at least 44.1/48 kHz material where the
   current decoder contract permits it.
4. Run mixed-format pairs on both deck assignments.
5. Exercise load, PLAY, CUE, seek, loop, Hot Cue, scratch, near EOF and replay.
6. Force sustained cache misses and repeat selected USB0 fault cases.
7. Record BNA recovery, cache/locked-backend reads, PCM underruns, output late,
   UAC drop/overflow/session-loss flags, heap and PSRAM floors.

Acceptance requires physical files, audible review and stable counters. A PDB
row or successful parser unit test is not codec hardware acceptance.

## Session 4 — P4 timing, listening and DSP/FX

1. Run both decks with Master Tempo enabled.
2. Combine different sample rates, demanding pitch values and repeated seek.
3. Measure worst output/mix/decode phase and available I2S deadline margin.
4. Exercise rapid scratch release/re-grab, STOP/LOAD and near-EOF hold.
5. Confirm PCM5102A MAIN and FLX4 cue simultaneously.
6. Use loud real material and inspect limiter/peak activity for flat-top
   distortion.
7. Complete Beat FX CH1, CH2 and `1&2` routing, beat-size changes, mode
   transitions and Echo/Delay tail checks.

Optimize source only when measured hardware evidence violates a defined
budget or produces an audible defect.

The targeted `RC2-116` run established a useful baseline: 14 warnings over
1,999,090 submitted UAC blocks, maximum `12,169 us` versus the `11,610 us`
warning threshold, with zero downstream failure. Reset/capture phase and sink
counters at the start of the later declared stress run and correlate any new
warning with the active operation before changing code. The internal main-sink
counters are not currently serialized by `/api/status`; expose them or capture
them through an equivalent exact-image diagnostic before claiming the strict
sink-error gate.

## Session 5 — Wi-Fi, web, profile and OTA fault matrix

1. Confirm the service AP and mDNS address.
2. Run hardened pull OTA through AP-to-STA-to-AP transition.
3. Verify newer-only policy and offer TTL.
4. Reject wrong size, SHA-256, signature, project, chip and version without
   changing the boot partition.
5. Exercise slow, fragmented and interrupted upload paths.
6. Confirm a new request succeeds after each failure.
7. Run signed push rollback and confirm opposite slot, exact version and empty
   error after reboot.
8. Confirm USB0 and FLX4 recover without physical reinsert.
9. Exercise guarded PLAY, LOAD, seek and profile mutations.
10. Verify profile overwrite, corrupt/interrupted rejection, reactivation and
    reboot persistence only if remote profiles are in first-release scope.

## Session 6 — multi-hour combined product soak

Define the duration before starting; use at least a multi-hour window and
prefer an overnight run for the final enclosure candidate.

Include:

- both decks continuously active;
- periodic seek/restart;
- Master Tempo, scratch, loops, Hot Cues and Beat FX;
- FLX4 MIDI and LED traffic;
- simultaneous MAIN and cue audio;
- sustained USB0 cache activity;
- web status traffic;
- controlled reconnects only at predeclared checkpoints.

Acceptance requires one boot epoch, no brownout/reset, no lost media or
controller, no latched state, no audible defect and no increase in gated audio,
USB or service-log failure counters.

The 2026-09-10 three-hour dual-MP3 loop is a passed targeted sub-gate for the
limiter/WDT regression. It does not close this session because the complete
stress mix and predeclared reconnect checkpoints were not included.

## Session 7 — final enclosure and release

With production-intent wiring and the enclosure closed:

1. repeat power measurements;
2. measure temperatures at idle and worst sustained load;
3. verify Wi-Fi/AP reachability;
4. verify connector retention and cable strain relief;
5. retain a reachable wired recovery/service connector;
6. repeat the full manual product smoke;
7. decide service credentials, signing-key custody/rotation, SBOM and
   irreversible security provisioning;
8. freeze one final commit;
9. run the full host suite, UI simulator and clean signed build;
10. install the exact final image and repeat the complete functional smoke;
11. publish hashes, slot/version evidence and all acceptance results;
12. merge only after mandatory gates close, then create the release tag.

## Resume commands

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1
idf.py --version
$repoRoot = git rev-parse --show-toplevel
Set-Location $repoRoot
git fetch --prune origin
git status -sb
git log -3 --oneline --decorate
```

Automated gates:

```powershell
.\tests\run_p4_host_tests.ps1
.\tests\ui_simulator\run_ui_simulator_e2e.ps1

Set-Location "$repoRoot\firmware\main-deck-p4"
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build
```

Network status baseline:

```powershell
Invoke-RestMethod http://pajoniiir.local/api/firmware
Invoke-RestMethod http://pajoniiir.local/api/status
```

Do not convert an unobserved physical, audible or electrical row into a pass.
