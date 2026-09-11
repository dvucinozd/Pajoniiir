# P4 dual-USB lifecycle/recovery matrix

Opened: **2026-09-11**

Status: **IN PROGRESS — cold-boot Group A complete after remediation; Group B
warm/software reboot is next**.

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
| B | Warm/software reboot with both attached | 4 | 0 | pending |
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
