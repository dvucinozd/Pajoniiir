# P4 dual-USB next-session handoff

Saved: **2026-09-09**

Status: **active P4-only operational handoff**.

## Repository and device checkpoint

- Repository: `https://github.com/dvucinozd/Pajoniiir.git`
- Branch: `feat/p4-dual-usb-host`
- Current source checkpoint: `c8b27116d1d261e15357f89ee2928f333b75d291`
- Current source version: `RC2-114-gc8b2711`
- Current signed bundle: `2,453,116` bytes, SHA-256
  `e19815a1c843c64f2f10f420e13906309d779ca18a4fdbc1b39136f34c551510`
- Current application: `2,452,928` bytes, SHA-256
  `7cdaf7ec9b5d6b8d386121c0ed9e4b658b182963be86073fadafc9c148ae70e8`
- Current bundle path:
  `releases/pajoniiir-RC2-114-gc8b2711/main-deck-p4.ddjota`
- Current source validation: automated and signed-package gates pass; not
  installed or hardware-smoked
- Required SDK: ESP-IDF v6.0.2
- Latest installed version: `RC2-113-gaf597d8` from commit `af597d8`
- Installed slot: `ota_1`
- OTA state: `idle`, empty `last_error`
- Application: `2,451,840` bytes
- Application SHA-256:
  `e9966017d078dece284ee1e8c7813ea1820ebc65022a618101572641b4d40eca`
- Signed bundle: `2,452,028` bytes
- Signed bundle SHA-256:
  `6858a36f714e61926f12f88ad4c3d3b506b9a5fb728f28e3f0c43931a29a3e17`
- Bundle path:
  `releases/pajoniiir-RC2-113-gaf597d8/main-deck-p4.ddjota`

The latest installed image currently has USB0 mounted, the direct FLX4 profile active,
MIDI IN available, direct USB audio available, zero USB host daemon errors and
zero service-log drops. Both decks were deliberately stopped at the end of the
last smoke.

## Latest closed gate

Commit `c8b2711` closes the current P4 review remediations with host/build
evidence: safe audio worker teardown, nonblocking EOF/scratch bookkeeping,
durable controller connection delivery, UAC producer/cleanup ownership,
per-packet loss diagnostics and valid 44.1/48 kHz hardware-rate selection. Its
signed package verifies, but the image has not been installed; start the next
software/hardware session by installing it and running a focused regression
smoke before relying on the broader matrix below.

The latest installed baseline remains `af597d8`:

Commit `af597d8` separates physical wake-only controls from authenticated
remote controls and adds authoritative PLAY state confirmation. It also makes
UAC data-loss status playback-session scoped while retaining lifetime
underflow telemetry.

On the exact image:

- both decks remained idle for more than the 120-second screensaver timeout;
- exactly one web PLAY returned HTTP 200 and Deck 1 entered PLAYING;
- lifetime idle underflow remained visible while `data_loss=false`, flags `0`;
- a 30-second dual-deck window added zero drop, overflow, underflow and
  output-late counts;
- both decks stopped cleanly and both USB roots remained active.

Evidence:
[`../validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md`](../validation/P4_REMOTE_PLAY_UAC_HEALTH_OTA_SMOKE_20260902.md).

## Current release blocker

The product is not release-qualified because the common 5 V rail and both
downstream VBUS branches have not been electrically measured or proven
protected against backfeed and overcurrent. Earlier bench wiring produced a
raw brownout during dual-deck load. The improved supply has passed focused and
30-minute runs, but supply quality alone does not prove the installed wiring.

Do not merge the branch or close the enclosure until this gate passes.

## Session 1 — electrical qualification

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

## Session 2 — complete dual-USB lifecycle matrix

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
