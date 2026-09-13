# P4 dual-USB lifecycle/recovery matrix

Opened: **2026-09-11**

Status: **IN PROGRESS — Groups I/J closed, Group K passed; 41 PASS, 7 waived, 2 pending**.

## Test images and current installed image

Opening Group A baseline:

- Firmware commit: `77d723c8d19b1a859b9f5b4fa8421928250c68d3`
- Installed version: `RC2-116-g77d723c`
- Installed slot: `ota_1`
- USB0 device: Rekordbox storage medium
- USB1 device: Pioneer DDJ-FLX4
- Power prerequisite:
  [`P4_POWER_VBUS_ACCEPTANCE_20260911.md`](P4_POWER_VBUS_ACCEPTANCE_20260911.md)

Capture `/api/firmware`, `/api/status` and the diagnostic log before cycle 1
and after every group. Each cycle must record its exact firmware, boot epoch
and operator-visible result.

Current installed validation image, used from remediated B2 through E5:

- Firmware commit: `7b7b29a40a8ff154c22a2d5556a81ffdb3993495`
- Installed version/slot: `RC2-121-g7b7b29a`, `ota_1`
- Application size/SHA-256: 2,452,928 bytes;
  `f3c55750ca4c555d4eb854597cc19919eba1b6320273311eba7ff4c6bb46c015`
- Signed bundle size/SHA-256: 2,453,116 bytes;
  `91bdb72ba7faf0627e4c7fd3fe94ddbb38df7d84af1b1d0aabd888ac4af3719f`
- Installed by signed push OTA with USB0 and USB1 continuously attached; boot
  402 reports `SW` reset.

Current installed Group F validation image:

- Firmware commit: `06c0e858ec77b0264b21566ee726e2d2135f365f`
- Installed version/slot: `RC2-127-g06c0e85`, `ota_0`
- Application size/SHA-256: 2,454,848 bytes;
  `9ad6149bf48605ad6b25b76f097e53c82bf1cca6c7f3d7fe9fc6f76c36875cdc`
- Signed bundle size/SHA-256: 2,455,036 bytes;
  `2aa8162ff522905e3e055d40656f5ab38c4df4942c17e5d3ec5d9bdb2688a9a2`
- Installed and exact-image smoke-tested by signed OTA; F1--F5 ran on boot
  epoch 415 with USB0 and FLX4 healthy.

Current installed Group G validation candidate:

- Firmware commit: `495947e0da847b377375b898838fb0c522d97ef5`
- Installed version/slot: `RC2-128-g495947e`, `ota_1`
- Application size/SHA-256: 2,456,528 bytes;
  `656f543f6ba30e768a06e94bde1c1022b14679710a546e0eb60191288969c805`
- Signed bundle size/SHA-256: 2,456,716 bytes;
  `33952d47ee7762ce5a84b5a89e53b9f8e5618c92fc94fc86117f18b4d2c4f927`
- Clean ESP-IDF v6.0.2 build, package verification, signed OTA and focused
  ten-second dual-deck smoke pass. G1--G5 passed on boot epoch 416.

## Acceptance requirements

Every cycle requires:

- no unexpected reset, panic or brownout;
- USB0 returns when present and the Library remains coherent;
- USB1 returns through one bounded recovery epoch;
- USB0 remains mounted during a USB1-only fault;
- FLX4 MIDI, LEDs and UAC recover;
- no held jog, Shift, Censor, Pad FX or roll state remains latched;
- MAIN and cue audio resume;
- no new USB host daemon, recovery, queue-drop or active UAC data-loss fault.

The complete gate requires 50 controlled cycles, including at least 20
independent physical reconnects and explicit USB0 removal during active
load/decode.

## Guided Groups C--H harness

Groups C--H use `tools/run_p4_lifecycle_cycle.ps1` to reduce operator work
to the required cable actions and listening/controller confirmation. The
harness verifies the exact installed image, empty-boot baseline, attachment
order, both USB roles, 100-track Library, dual-deck playback progress, UAC ring
health, full-cycle counter deltas, current-boot service events and absence of a
reboot. It stops both decks and writes machine-readable JSON plus a short
Markdown summary under the ignored `tmp/p4-lifecycle` directory.

The two staged attachments produce one or two bounded host recoveries depending
on whether the second topology change requires another root reconciliation.
The harness accepts only one to two total requests, requires every request to
have a matching success and rejects any recovery failure or queue drop.

Before each cycle, disconnect USB0 and USB1, leave the P4 powered, press the
board reset button and wait for the empty boot. Run from the repository root:

```powershell
.\tools\run_p4_lifecycle_cycle.ps1 -Group C -Cycle 1 `
    -ExpectedVersion RC2-121-g7b7b29a
```

Use `-Group D` for the reverse attachment order and advance `-Cycle` from 1 to
4. The script prints `ACTION_REQUIRED` for each physical attachment and asks
for `yes` only after audible MAIN and FLX4 cue output, physical PLAY/PAUSE,
controller LEDs and absence of latched controls have been confirmed. A failed
or interrupted run remains evidence only and does not count as an accepted
matrix cycle. The pure parser/counter logic can be checked without hardware:

```powershell
.\tools\run_p4_lifecycle_cycle.ps1 -SelfTest
```

Group E starts with both devices active and both decks stopped; it does not
require a reboot between cycles. Use `-Group E -Cycle 1` through 5. The harness
waits for the expected USB0 disconnect/release and one `USB_UNMOUNTED` event,
requires Library count `0` while absent, verifies FLX4 remains active, then
requires the same medium to remount with all 100 tracks. While USB0 is absent,
the storage owner intentionally runs up to eight fast recovery probes before a
30-second slow cadence. Their count therefore depends on operator timing; the
harness requires every request to have a matching success and rejects every
recovery failure or queue drop rather than imposing a time-dependent count.

Group F also starts with both devices healthy and both decks stopped. The
harness first requests removal of USB0 and confirms the absent-media baseline,
then arms a guarded one-shot firmware barrier. After USB0 is reinserted, the
firmware pauses the Library rebuild immediately after the first bounded PDB
header read. Only after the harness observes `holding` does it ask the operator
to remove USB0. The disconnect must change the barrier state to
`media_removed`, keep FLX4 active and leave Library count zero. A final normal
reinsert must restore all 100 tracks before the standard dual-playback and
operator checks run. The barrier expires after 60 seconds and normal product
operation is a no-op unless it was explicitly armed.

Run F1 through F5 on the exact installed candidate:

```powershell
.\tools\run_p4_lifecycle_cycle.ps1 -Group F -Cycle 1 `
    -ExpectedVersion <exact-installed-version>
```

The bounded PDB reader, partial-index rejection, validation barrier and Group F
harness passed the complete P4 host suite on 2026-09-13. The exact committed
ESP-IDF v6.0.2 image was signed, installed and smoke-tested, then all five
Group F hardware cycles passed.

Group G also starts with both devices healthy and both decks stopped. A
separate guarded one-shot audio-load barrier is armed for D1 on odd cycles and
D2 on even cycles. The harness submits a LOAD; the selected loader reads its
first bounded 32 KiB compressed-cache page and enters `holding` before
publishing `load_done`. Only then does the harness ask the operator to remove
USB0. The disconnect must change the barrier state to `media_removed`, retain
FLX4 and leave Library count zero. A normal reinsert must restore all 100
tracks before the standard dual-playback and physical controller checks.

Run G1 through G5 only on the exact installed candidate containing the gate:

```powershell
.\tools\run_p4_lifecycle_cycle.ps1 -Group G -Cycle 1 `
    -ExpectedVersion <exact-installed-version>
```

The Group G gate unit test, harness self-test, complete P4 host suite, clean
ESP-IDF v6.0.2 build, signed-package verification, OTA and focused exact-image
smoke pass on `RC2-128-g495947e`; G1--G5 then passed on boot epoch 416.

Group H starts with both devices healthy and both decks stopped. The harness
asks for one FLX4 disconnect/reconnect while USB0 remains untouched. While
FLX4 is absent it requires USB0 to stay mounted and the Library to retain all
100 tracks. After reconnection it requires one controller disconnect and one
connect, active FLX4 profile, MIDI IN/OUT, LED output acceptance, UAC recovery,
zero to two matched host recovery requests, at most one controller fault epoch
and the standard dual-playback/operator checks.

Run H1 through H5 on the current exact installed candidate:

```powershell
.\tools\run_p4_lifecycle_cycle.ps1 -Group H -Cycle 1 `
    -ExpectedVersion (git describe --tags --always)
```

The extended harness self-test passes. One earlier focused USB1 reconnect smoke
passed on `RC2-109-g269036b`, but it is not one of the five formal Group H
cycles on the current candidate. H1--H4 passed on boot 416 and H5 passed on
boot 417 after a clean controlled reboot, all on `RC2-128-g495947e`.

The first current-candidate H1 attempt completed the physical reconnect,
retained USB0 and passed audio/controller checks, but the initial harness
incorrectly required exactly one soft-fault epoch. Current physical
`DEV_GONE` handling correctly produced zero. That attempt remains rejected;
the corrected gate accepts zero or one epoch and rejects duplicates.

## Accelerated paired Groups I/J harness

`tools/run_p4_lifecycle_ij_batch.ps1` runs I1/J1 through I5/J5 in one guided
session. It loads the two test tracks and starts both decks once, then seeks the
already loaded tracks back to zero before each reconnect so operator time
cannot reach EOF. Each I or J reconnect still has its own baseline, current-boot
event delta, JSON/Markdown evidence and PASS/FAIL result; one physical reconnect
therefore never counts as two lifecycle cycles. The batch stops immediately on
the first failure and stops both decks during cleanup.

Group I disconnects FLX4 while both decks are playing. Group J repeats the
active-playback disconnect while one defined Deck 1 control is held, rotating
through jog touch, Shift, Censor, Pad FX1 pad 1 and shifted Beat Loop roll pad 1.
USB0 must remain mounted with 100 tracks throughout. After every reconnect the
harness requires profile/MIDI/UAC recovery, continued dual playback, a clean
post-reconnect audio window, exact controller disconnect/connect events and an
operator-confirmed MAIN/cue, LED, transport and unlatched-control check.

Run from a clean boot with USB0 and FLX4 attached and both decks stopped:

```powershell
.\tools\run_p4_lifecycle_ij_batch.ps1 `
    -ExpectedVersion RC2-128-g495947e
```

Two pre-remediation I1 attempts do not count. The boot 417 attempt is
operator-invalid because cables were touched. The clean boot 418 attempt kept
USB0 mounted with all 100 tracks, both decks advanced by about 5.14 seconds,
FLX4 profile/MIDI/UAC recovered and audio was confirmed audible. It failed only
because the UAC health latch reported the 745-frame isochronous reconnect-prime
underflow (`flags=16`) even though underflow stopped growing and drop, overflow
and packet-loss deltas were zero. The remediation assigns a monotonic epoch to
each successfully primed UAC stream and gives only that boundary its bounded
underflow grace; all other loss classes and later underflows remain fatal.

Use `-StartPair` and `-EndPair` only to resume at a documented pair after a
failed or operator-invalid run. The batch and original lifecycle harness
self-tests are both part of `tests/run_p4_host_tests.ps1`. This harness changes
test orchestration only; no firmware build or OTA is required.

## Planned distribution

| Group | Scenario | Cycles | Physical reconnects | Status |
| --- | --- | ---: | ---: | --- |
| A | Cold boot with USB0 and USB1 already attached | 4 | 0 | PASS after remediation: R-A1, R-A2, A3 and A4 |
| B | Warm/software reboot with both attached | 4 | 0 | PASS: B1, remediated B2, B3 and B4 |
| C | Boot empty, attach USB0 then USB1 | 4 | 8 | PASS: C1--C4 |
| D | Boot empty, attach USB1 then USB0 | 4 | 8 | PASS: D1--D4 |
| E | USB0 idle remove/reinsert while FLX4 remains active | 5 | 5 | PASS: E1--E5 |
| F | USB0 remove/reinsert during Library load | 5 | 5 | PASS: F1--F5 |
| G | USB0 remove/reinsert during load/decode or active playback | 5 | 5 | PASS: G1--G5 |
| H | USB1 idle disconnect/reconnect while USB0 remains mounted | 5 | 5 | PASS: H1--H5 |
| I | USB1 disconnect/reconnect during dual-deck playback | 5 | 5 | I1/I2 PASS; I3--I5 operator-waived and closed |
| J | USB1 disconnect/reconnect while a defined control is held | 5 | 5 | J1 PASS; J2--J5 operator-waived and closed |
| K | Software reboot with both roots occupied | 2 | 0 | pending |
| L | Signed OTA reboot with both roots occupied | 2 | 0 | pending |
| **Total** |  | **50** | **46 attachment/reconnect actions** | **pending** |

Groups C and D count two physical attachment actions per cycle; groups E--J
count one remove/reinsert or disconnect/reconnect action per cycle. The total
therefore exceeds the minimum 20 physical reconnect requirement.

## Baseline

Captured from `192.168.4.1` before cycle 1. The computer was associated with
the `Pajoniiir` AP; the initial four-second HTTP probe timed out, but TCP/80 and
the subsequent 30-second API reads passed.

| Field | Value |
| --- | --- |
| Firmware/slot/OTA state | `RC2-116-g77d723c`, `ota_1`, `idle`, empty error |
| Boot epoch/reset reason | `390`, `POWERON` |
| USB0 mount/library count | mounted; `100` tracks; 1/1 mount success |
| FLX4 VID/PID/profile/MIDI/UAC | `2B73:0045`; `pioneer_ddj_flx4`; all active |
| USB daemon/recovery counters | daemon errors `0`; recovery 1 request / 1 success / 0 failures or drops |
| Controller counters | disconnects `0`; interface/transfer/queue failures `0` |
| PCM/output-late counters | PCM `0/0`; late `0`, output idle |
| UAC drop/overflow/session-loss | `0` / `0` / `false`; no packets submitted while decks idle |
| Heap/PSRAM | internal `100,711`; PSRAM `26,725,712` bytes |

The diagnostics endpoint still exposes the previously documented historical
crash dump, while current boot 390 reports `twdt_isr_seen=false`; it is not
counted as a lifecycle failure unless its identity changes or a new boot/reset
correlates with a cycle.

## Results

### A1 — cold boot with both devices attached

Status: **PASS with observation — cold boot, dual-root recovery and audible
MAIN/cue verified; a one-time startup-underflow flag did not persist or recur**.

- Boot epoch advanced `390 -> 391` with expected `POWERON` reset reason.
- Exact image remained `RC2-116-g77d723c`, `ota_1`; OTA stayed `idle` with an
  empty error.
- USB0 mounted at `1,388 ms`; the 100-track Library loaded at `1,442 ms`.
- DDJ-FLX4 connected at `1,939 ms`; profile activation completed at
  `1,946 ms`.
- FLX4 identity `2B73:0045`, built-in profile, MIDI IN, MIDI OUT and UAC were
  active.
- Storage/controller disconnects, controller fault-recovery epochs, daemon
  errors, recovery failures/drops and runtime queue failures were all zero.
- PCM underruns, output-late warnings, UAC drops/overflow/packet failures,
  active UAC data loss, service-log drops and current TWDT ISR flag were all
  zero/clear.
- USB host startup reconciliation reported one request and one success, the
  same expected startup pattern as the baseline.

The operator loaded one track on each deck and confirmed audible PCM5102A MAIN
plus FLX4 headphone cue. Both decks were still reported `PLAYING` in the first
post-check snapshot, with 9,833 UAC blocks submitted, zero PCM underrun, drop,
overflow or packet failure, and zero output-late warnings. However, that
snapshot reported `data_loss=true`, `data_loss_flags=16`, which maps to UAC
underflow. After playback stopped, three snapshots showed the expected
playback-scoped flag clear while the lifetime idle underflow counter continued
to rise.

A focused repeat then kept both decks playing beyond 30 seconds. The current
snapshot reported a nominal UAC ring with 1,030 queued frames, 36,656 submitted
blocks, `data_loss=false`, flags `0`, and zero UAC drop, overflow, packet-loss,
PCM-underrun and output-late counters. Two exact snapshots 10 seconds apart
during the same repeat recorded 1,735 additional submitted blocks, queue depth
`1,024 -> 1,175`, no underflow/drop/overflow/packet/PCM/output-late delta, flags
`0` throughout and no TWDT indication.

The earlier one-time flag 16 is therefore recorded as a bounded first-playback
startup observation rather than sustained UAC loss. A1 passes, but A2-A4 must
explicitly check whether the same startup flag recurs after a fresh cold boot;
systematic recurrence remains a release-blocking investigation item.

### A2 — cold boot with both devices attached

Status: **FAIL release gate — boot, dual-root recovery and audible MAIN/cue
passed, but first-playback UAC underflow flag 16 reproduced**.

- Boot epoch advanced `391 -> 392` with expected `POWERON` reset reason.
- Exact image remained `RC2-116-g77d723c`, `ota_1`; OTA stayed `idle` with an
  empty error.
- USB0 mounted at `1,388 ms`; the 100-track Library loaded at `1,441 ms`.
- DDJ-FLX4 connected at `1,938 ms`; profile activation completed at
  `1,948 ms`.
- FLX4 identity `2B73:0045`, built-in profile, MIDI IN, MIDI OUT and UAC were
  active.
- Storage/controller disconnects, controller fault-recovery epochs, daemon
  errors, recovery failures/drops, runtime queue failures and service-log drops
  were all zero.
- Before playback, PCM underruns, output-late warnings, UAC drops/overflow,
  packet failures and session data-loss flags were all zero/clear.
- Current boot trace reported no TWDT ISR event. The exposed crash dump retained
  the same historical `RC2-114` `esp_timer` record and did not correlate with
  this cycle.

The operator loaded one track per deck, started both decks and confirmed audible
PCM5102A MAIN plus FLX4 headphone cue. Across two snapshots 10 seconds apart,
both decks remained `PLAYING`, 1,739 UAC blocks were submitted, the ring remained
`nominal` (`1,161 -> 1,103` queued frames), and underflow, drop, overflow,
packet-loss, PCM-underrun, output-late, controller-disconnect, daemon-error and
runtime-queue-failure deltas were all zero. The current TWDT ISR flag remained
clear.

The first deck-2 selection logged one `AUDIO_LOAD_FAILED` / `NOT FOUND` at
`120,712 ms`. A different track loaded successfully at `133,473 ms` and both
decks then played. Keep this as a separate media/library observation; it did not
cause a USB detach, reset or failure of the subsequent dual-deck playback.

Despite the stable window, `data_loss=true` and `data_loss_flags=16` were
latched at both samples. The lifetime underflow counter was `4,762,127` frames
at the second sample but did not increase during the measured window. Because
the same first-playback flag occurred after both cold boots A1 and A2, classify
this as a reproducible startup transition defect rather than a one-off
observation. Pause A3/A4 and analyze the UAC stream-start/ring-prime ordering
before continuing the lifecycle matrix.

Read-only source analysis found that the FLX4 isochronous queue begins consuming
the UAC ring immediately after controller enumeration and intentionally
zero-fills empty reads while playback is idle. The health monitor ignores idle
deltas and the first active sample, but it considers playback active before the
ring has completed its first prime. A remaining startup empty-read delta can
therefore be latched on the next five-second health sample even when the ring is
already nominal and sustained playback has no loss. The narrow remediation is
to establish the session underflow baseline only after the first active ring
prime, while retaining low-ring pressure reporting and all post-prime loss
detection. This requires a focused host regression before another hardware
image is installed; no firmware change was made during this diagnostic cycle.

## Local remediation after A2

The health monitor now carries one playback-start underflow grace interval. It
suppresses only the transition underflow delta, and only if the follow-up sample
shows that the UAC ring recovered to `nominal` or `high`. A ring that remains
`low`/`unavailable` still reports pressure plus underflow immediately; any later
post-prime underflow, packet loss, drop or overflow remains a latched active
data-loss condition.

Validation completed before hardware installation:

- full `tests/run_p4_host_tests.ps1`: PASS, exit code `0`, including focused
  `audio_uac_health` startup-recovery, sustained-low-ring and post-prime-loss
  coverage;
- P4 firmware build: PASS with ESP-IDF `v6.0.2`;
- dirty development image: `RC2-117-g15329cc-dirty`, size `2,452,752` bytes,
  SHA-256 `cc04dfd8637a2a6899af16bfb6af92ffa495446124fbfdcb67de2cc0073fc471`;
- binary budget: PASS, `1,217,264` bytes remaining;
- `dependencies.lock`: unchanged.

The dirty image is build evidence only. Commit and package the exact resulting
revision before installation, then repeat A1 and A2 from cold boot. Do not resume
A3 until both retests keep `data_loss_flags=0` during the first playback session.

## Remediation exact-image retest

### Installation and OTA reboot

- Firmware commit: `6c7a0f69880928184da0d660037a57a6dd344c33`.
- Installed version/slot: `RC2-118-g6c7a0f6`, `ota_0`.
- Signed bundle SHA-256:
  `19fb02de5d19902e078bcdbfdb88e20ea9ac24a65b28de461851f935c262f2ac`.
- Upload returned `ok=true`, `rebooting=true`; boot 393 reported expected `SW`
  reset, OTA returned to `idle` with an empty error, and USB0/USB1 recovered.

### R-A1 — first remediation cold boot with both devices attached

Status: **PASS**.

- Boot epoch advanced `393 -> 394` with expected `POWERON` reset reason.
- USB0 mounted at `1,401 ms`; the 100-track Library loaded at `1,456 ms`.
- DDJ-FLX4 connected at `1,934 ms`; profile activation completed at
  `1,941 ms`.
- Before playback, controller disconnects, daemon/recovery/queue failures,
  UAC drop/overflow/packet failures, PCM underruns, output-late count, active
  data-loss flags and current TWDT ISR flag were all zero/clear.
- The operator loaded one track per deck, played both and confirmed audible
  MAIN plus FLX4 cue.
- Across two active snapshots 10 seconds apart, both decks remained `PLAYING`,
  1,737 UAC blocks were submitted, the ring remained `nominal`
  (`1,126 -> 1,261` frames), and underflow/drop/overflow/packet/PCM/late,
  daemon-error and runtime-queue-failure deltas were all zero.
- `data_loss=false` and `data_loss_flags=0` at both active samples: the original
  first-playback false positive did not recur.

The service log also contained two FLX4 disconnect/reconnect actions at about
128 s and 155 s. The operator confirmed these were intentional manual unplug
and replug actions during the test, so they are excluded from unexpected-fault
classification. USB0 remained mounted, playback continued, FLX4 recovered and
the UAC active data-loss flag stayed clear.

### R-A2 — second remediation cold boot with both devices attached

Status: **PASS**.

- The accepted cycle ran on boot 396 with expected `POWERON` reset reason and
  exact image `RC2-118-g6c7a0f6` from `ota_0`. An additional `POWERON` boot 395
  was present in the retained log but was not used as the controlled R-A2
  sample.
- USB0 mounted at `1,408 ms`; the 100-track Library loaded at `1,461 ms`.
- DDJ-FLX4 connected at `1,933 ms`; profile activation completed at
  `1,940 ms` with MIDI IN/OUT and USB Audio active.
- The initial controller check showed one successful host recovery request,
  with zero daemon errors, recovery failures, probe drops, runtime queue
  failures, controller disconnects, storage disconnects and service-log drops.
- Browse rotation initially appeared inactive while the empty Overview screen
  owned the encoder as waveform zoom. A controlled input check proved the USB
  path live: MIDI packets advanced `8,971 -> 8,991`, with matching semantic
  events. Pressing browse opened Library, after which track selection worked.
- The operator loaded one track per deck, played both and confirmed audible
  output. Across two active snapshots 10 seconds apart, both decks remained
  `PLAYING` and each position advanced by `10,101 ms`.
- UAC submitted blocks advanced by 1,740 and the ring remained `nominal`
  (`1,229 -> 1,253` queued frames). Underflow, drop, overflow, packet-failure,
  packet-lost, PCM-underrun and output-late deltas were all zero.
- Controller/storage disconnect, daemon-error, recovery-failure,
  runtime-queue-failure and service-log-drop deltas were all zero.
- `data_loss=false`, `data_loss_flags=0` and current TWDT ISR flag false at
  both active samples. The first-playback false positive did not recur.

R-A1 and R-A2 therefore satisfy the remediation stop gate. Continue with A3
and A4 on the same exact image; do not reinterpret the historical original A2
failure as a pass.

### A3 — third accepted cold boot with both devices attached

Status: **PASS**.

- Boot 397 reported the expected `POWERON` reset reason on exact image
  `RC2-118-g6c7a0f6` from `ota_0`.
- USB0 mounted at `1,388 ms`; the 100-track Library loaded at `1,439 ms`.
- DDJ-FLX4 connected at `1,933 ms`; profile activation completed at
  `1,940 ms` with MIDI IN/OUT and USB Audio active.
- Before playback, controller/storage disconnects, daemon/recovery/queue
  failures, UAC drop/overflow/packet failures, PCM underruns, output-late
  count, active data-loss flags and current TWDT ISR flag were all zero/clear.
- The operator loaded one track per deck, played both and confirmed audible
  output.
- Across two active snapshots 10 seconds apart, both decks remained `PLAYING`
  and each position advanced by `10,072 ms`.
- UAC submitted blocks advanced by 1,735 and the ring remained `nominal`
  (`1,016 -> 992` queued frames). Underflow, drop, overflow, packet-failure,
  packet-lost, PCM-underrun and output-late deltas were all zero.
- Controller/storage disconnect, daemon-error, recovery-failure,
  runtime-queue-failure and service-log-drop deltas were all zero.
- `data_loss=false`, `data_loss_flags=0` and current TWDT ISR flag false at
  both active samples.

### A4 — fourth accepted cold boot with both devices attached

Status: **PASS**.

- Boot 398 reported the expected `POWERON` reset reason on exact image
  `RC2-118-g6c7a0f6` from `ota_0`.
- USB0 mounted at `1,519 ms`; the 100-track Library loaded at `1,572 ms`.
- DDJ-FLX4 connected at `1,931 ms`; profile activation completed at
  `1,939 ms` with MIDI IN/OUT and USB Audio active.
- Before playback, controller/storage disconnects, daemon/recovery/queue
  failures, UAC drop/overflow/packet failures, PCM underruns, output-late
  count, active data-loss flags and current TWDT ISR flag were all zero/clear.
- The operator loaded one track per deck, played both and confirmed audible
  output.
- Across two active snapshots 10 seconds apart, both decks remained `PLAYING`
  and each position advanced by `10,077 ms`.
- UAC submitted blocks advanced by 1,735 and the ring remained `nominal`
  (`897 -> 1,224` queued frames). Underflow, drop, overflow, packet-failure,
  packet-lost, PCM-underrun and output-late deltas were all zero.
- Controller/storage disconnect, daemon-error, recovery-failure,
  runtime-queue-failure and service-log-drop deltas were all zero.
- `data_loss=false`, `data_loss_flags=0` and current TWDT ISR flag false at
  both active samples.

Group A is complete on the remediated exact image: four accepted cold boots
with USB0 and USB1 continuously attached all reached dual-deck playback with
clear active audio-loss flags. Proceed to Group B warm/software reboot cycles.

### B1 — external warm reset with both devices attached

Status: **PASS**.

- The P4 common supply and both downstream USB connections remained on and
  continuously attached. The operator briefly pressed the board `RST/RESET`
  button; no power-cycle or USB reinsert was used.
- Boot 400 retained exact image `RC2-118-g6c7a0f6` from `ota_0`. This board's
  external reset line is reported by `esp_reset_reason()` as `POWERON`, so the
  operator-observed continuous-power procedure is recorded alongside the raw
  reset reason. This is not evidence of an `esp_restart()` cycle.
- The startup root-port reconciliation connected FLX4 at `1,415 ms`, produced
  one bounded controller disconnect at `1,549 ms`, mounted USB0 at `1,697 ms`,
  loaded the 100-track Library at `1,753 ms`, and reconnected/reactivated FLX4
  by `2,018 ms` without manual intervention.
- Recovery ended with both roots active, one recovery request/success, and zero
  daemon errors, recovery failures, runtime queue failures, storage disconnects
  or service-log drops.
- The operator loaded one track per deck, played both and confirmed audible
  output.
- Across two active snapshots 10 seconds apart, both decks remained `PLAYING`
  and each position advanced by `10,095 ms`.
- UAC submitted blocks advanced by 1,739 and the ring remained `nominal`
  (`969 -> 1,088` queued frames). Underflow, drop, overflow, packet-failure,
  packet-lost, PCM-underrun and output-late deltas were all zero.
- Controller/storage disconnect, daemon-error, recovery-failure,
  runtime-queue-failure and service-log-drop deltas were all zero during the
  active measurement; `data_loss=false`, `data_loss_flags=0` and current TWDT
  ISR flag false at both samples.

### B2 — second external warm reset with both devices attached

Status: **INITIAL FAIL — active UAC underflow; remediated and passed below**.

- Boot 401 retained exact image `RC2-118-g6c7a0f6` from `ota_0`; the board
  again reported raw reset reason `POWERON` for the operator-performed external
  reset with the common supply and both USB devices continuously connected.
- Startup recovery was bounded and automatic: FLX4 connected at `1,415 ms`,
  the expected root-port cycle disconnected it at `1,526 ms`, USB0 mounted at
  `1,698 ms`, the 100-track Library loaded at `1,753 ms`, and FLX4 reactivated
  at `1,996 ms`.
- Initial recovery counters were otherwise clean: one recovery request/success,
  with zero daemon errors, recovery failures/drops, runtime queue failures,
  storage disconnects, active UAC flags, PCM underruns, output-late events and
  current TWDT ISR indication.
- Deck 1 track load completed at `39,574 ms`. A first Deck 2 selection completed
  Library metadata work at `40,465 ms` but audio open failed immediately with
  `ESP_ERR_NOT_FOUND`; a replacement Deck 2 load completed at `60,131 ms`.
- At `55,491 ms`, before the successful replacement Deck 2 load, the service
  journal emitted `UAC_DATA_LOSS` with 23,673 underflow frames and a recovered
  ring depth of 1,256 frames. This is an active-session counter delta, not only
  a stale lifetime value.
- During the later two-snapshot window, both decks remained `PLAYING` and
  advanced by `10,077/10,078 ms`; UAC submitted blocks advanced by 1,736, the
  ring remained `nominal` (`1,176 -> 1,050` frames), and all measured fault
  deltas were zero. Nevertheless, `data_loss=true` and `data_loss_flags=16`
  remained latched at both samples, so the release criterion is not met.

Do not continue to B3 until a focused reproduction separates single-deck UAC
startup from the concurrent failed Deck 2 load and the active underflow cause is
remediated or otherwise dispositioned with evidence.

#### B2 focused diagnosis

The original B2 health event is now classified as a monitor/session-boundary
false positive rather than demonstrated post-PLAY UAC starvation:

- After stopping both decks, the session-scoped flag cleared normally. The raw
  UAC underflow counter continued to rise while idle, as expected because the
  isochronous consumer continuously zero-fills an empty ring.
- A D1-only restart ran for 25 seconds with 4,697 submitted blocks, nominal ring
  depth and zero post-PLAY raw underflow, PCM underrun, output-late or active
  data-loss flags.
- Repeating the known `ESP_ERR_NOT_FOUND` Deck 2 load while D1 was already
  stable left the raw underflow delta and active flags at zero for 15 seconds.
- A controlled `LOAD1 -> immediately PLAY1` transition captured 6,703 frames
  accrued between the last idle sample and the first PLAY sample, followed by
  zero post-PLAY underflow for 25 seconds; the startup grace handled this case.
- A precise `good LOAD1 -> failed LOAD2 -> immediately PLAY1` reproduction
  captured 7,232 idle-to-PLAY frames, zero post-PLAY underflow for 30 seconds,
  nominal ring depth and clear flags.
- Five automated 18-second repetitions used different known-good D1 tracks and
  the same failing Deck 2 track. All five had 6,350--7,056 idle-to-PLAY frames,
  zero post-PLAY raw underflow, nominal ring depth and zero PCM/output-late
  faults. Nevertheless, health flag 16 appeared in repetitions 1 and 4.

The false flags depended on the phase of the five-second health timer. A short
STOP/PLAY interval can occur entirely between two health callbacks, leaving
`last_playback_active=true`; the next callback then attributes raw underflow
accumulated while idle to the new active session. The monitor therefore needs
an audio-engine-owned playback-session epoch (or equivalent exact transition
signal) instead of inferring session boundaries solely from periodic boolean
samples. B2 remains a release-gate failure until that diagnostic defect is
fixed and the exact-image hardware retest passes.

#### B2 remediation implementation

Status: **PASS — implemented, exact-commit OTA installed and hardware-retested**.

- The audio engine now owns a monotonically advancing playback-session epoch.
  It advances only when `PLAY` changes the overall dual-deck engine from all
  idle to active; starting the second deck while the first is already active
  remains part of the same session.
- The diagnostics snapshot publishes the epoch atomically with the sampled
  deck-active state under the audio-engine lock. The five-second UAC health
  monitor uses an epoch change as an exact new-session boundary even when both
  STOP and PLAY occurred between callbacks.
- A new host regression reproduces the missed-idle case directly: two
  consecutive monitor samples are both active, the epoch changes between them,
  and 7,000 idle underflow frames accrue. The new session clears the old
  latched loss and establishes a fresh baseline, while a later genuine
  post-prime underflow is still reported and latched.
- Audio-engine host coverage verifies epoch zero after initialization, epoch
  one on the first all-idle-to-active transition, no change when the second deck
  joins active playback, and epoch two after both decks pause and playback
  restarts.
- `tests/run_p4_host_tests.ps1` passed in full on 2026-09-12, including all
  static gates, 408/408 audio-engine assertions and the focused
  `audio_uac_health` suite.
- A firmware build completed with ESP-IDF v6.0.2. The uncommitted development
  image identified itself as `RC2-120-g92e87d9-dirty`; `main-deck-p4.bin` was
  2,452,880 bytes with 1,217,136 bytes free inside the configured binary budget
  and SHA-256
  `dc9634c82713cf81fa1c6394e047b867ea624753570dace18dbe045700a46444`.

The dirty development version is retained only as build-development evidence;
it is not release evidence.

#### B2 exact-image OTA and hardware retest

- Commit `7b7b29a40a8ff154c22a2d5556a81ffdb3993495` was pushed and its remote SHA
  verified before packaging. Both normal and isolated `build_signed` targets
  built as `RC2-121-g7b7b29a` with ESP-IDF v6.0.2.
- The signed `rel-001` bundle verified before upload. Its application image was
  2,452,928 bytes with SHA-256
  `f3c55750ca4c555d4eb854597cc19919eba1b6320273311eba7ff4c6bb46c015`.
- Push OTA from `RC2-118-g6c7a0f6`/`ota_0` returned HTTP 200 and rebooted into
  the exact expected image on `ota_1`; `/api/firmware` reported transfer state
  `idle` and an empty `last_error`.
- Boot 402 reported `SW` reset. With both roots continuously occupied, startup
  connected FLX4 at 1,348 ms, performed the expected bounded root cycle,
  mounted USB0 at 1,631 ms, loaded all 100 tracks at 1,686 ms and reactivated
  the FLX4 profile at 1,943 ms. Recovery was 1/1 with zero daemon errors,
  recovery failures, runtime queue failures or service-log drops.
- The first automation pass completed two clean reproduction cycles. Its third
  load request intentionally stopped on HTTP 409 because the test polled the
  old `READY` presentation before the asynchronous loader had claimed its next
  session. The service journal records this as one
  `WEB_LOAD_REQUEST_FAILED`; it caused no USB, audio or device fault. The
  corrected harness waits for the requested title and lifecycle completion.
- Five accepted 18-second reproductions used good D1 keys 3, 10, 13, 15 and 5
  followed by the known missing-audio D2 key 8 and immediate D1 PLAY:

| D1 key | Idle-to-PLAY underflow | Post-PLAY underflow | Submitted blocks | Final ring | UAC flags | PCM 1/2 | Late |
| ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 3 | 1,411 | 0 | 3,110 | 1,237 nominal | 0 | 0/0 | 0 |
| 10 | 1,411 | 0 | 3,107 | 870 nominal | 0 | 0/0 | 0 |
| 13 | 1,411 | 0 | 3,108 | 953 nominal | 0 | 0/0 | 0 |
| 15 | 1,235 | 0 | 3,107 | 1,239 nominal | 0 | 0/0 | 0 |
| 5 | 1,235 | 0 | 3,105 | 1,020 nominal | 0 | 0/0 | 0 |

- The final idle snapshot retained both devices and both powered roots, with
  controller profile active, USB0 mounted, UAC session flags clear, PCM
  underruns 0/0, output-late 0 and current TWDT ISR false. Across the complete
  boot 402 service journal there were zero `UAC_DATA_LOSS`, `AUDIO_UNDERRUN`,
  `AUDIO_OUTPUT_LATE` or USB-unmount events.
- The operator confirmed that the test playback was audible on the physical
  MAIN output after the exact-image OTA; this closes the listening check rather
  than inferring sound solely from advancing transport and UAC counters.
- The one controller disconnect in boot 402 is the expected bounded startup
  root-port cycle between the initial 1,348 ms connection and the successful
  1,936 ms reconnect, not a runtime disconnect.

B2 is accepted after remediation. The known key-8 `ESP_ERR_NOT_FOUND` remains
a separate media/catalog consistency defect; this test confirms that it no
longer contaminates the new playback session's UAC health evidence. Proceed to
B3 on the same exact image.

### B3 — third external warm reset with both devices attached

Status: **PASS**.

- The operator pressed the board `RST/RESET` button while USB0, USB1 and the
  common supply remained continuously connected. Boot 403 retained exact image
  `RC2-121-g7b7b29a` on `ota_1`; this board again reported the external reset
  line as raw reason `POWERON`.
- Startup recovery was bounded and automatic: FLX4 connected at 1,478 ms, the
  expected root-port cycle disconnected it at 1,610 ms, USB0 mounted at
  1,760 ms, all 100 Library tracks loaded at 1,818 ms, and FLX4 reconnected and
  activated by 2,131 ms.
- The startup snapshot had both roots powered, recovery 1 request / 1 success,
  and zero daemon errors, recovery failures, runtime queue failures,
  service-log drops, PCM underruns, output-late events, active UAC flags or
  current TWDT ISR indication.
- Known-good tracks key 3 (Pearl Jam) and key 10 (Anna Nalick) loaded on D1/D2.
  Both decks remained `PLAYING` across an active ten-second window and each
  position advanced by 10,042 ms.
- UAC submitted blocks advanced by 1,730; its ring remained nominal
  (`1,058 -> 1,148` frames). Underflow, dropped-block, overflow,
  packet-failure and packet-lost deltas were all zero. PCM 1/2, output-late,
  controller/storage disconnect, daemon-error, recovery-failure,
  runtime-queue-failure and service-log-drop deltas were also zero; active UAC
  data-loss flags remained clear.
- The operator confirmed audible physical output, then exercised D1
  `PLAY/PAUSE` twice on the FLX4 and confirmed that the physical controller
  stopped and resumed playback correctly. The final snapshot recorded 342 MIDI
  packets, 344 semantic events, active MIDI output acceptance and no runtime
  controller disconnect.
- Both decks were stopped cleanly after the test. USB0 remained mounted, FLX4
  remained active and the complete boot-403 journal contained zero
  `UAC_DATA_LOSS`, `AUDIO_UNDERRUN`, `AUDIO_OUTPUT_LATE` or USB-unmount events.

B3 is accepted. Proceed to B4 on the same exact image with both USB devices
continuously attached.

### B4 — fourth external warm reset with both devices attached

Status: **PASS**.

- The operator pressed `RST/RESET` with the common supply, USB0 and USB1
  continuously connected. Boot 404 retained exact image
  `RC2-121-g7b7b29a` on `ota_1` and reported raw reason `POWERON`, consistent
  with the previous external-reset observations on this board.
- Startup recovery was bounded and automatic: FLX4 connected at 1,574 ms, the
  expected root-port cycle disconnected it at 1,693 ms, USB0 mounted at
  1,856 ms, all 100 Library tracks loaded at 1,908 ms, and FLX4 reconnected and
  activated by 2,162 ms.
- The startup snapshot had both roots powered, recovery 1/1, and zero daemon
  errors, recovery failures, runtime queue failures, service-log drops, PCM
  underruns, output-late events, active UAC flags or current TWDT indication.
- Known-good tracks key 5 (Heart) and key 13 (Kate Bush) loaded on D1/D2. Both
  decks remained `PLAYING` across the active measurement and advanced by
  10,065/10,066 ms.
- UAC submitted blocks advanced by 1,734 and the ring remained nominal
  (`941 -> 1,164` frames). Underflow, dropped-block, overflow, packet-failure,
  packet-lost, PCM 1/2, output-late, controller/storage disconnect,
  daemon-error, recovery-failure, runtime-queue-failure and service-log-drop
  deltas were all zero; active UAC flags stayed clear.
- The operator confirmed audible physical output and pressed D2 `PLAY/PAUSE`
  twice on the FLX4. The final pre-stop snapshot showed both decks playing,
  confirming physical stop/resume. Controller diagnostics recorded four MIDI
  packets and seven semantic events, retained active MIDI-output acceptance and
  had no runtime disconnect.
- Both decks were stopped cleanly after the test. USB0 remained mounted, FLX4
  remained active, and the complete boot-404 journal contained zero
  `UAC_DATA_LOSS`, `AUDIO_UNDERRUN`, `AUDIO_OUTPUT_LATE` or USB-unmount events.

B4 and Group B are accepted. Groups A and B account for 8 of the planned 50
controlled lifecycle cycles. Proceed to Group C: boot empty, then attach USB0
followed by USB1, on the same exact firmware image.

### C1 — empty boot, USB0 then USB1

Status: **PASS**.

- The operator disconnected both downstream devices, reset the powered P4 and
  began from empty boot 405 on exact image `RC2-121-g7b7b29a`, `ota_1`.
- The guided harness first accepted USB0 alone: storage mounted, the Library
  contained all 100 tracks and FLX4 remained absent. FLX4 was then attached to
  USB1 and reached active profile, MIDI IN/OUT, MIDI-output acceptance and UAC
  while USB0 remained mounted.
- Known-good keys 3 and 10 loaded on D1/D2. Across the measured playback window
  both deck positions advanced by 10,118 ms, UAC submitted 1,743 blocks and
  the ring finished nominal with 1,073 queued frames.
- UAC active data-loss flags remained zero. PCM 1/2, output-late,
  storage/controller disconnect, topology/probe/allocation, USB daemon,
  recovery-failure/drop, runtime-queue and service-log-drop deltas were all
  zero. The single host recovery request completed successfully.
- The operator confirmed audible MAIN and FLX4 cue output, normal LEDs and no
  latched controls. Two physical D1 PLAY/PAUSE presses produced four MIDI
  packets and four semantic events and returned both decks to playback.
- Cleanup stopped both decks with both devices healthy. Boot 405 remained
  current and its journal contained zero `UAC_DATA_LOSS`, `AUDIO_UNDERRUN`,
  `AUDIO_OUTPUT_LATE` or `USB_UNMOUNTED` events.

C1 accounts for two controlled physical attachment actions and raises matrix
progress to 9/50 cycles. Repeat the same empty-boot USB0-then-USB1 sequence for
C2.

### C2 — empty boot, USB0 then USB1

Status: **PASS with bounded-recovery observation**.

- Empty boot 406 retained exact image `RC2-121-g7b7b29a`, `ota_1`. USB0 was
  attached alone and mounted with all 100 Library tracks before FLX4 was
  attached and reached its active MIDI IN/OUT, LED-output and UAC state.
- USB0 diagnostics recorded two accepted connect/mount attempts and one mount
  success. One host recovery request completed successfully, after which the
  mount and Library remained stable; there was no storage disconnect, release,
  final mount error or `USB_UNMOUNTED` event. This is accepted as the required
  single bounded recovery, not as an unexplained successful first attempt.
- During the measured dual-deck window D1/D2 advanced by 10,119/10,118 ms,
  UAC submitted 1,743 blocks and its ring finished nominal with 1,100 queued
  frames. Active data-loss flags remained zero.
- All full-cycle fault deltas were zero: PCM 1/2, output-late,
  storage/controller disconnect, topology/probe/allocation, controller fault
  recovery, USB daemon, recovery failure/drop, runtime queue and service-log
  drop. The boot journal had no UAC-loss, audio-underrun, output-late or USB
  unmount events and boot 406 remained current.
- The operator confirmed audible MAIN and FLX4 cue, normal LEDs and no latched
  controls. Two physical D1 PLAY/PAUSE presses produced four MIDI packets and
  four semantic events and returned both decks to playback before automated
  cleanup stopped them.

C1 and C2 account for four controlled attachment actions. Matrix progress is
now 10/50 cycles; continue with C3 on the same exact image.

### C3 — empty boot, USB0 then USB1

Status: **PASS**.

- Empty boot 407 retained exact image `RC2-121-g7b7b29a`, `ota_1`. USB0 alone
  produced one connect event and one successful mount attempt with all 100
  Library tracks. FLX4 then activated on USB1 while storage remained mounted.
- The required host recovery was exactly one request and one success, with no
  recovery failure/drop, USB daemon error, topology/probe/allocation failure,
  controller fault-recovery epoch, storage/controller disconnect, runtime
  queue failure or service-log drop.
- D1 and D2 each advanced by 10,118 ms during the measured playback window.
  UAC submitted 1,743 blocks and finished nominal with 1,154 queued frames;
  active UAC flags, PCM 1/2 and output-late deltas stayed zero.
- The operator confirmed MAIN and FLX4 cue audio, LEDs, control state and the
  physical D1 double PLAY/PAUSE action. It produced four MIDI packets and four
  semantic events and returned both decks to playback before cleanup.
- Both devices remained healthy, both decks stopped cleanly, boot 407 did not
  change and the boot journal contained none of the four gated fault events.

Group C is 3/4 complete and matrix progress is 11/50 cycles. Repeat once more
as C4 before reversing the attachment order for Group D.

### C4 — empty boot, USB0 then USB1

Status: **PASS; Group C complete**.

- Empty boot 408 retained `RC2-121-g7b7b29a`, `ota_1`. USB0 produced one
  connect and one successful mount attempt with all 100 Library tracks; FLX4
  subsequently reached its complete active state without disturbing storage.
- Host recovery completed as exactly one request and one success. Every gated
  full-cycle fault delta and boot-journal event count remained zero.
- D1/D2 both advanced by 10,153 ms. UAC submitted 1,749 blocks, finished
  nominal with 1,114 queued frames and retained zero active data-loss flags,
  PCM underruns or output-late events.
- The operator confirmed MAIN/cue audio, LEDs, controls and two physical D1
  PLAY/PAUSE presses; four MIDI packets and four semantic events were observed.
  Automated cleanup stopped both decks with both USB devices healthy and no
  intervening reboot.

All four Group C cycles passed on the exact image. They contribute eight
controlled physical attachment actions and bring matrix progress to 12/50
cycles. Proceed to Group D: boot empty, attach USB1 first, then USB0.

### D1 — empty boot, USB1 then USB0

Status: **PASS after correcting a harness-only recovery-count assumption**.

- Empty boot 409 retained exact image `RC2-121-g7b7b29a`, `ota_1`. FLX4 first
  reached active profile, MIDI IN/OUT, MIDI-output acceptance and UAC with USB0
  absent. USB0 was then attached, mounted on its first attempt and exposed all
  100 Library tracks while FLX4 remained active.
- The reverse order correctly produced two sequential host recovery requests
  and two successes: one after FLX4 activation and one after USB0 attachment.
  There were no recovery failures/drops, USB daemon errors,
  topology/probe/allocation faults, controller fault-recovery epochs,
  storage/controller disconnects, runtime queue failures or service-log drops.
- D1/D2 advanced by 10,112/10,113 ms, UAC submitted 1,742 blocks and finished
  nominal with 1,119 queued frames. UAC flags, PCM 1/2 and output-late deltas
  remained zero.
- The operator confirmed MAIN/cue audio, LEDs, normal control state and the
  physical D1 double PLAY/PAUSE action. It produced four MIDI packets and four
  semantic events. Final cleanup left both devices healthy on unchanged boot
  409, with none of the four gated journal events.
- The first harness verdict said `FAIL` solely because its newly added check
  still expected Group C's one total recovery. Offline re-evaluation confirmed
  that this was the only failure entry and that every hardware criterion was
  clean. The harness now accepts the bounded one-to-two range, requires
  matching successes and rejects more than two total requests across the two
  staged attachments; the physical D1 cycle does not need to be repeated.

D1 contributes two attachment actions and brings matrix progress to 13/50
cycles. Continue with D2 on the same exact image.

### D2 — empty boot, USB1 then USB0

Status: **PASS**.

- Empty boot 410 retained exact image `RC2-121-g7b7b29a`, `ota_1`. FLX4
  activated completely while alone, then USB0 mounted on its single attempt
  and exposed all 100 tracks without disturbing the controller.
- The corrected Group D gate directly accepted the expected two recovery
  requests and two matching successes. All recovery-failure/drop, daemon,
  topology, controller, storage, queue and service-log fault deltas were zero.
- Both decks advanced by 10,130 ms, UAC submitted 1,745 blocks and finished
  nominal with 1,225 queued frames. UAC flags, PCM 1/2 and output-late deltas
  remained zero.
- MAIN/cue audio, LEDs and unlatched control state were operator-confirmed.
  Two D1 PLAY/PAUSE presses produced four MIDI packets and four semantic
  events. Both devices remained healthy, cleanup stopped the decks, boot 410
  remained current and the journal had none of the gated fault events.

Group D is 2/4 complete and matrix progress is 14/50 cycles. Continue with D3.

### D3 — empty boot, USB1 then USB0

Status: **PASS after correcting a harness-only exact-count assumption**.

- Empty boot 411 retained exact image `RC2-121-g7b7b29a`, `ota_1`. FLX4
  activation required one successful bounded recovery. USB0 then mounted
  directly on its first attempt with all 100 Library tracks, so the second
  staged topology change did not require another root reconciliation.
- The cycle therefore recorded one request and one success, with zero recovery
  failure/drop and zero daemon, topology, controller, storage, runtime-queue,
  service-log, PCM or output-late fault deltas.
- D1 and D2 each advanced by 10,118 ms. UAC submitted 1,743 blocks, finished
  nominal with 1,076 queued frames and retained clear active data-loss flags.
- The operator confirmed MAIN/cue audio, LEDs, controls and the physical D1
  double PLAY/PAUSE action, which produced four MIDI packets and four semantic
  events. Both devices remained healthy, cleanup stopped both decks, boot 411
  did not change and its journal contained none of the four gated events.
- The initial `FAIL` was solely the harness assumption that Group D must always
  total exactly two recoveries. Offline checks confirmed that it was the only
  failure entry. The revised 1--2 bounded range covers both valid observed
  paths while still rejecting missing successes, more than two requests and
  all existing fault counters; self-tests pass in both supported shells.

Group D is 3/4 complete and matrix progress is 15/50 cycles. Continue with D4.

### D4 — empty boot, USB1 then USB0

Status: **PASS on controlled repeat; Group D complete**.

- The first boot-412 attempt was operator-invalidated before USB enumeration:
  FLX4 was accidentally unplugged, so the host recorded zero topology
  observations, probes, VID/PID or connect events and the harness timed out at
  the first attachment gate. Live status after reconnect confirmed the correct
  FLX4 identity and full profile/MIDI/UAC activation with no host fault. This
  attempt is retained as operator-interrupted evidence and is not a D4 cycle.
- The controlled repeat began from empty boot 413 on exact image
  `RC2-121-g7b7b29a`, `ota_1`. FLX4 activated alone; USB0 was then attached and
  mounted 1/1 with all 100 Library tracks while the controller remained active.
- Recovery completed with two requests and two successes. Every full-cycle
  fault delta and gated boot-journal event count remained zero.
- D1/D2 each advanced by 10,112 ms, UAC submitted 1,742 blocks and finished
  nominal with 995 queued frames and clear active loss flags.
- The operator confirmed MAIN/cue audio, LEDs, controls and the physical D1
  double PLAY/PAUSE check, producing four MIDI packets and four semantic
  events. Cleanup stopped both decks with both USB devices healthy and boot
  413 unchanged.

All four Group D cycles passed on the exact image. Groups C and D together add
16 controlled attachment actions; total matrix progress is 16/50 cycles.
Proceed to Group E: remove and reinsert USB0 while idle and keep FLX4 active.

### E1 — idle USB0 remove/reinsert with FLX4 active

Status: **PASS after correcting a harness-only time-dependent recovery cap**.

- E1 began on boot 413 with both devices healthy, all 100 Library tracks
  present and both decks stopped. Removing USB0 produced exactly one accepted
  storage disconnect, one release and one `USB_UNMOUNTED` event. Library count
  became zero while FLX4 retained its active profile, MIDI IN/OUT, LED-output
  acceptance and UAC with zero controller disconnects.
- Reinserting the same medium produced one connect, one successful mount
  attempt and all 100 Library tracks. The host performed eight storage-owner
  recovery probes during the operator-timed absent interval; all eight
  completed successfully with zero failure/drop, daemon error or controller
  fault. Source review confirmed the deliberate eight-cycle 900 ms fast
  cadence followed by a 30-second slow cadence while no storage session exists.
- After remount, D1/D2 advanced by 10,280/10,281 ms, UAC submitted 1,771 blocks
  and finished nominal with clear active loss flags. PCM, output-late and every
  non-storage fault delta remained zero.
- The operator confirmed MAIN/cue audio, LEDs, controls and physical D1
  PLAY/PAUSE response. Both devices remained healthy, both decks stopped on
  cleanup and boot 413 remained current.
- The original automated `FAIL` was solely the invalid 0--1 recovery-count cap;
  offline re-evaluation confirmed every hardware criterion above. The harness
  now treats E recovery count as timing-dependent while requiring matching
  successes and zero failure/drop, with host self-tests in both PowerShell
  versions. The physical E1 cycle does not need to be repeated.

E1 contributes one remove/reinsert action and brings matrix progress to 17/50
cycles. Continue with E2 without rebooting.

### E2 — idle USB0 remove/reinsert with FLX4 active

Status: **PASS**.

- E2 continued on boot 413 with both devices healthy and both decks stopped.
  USB0 removal again produced one disconnect, one release, one
  `USB_UNMOUNTED` event and Library count zero while FLX4 stayed fully active
  with zero controller disconnects.
- Reinsertion followed the storage owner's alternative bounded retry path:
  eight mount attempts produced one final successful mount and restored all
  100 tracks, without a host root-power recovery request. The final mount
  result was `ESP_OK`; recovery failures/drops, daemon and topology/controller
  fault deltas were all zero.
- D1/D2 each advanced by 10,217 ms. UAC submitted 1,760 blocks and finished
  nominal with 1,033 queued frames and clear loss flags; PCM and output-late
  deltas stayed zero.
- The operator confirmed MAIN/cue audio, LEDs, controls and D1 PLAY/PAUSE
  response. Four MIDI packets and four semantic events were observed. Cleanup
  left both devices healthy and boot 413 unchanged.

Group E is 2/5 complete and matrix progress is 18/50 cycles. Continue with E3
without rebooting.

### E3 — idle USB0 remove/reinsert with FLX4 active

Status: **PASS on controlled repeat after fixing a harness-only Library race**.

- E3 continued on unchanged boot 413 and exact image
  `RC2-121-g7b7b29a`, `ota_1`. The accepted repeat began with both devices
  healthy, all 100 Library tracks present and both decks stopped.
- Removing USB0 produced exactly one storage disconnect, one release and one
  `USB_UNMOUNTED` event. Library count became zero while FLX4 stayed fully
  active; controller disconnects and all controller fault counters remained
  zero.
- Reinsertion recorded two connect observations, seven mount attempts and one
  successful mount, with final result `ESP_OK` and all 100 tracks restored.
  No host recovery was needed during this operator-timed interval. Recovery
  failures/drops, USB daemon errors and topology faults remained zero.
- D1/D2 each advanced by 10,490 ms, UAC submitted 1,807 blocks and finished
  nominal with 1,244 queued frames. Active loss flags, PCM 1/2, output-late,
  runtime-queue and service-log-drop deltas all stayed zero.
- The operator confirmed MAIN/cue audio, LEDs, controls and two physical D1
  PLAY/PAUSE presses. Four MIDI packets and four semantic events were observed.
  Cleanup stopped both decks with both USB devices healthy and no reboot.
- The first E3 attempt reached a successful USB mount, but the harness sampled
  Library count once before the asynchronous `LIBRARY_LOADED` event, which
  followed `USB_MOUNTED` by about 62 ms. That attempt was invalidated as a
  harness timing race, not counted as a device failure, and the repeat used a
  bounded wait for the required 100-track Library state.

Group E is 3/5 complete and matrix progress is 19/50 cycles. Continue with E4
without rebooting.

### E4 — idle USB0 remove/reinsert with FLX4 active

Status: **PASS**.

- E4 continued on boot 413 with both devices healthy, 100 Library tracks and
  both decks stopped. USB0 removal produced exactly one disconnect, one
  release, one `USB_UNMOUNTED` event and Library count zero while FLX4 remained
  fully active with zero controller disconnects.
- Reinserting the same medium mounted successfully on its first attempt and
  restored all 100 tracks. Nine operator-timed host recovery requests all had
  matching successes, with no recovery failure/drop, daemon error, topology
  fault, controller fault or final mount error.
- D1/D2 each advanced by 10,118 ms, UAC submitted 1,743 blocks and finished
  nominal with 1,235 queued frames. Active data-loss flags, PCM 1/2,
  output-late, runtime-queue and service-log-drop deltas remained zero.
- The operator confirmed MAIN/cue audio, LEDs, controls and the physical D1
  PLAY/PAUSE check. Eight MIDI packets and eight semantic events were observed.
  Cleanup stopped both decks with both USB roles healthy and boot 413
  unchanged.

Group E is 4/5 complete and matrix progress is 20/50 cycles. Complete E5 on
the same exact image without rebooting.

### E5 — idle USB0 remove/reinsert with FLX4 active

Status: **PASS; Group E complete**.

- E5 continued on unchanged boot 413 and exact image
  `RC2-121-g7b7b29a`, `ota_1`, with both devices healthy, all 100 Library
  tracks present and both decks stopped.
- USB0 removal produced exactly one disconnect, one release and one
  `USB_UNMOUNTED` event. Library count became zero while FLX4 retained its
  active profile, MIDI IN/OUT, LED-output acceptance and UAC; controller
  disconnects remained zero.
- The same medium remounted successfully on its first attempt and restored all
  100 tracks. Nine operator-timed recovery requests had nine matching
  successes. Recovery failures/drops, daemon errors, topology/controller
  faults and the final mount error all remained zero.
- D1/D2 each advanced by 10,217 ms, UAC submitted 1,760 blocks and finished
  nominal with 1,149 queued frames. Active UAC-loss flags, PCM 1/2,
  output-late, runtime-queue and service-log-drop deltas stayed zero.
- The operator confirmed MAIN/cue audio, LEDs, controls and the physical D1
  double PLAY/PAUSE check. Four MIDI packets and four semantic events were
  observed. Cleanup stopped both decks with both USB devices healthy and no
  reboot.

All five idle USB0 remove/reinsert cycles passed while FLX4 remained active.
Groups A--E account for 21/50 controlled lifecycle cycles and 21 planned
physical attachment/reconnect actions.

### F1--F5 — USB0 removal during deterministic Library load

Status: **PASS; Group F complete**.

- Exact installed image: `RC2-127-g06c0e85`, `ota_0`, boot epoch 415.
- Every cycle reached validation-gate state `holding` after the first bounded
  PDB header read. Removing USB0 changed the state to `media_removed`, produced
  no partial catalog and left Library count zero while FLX4 stayed active.
- Reinserting the same medium restored a coherent 100-track Library in every
  cycle. All recovery requests had matching successes; recovery failures,
  queue drops, daemon errors, topology/controller failures and mount errors
  remained zero.
- Both decks advanced for more than ten seconds in every cycle. PCM 1/2,
  output-late, active UAC-loss, runtime-queue and service-log-drop deltas stayed
  zero, with no reboot.
- The operator confirmed MAIN/cue audio, LEDs and controls after every cycle.
  Each accepted D1 double PLAY/PAUSE check produced four MIDI packets and four
  semantic events.

| Cycle | Library | D1 advance | D2 advance | MIDI / semantic | Recovery | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| F1 | 100 | 10,170 ms | 10,170 ms | 4 / 4 | 18 / 18 | PASS |
| F2 | 100 | 10,118 ms | 10,118 ms | 4 / 4 | 20 / 20 | PASS |
| F3 | 100 | 10,123 ms | 10,124 ms | 4 / 4 | 16 / 16 | PASS |
| F4 | 100 | 10,181 ms | 10,182 ms | 4 / 4 | 16 / 16 | PASS |
| F5 | 100 | 10,245 ms | 10,246 ms | 4 / 4 | 16 / 16 | PASS |

The first F3 attempt completed the USB and audio sequence but the operator
forgot the required D1 double PLAY/PAUSE action. The harness correctly rejected
that attempt; it does not count. F3 was repeated from the beginning and passed.
Groups A--F now account for 26/50 controlled lifecycle cycles and 26 planned
physical attachment/reconnect actions. Proceed to Group G: USB0 removal during
audio load/decode or active playback.

### G1--G5 — USB0 removal after deterministic first audio-cache read

Status: **PASS; Group G complete**.

- Exact installed image: `RC2-128-g495947e`, `ota_1`, boot epoch 416.
- The guarded gate alternated D1 on odd cycles and D2 on even cycles. Every
  selected loader reached `holding` after its first bounded 32 KiB cache read,
  then changed to `media_removed` when USB0 was removed.
- FLX4 profile, MIDI and UAC stayed active while USB0 was absent. Every normal
  reinsert restored a coherent 100-track Library, and all eight recovery
  requests per accepted cycle had matching successes.
- Both decks advanced for more than ten seconds in every cycle. MIDI and
  semantic deltas were 4/4 per cycle; PCM underrun, output-late, UAC-loss,
  runtime-queue, daemon and service-log fault deltas stayed zero with no reboot.

| Cycle | Target | D1 advance | D2 advance | UAC blocks | Recovery | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| G1 | D1 | 10,251 ms | 10,251 ms | 1,766 | 8 / 8 | PASS |
| G2 | D2 | 10,107 ms | 10,107 ms | 1,741 | 8 / 8 | PASS |
| G3 | D1 | 10,124 ms | 10,123 ms | 1,745 | 8 / 8 | PASS |
| G4 | D2 | 10,304 ms | 10,304 ms | 1,774 | 8 / 8 | PASS |
| G5 | D1 | 10,112 ms | 10,112 ms | 1,742 | 8 / 8 | PASS |

The first G4 attempt had an additional rapid USB0 disconnect/remount roughly
0.75 seconds after reinsertion. The strict harness rejected it because the
cycle contained two disconnects/releases/unmount events. It does not count;
the complete G4 sequence was repeated from a stable baseline and passed.
Groups A--G now account for 31/50 controlled lifecycle cycles and 31 planned
physical reconnect actions. Proceed to Group H: USB1 idle disconnect/reconnect
while USB0 remains mounted.

### H1--H5 — USB1 idle disconnect/reconnect with USB0 retained

Status: **PASS; Group H complete**.

- Exact installed image: `RC2-128-g495947e`, `ota_1`; H1--H4 used boot epoch
  416 and H5 used boot epoch 417 after a controlled clean reboot.
- Every accepted cycle observed exactly one controller disconnect and connect,
  zero storage disconnects, a continuously mounted USB0 and a coherent
  100-track Library.
- FLX4 identity/profile, MIDI IN/OUT, LED delivery and UAC returned after each
  reconnect. Both decks then advanced for more than ten seconds and the
  operator confirmed audible MAIN/cue plus physical Deck 1 PLAY/PAUSE.
- Host recovery requests remained matched to successes, controller fault
  recovery stayed within the allowed zero-to-one epoch bound, and accepted
  cycles had zero PCM-underrun, output-late, UAC-loss, runtime-queue, daemon,
  storage or controller fault deltas with no reboot.

| Cycle | Boot | D1 advance | D2 advance | UAC blocks | Controller disconnect/connect | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| H1 | 416 | 10,112 ms | 10,113 ms | 1,742 | 1 / 1 | PASS |
| H2 | 416 | 10,106 ms | 10,107 ms | 1,742 | 1 / 1 | PASS |
| H3 | 416 | 10,228 ms | 10,228 ms | 1,763 | 1 / 1 | PASS |
| H4 | 416 | 10,235 ms | 10,235 ms | 1,762 | 1 / 1 | PASS |
| H5 | 417 | 10,130 ms | 10,129 ms | 1,745 | 1 / 1 | PASS |

Rejected attempts do not count. The first H1 run exposed an overly strict
harness expectation of exactly one soft-fault epoch even though a normal
physical `DEV_GONE` can correctly produce zero; the corrected harness accepts
zero or one and rejects duplicates. One H5 attempt included an unintended cable
movement and another included an unintended USB0 removal. A following launch
correctly refused the dirty output-late baseline. After the controlled reboot,
the complete H5 sequence passed cleanly.

### I/J closure — active playback and lost held-control release

Status: **CLOSED — 3 PASS, 7 explicitly waived**.

- Exact installed image: `RC2-134-g09f7efc`, `ota_0`, boot epoch 420.
- I1 and I2 each retained USB0 and the coherent 100-track Library through an
  active dual-deck FLX4 disconnect/reconnect. Profile, MIDI, LEDs, UAC,
  audible MAIN/cue and both deck positions recovered with zero critical fault
  deltas and exactly one controller disconnect/connect event pair.
- J1 repeated the active reconnect while Deck 1 jog touch was held until after
  disconnect. Jog/scratch did not remain latched, and the same automated and
  operator checks passed.
- The pre-remediation boot 418 I1 attempt is not counted: it exposed the false
  reconnect-prime UAC underflow latch fixed by the stream epoch. Attempts
  disturbed by cable movement, a device fall or loss of power are also invalid.
- By explicit operator decision on 2026-09-14, I3--I5 and J2--J5 are waived,
  are not counted as PASS and will not be resumed. Untested held-control
  variants remain an accepted release-scope limitation.

| Cycle | Boot | D1 advance | D2 advance | UAC blocks | Controller disconnect/connect | Result |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| I1 | 420 | 5,231 ms | 5,231 ms | 901 | 1 / 1 | PASS |
| I2 | 420 | 5,225 ms | 5,225 ms | 900 | 1 / 1 | PASS |
| J1 jog touch | 420 | 5,120 ms | 5,120 ms | 882 | 1 / 1 | PASS |
| I3--I5 | — | — | — | — | — | WAIVED |
| J2--J5 | — | — | — | — | — | WAIVED |

The matrix now accounts for 41/50 PASS cycles, 7/50 waived cycles and 2/50
pending Group L OTA-reboot cycles, with 39 accepted physical
attachment/reconnect actions.

## Continuation checkpoint — 2026-09-14

- Installed hardware is on exact image `RC2-136-g034cd76`, partition `ota_1`;
  signed OTA and focused dual-deck smoke passed with USB0 and FLX4 healthy and
  both decks stopped.
- Groups I/J are closed: I1, I2 and J1 passed; seven remaining cycles are
  explicitly waived. Group K is complete; only two Group L OTA-reboot cycles
  remain.
- The operator-invalidated first D4 attempt and the first E3 harness-race
  attempt do not count toward the 50-cycle total.
- `tools/run_p4_lifecycle_cycle.ps1` supports Groups C--H, all with accepted
  hardware evidence. Local
  JSON/Markdown evidence under ignored `tmp/p4-lifecycle` is not a release
  artifact; this document preserves the accepted results.
- The deterministic Group F trigger, bounded PDB reader and fail-closed rebuild
  are implemented; the complete host suite and ESP-IDF v6.0.2 build pass.
- The guarded Group K software-reboot endpoint and
  `tools/run_p4_lifecycle_k.ps1` K1/K2 harness are implemented. The endpoint
  refuses non-empty requests, active OTA, missing/unhealthy USB roots and
  loading or playing decks. The harness requires an observable API outage, a
  higher boot epoch with reset reason `SW`, unchanged exact version/slot,
  automatic recovery of both roots and the coherent 100-track Library, clean
  post-boot fault counters, dual playback and operator-confirmed MAIN/cue,
  LEDs and controls. Host self-tests and the ESP-IDF v6.0.2 build pass, but no
  K cycle is counted until exact-image hardware evidence passes.

### Group K closure — software reboot with both roots occupied

Status: **PASS — 2/2**.

- Exact installed image: `RC2-136-g034cd76`, `ota_1`.
- K1 advanced boot 423 to 424 and K2 advanced boot 425 to 426; both reported
  reset reason `SW`, retained the exact version and slot, automatically restored
  USB0, FLX4 profile/MIDI/LED/UAC and the coherent 100-track Library, and had no
  harness failures or critical post-boot fault evidence.
- K1 advanced D1/D2 by 10,101/10,101 ms and submitted 1,740 UAC blocks. K2
  advanced D1/D2 by 10,123/10,124 ms and submitted 1,744 UAC blocks. MAIN/cue,
  LEDs, controls and the physical D1 PLAY/PAUSE event were operator-confirmed in
  both accepted cycles.
- The first requested K2 run on boot 425 also passed all technical and operator
  checks, but the shared helper reset its evidence label to K1. It is not counted;
  the harness now preserves the requested cycle before dot-sourcing helpers and
  its host self-test explicitly runs with `Cycle=2`.

| Cycle | Boot transition | Reset | D1 advance | D2 advance | UAC blocks | Result |
| --- | --- | --- | ---: | ---: | ---: | --- |
| K1 | 423 -> 424 | `SW` | 10,101 ms | 10,101 ms | 1,740 | PASS |
| K2 | 425 -> 426 | `SW` | 10,123 ms | 10,124 ms | 1,744 | PASS |

- First action: implement or select a deterministic Group L OTA-reboot harness,
  then run L1/L2 with both roots occupied and no manual reinsert.
