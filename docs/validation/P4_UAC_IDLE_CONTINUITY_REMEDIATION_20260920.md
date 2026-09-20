# P4 UAC idle-continuity remediation — 2026-09-20

Status: **focused exact-image remediation and final three-hour combined soak
PASS**.

## Failure that opened the remediation

The declared combined soak on `RC2-153-g66b5fee-dirty`, `ota_0`, boot epoch
`471`, stopped after `69.187` minutes. The last scheduled operation was D2 loop
clear followed by D1 CUE/restart and a four-beat loop. Seven seconds later the
strict monitor observed:

- UAC `underflow_frames`: `1,052,226 -> 1,070,542` (`+18,316`);
- active UAC data-loss flag: `0x10`;
- PCM underruns: `0/0`;
- dropped blocks, overflow, packet failures and packet-lost frames: all `0`;
- no firmware/slot change, reboot or current TWDT evidence.

The diagnostic journal recorded `UAC_DATA_LOSS` with `a2=18316`. A focused
pre-fix reproduction then failed on its first CUE/restart cycle with a further
`+15,504` UAC underflow frames, reducing the original hour-scale failure to a
repeatable seconds-scale case.

## Root cause and source change

`ae_output_task()` handled the state in which neither deck renderer produced a
block by sleeping for 5 ms and submitting nothing. The FLX4 isochronous OUT
endpoint continued consuming the 2,048-frame UAC ring during CUE, seek and
startup-prebuffer transitions, so the ring could drain even though no decoder,
USB-packet or MAIN-output error had occurred.

The firmware now keeps the hardware output clock continuous through that
temporary all-idle renderer state:

1. zero-fill one MAIN and one headphone block;
2. submit the silent block to the FLX4 UAC producer ring;
3. write the MAIN silent block through the bounded I2S sink, which remains the
   exact pacing clock;
4. preserve existing sink-fault handling and yield behavior.

The same candidate retains the 2,048-frame UAC ring, biases its nominal fill
window to 5/8--7/8, uses a 2,048-frame prebuffer for seek/restart transitions
and leaves the ordinary PLAY prebuffer at 512 frames. The attempted 4,096-frame
ring was rejected earlier because it caused Task-WDT resets and is not part of
this candidate.

## Automated and build evidence

- Repair source commit: `b9139e1bdab79e7d0aec279383e0b8f859d3eea3`.
- Regression-first static test failed before the source change because the
  idle branch did not zero and write MAIN/headphone blocks; it passes after the
  change.
- Complete `tests/run_p4_host_tests.ps1`: PASS, exit code `0`, including the
  firmware lifecycle, audio start gate, UAC health, controller UAC stream and
  transition-harness self-tests.
- Clean ESP-IDF v6.0.2 `build_signed`: PASS.
- Application size: `2,459,392` bytes.
- Application SHA-256:
  `f2f4424f58800d201963b661d86a69052300de9f146124e4073576a878c4b153`.
- Signed bundle size: `2,459,580` bytes.
- Signed bundle SHA-256:
  `a0679ac71548aa19a01752ad7aa862fe9039b16768c06ee18d7912239bc261e5`.
- Signature: ECDSA P-256/SHA-256, key ID `rel-001`; package verification PASS.

## Exact-image hardware evidence

The signed package was installed on `ota_1`, boot epoch `472`, with empty OTA
error. USB0 remounted, the 324-track Library returned, and the FLX4 profile,
MIDI IN/OUT and UAC were active.

The new `tools/run_p4_uac_transition_stress.ps1` repeatedly executes the exact
D2 loop-clear plus D1 CUE/restart/loop sequence and samples strict counters at
100 ms cadence:

| Run | Result | Operations | Time | UAC underflow delta | Other strict deltas |
| --- | --- | ---: | ---: | ---: | ---: |
| exact MP3+FLAC fixture | PASS | 12/12 | 82.982 s | 0 | 0 |
| extended MP3+FLAC fixture | stopped by natural D2 EOF | 94 complete + 1 partial | 735.768 s | 0 | 0 |
| long-track control | PASS | 20/20 | 162.699 s | 0 | 0 |

The extended run reached the FLAC's natural end at position `730,296 ms`
against a `728,000 ms` duration. That is a test-fixture duration limit, not an
audio-loss failure: underflow stayed exactly `1,111,604`, all UAC/PCM/packet
counters stayed flat, boot stayed unchanged and the only output-late delta was
`+1` without correlated loss. Across the accepted evidence there are 126 fully
sampled problem transitions plus one partially sampled EOF transition with no
UAC loss.

After both PASS runs stopped the decks, separate eight-second idle checks still
submitted approximately 359,000 silent frames while UAC underflow and every
strict counter remained unchanged. The current-boot diagnostic log contains no
`UAC_DATA_LOSS`, reset or TWDT event.

## Acceptance boundary and next action

This closes the deterministic UAC starvation regression and validates the
installed repair under accelerated repetition. A subsequent fresh automated
three-hour combined soak completed `180.156` minutes with zero strict audio/USB
counter delta, no reboot/TWDT and explicit operator confirmation that MAIN/cue
audio remained clean throughout. See
[`P4_FINAL_COMBINED_SOAK_20260920.md`](P4_FINAL_COMBINED_SOAK_20260920.md).
