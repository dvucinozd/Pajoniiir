# P4 dual-USB lifecycle/recovery matrix

Opened: **2026-09-11**

Status: **IN PROGRESS — cold-boot Group A and warm/software-reboot Group B
complete after remediation; Group C is next**.

## Exact test image

- Firmware commit: `77d723c8d19b1a859b9f5b4fa8421928250c68d3`
- Installed version: `RC2-116-g77d723c`
- Installed slot: `ota_1`
- USB0 device: Rekordbox storage medium
- USB1 device: Pioneer DDJ-FLX4
- Power prerequisite:
  [`P4_POWER_VBUS_ACCEPTANCE_20260911.md`](P4_POWER_VBUS_ACCEPTANCE_20260911.md)

The documentation-only branch successor does not change the installed binary.
Capture `/api/firmware`, `/api/status` and the diagnostic log before cycle 1
and after every group. Each cycle must record its boot epoch and operator-visible
result.

Current B2 remediation image:

- Firmware commit: `7b7b29a40a8ff154c22a2d5556a81ffdb3993495`
- Installed version/slot: `RC2-121-g7b7b29a`, `ota_1`
- Signed image size/SHA-256: 2,452,928 bytes;
  `f3c55750ca4c555d4eb854597cc19919eba1b6320273311eba7ff4c6bb46c015`
- Installed by signed push OTA with USB0 and USB1 continuously attached; boot
  402 reports `SW` reset.

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

## Planned distribution

| Group | Scenario | Cycles | Physical reconnects | Status |
| --- | --- | ---: | ---: | --- |
| A | Cold boot with USB0 and USB1 already attached | 4 | 0 | PASS after remediation: R-A1, R-A2, A3 and A4 |
| B | Warm/software reboot with both attached | 4 | 0 | PASS: B1, remediated B2, B3 and B4 |
| C | Boot empty, attach USB0 then USB1 | 4 | 8 | pending |
| D | Boot empty, attach USB1 then USB0 | 4 | 8 | pending |
| E | USB0 idle remove/reinsert while FLX4 remains active | 5 | 5 | pending |
| F | USB0 remove/reinsert during Library load | 5 | 5 | pending |
| G | USB0 remove/reinsert during load/decode or active playback | 5 | 5 | pending |
| H | USB1 idle disconnect/reconnect while USB0 remains mounted | 5 | 5 | pending |
| I | USB1 disconnect/reconnect during dual-deck playback | 5 | 5 | pending |
| J | USB1 disconnect/reconnect while a defined control is held | 5 | 5 | pending |
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
