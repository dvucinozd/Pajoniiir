# Accelerated M2 beta release plan

Status: **approved execution plan, active from 2026-09-19**.

## Release boundary

The accelerated target is a clearly labelled **M2 beta**, not an unrestricted
production release. Existing P4-only architecture, power/VBUS acceptance and
the closed 50-cycle lifecycle matrix remain valid for their recorded hardware
and images. Every new firmware image still needs image-specific automated and
physical evidence.

Production security decisions, final closed-enclosure power/thermal/RF checks
and non-FLX4 controller qualification may be deferred from M2 beta only when
they remain explicitly listed as open. They are not silently waived and still
block a production release.

## Compressed execution path

1. Run one 45--60 minute combined functional session on the candidate image.
   Use real mixed MP3/WAV/FLAC media across both deck assignments and cover
   seek, loop, CUE/restart, Hot Cue, scratch/release, near EOF, dual Master
   Tempo with opposing pitch, Beat FX routing/tails and simultaneous MAIN/cue.
   Continuous telemetry and explicit audible acceptance are mandatory.
2. Run one automated three-hour combined soak. Keep both decks active and
   periodically exercise seek, loop and CUE/restart while Master Tempo, FLX4
   traffic, MAIN/cue, FX and web status polling remain active. Stop on the first
   gated counter, USB/controller loss, reboot or OTA error.
3. Reduce the physical OTA matrix to three beta-critical paths: successful
   pull AP-to-STA-to-AP, interrupted upload with recovery and signed rollback
   to the opposite slot. Keep deterministic host tests for size, hash,
   signature, project/chip/version, newer-only and TTL rejection.
4. Migrate the version prefix from `RC2` to `M2` as one atomic change covering
   `git describe`, OTA ordering, packaging tests and documentation. The
   installed pre-migration RC2 image cannot order an `M` offer, so first use a
   signed local RC2 bridge built from the migration commit; then tag that same
   commit `M2` and prove the public newer-only pull path end to end.
5. Freeze one commit, run the complete host/UI and clean ESP-IDF v6.0.2 signed
   build gates, install that exact image, repeat the short manual smoke, publish
   hashes/evidence, then merge and tag the beta.

## Executable harness

`tools/run_p4_release_qualification.ps1` implements the first two steps. It
uses guarded web controls for deterministic LOAD/PLAY/seek/loop/CUE operations,
samples release counters throughout the run, checks the boot epoch at the end
and writes JSON plus Markdown evidence under
`tmp/p4-release-qualification/`.

FLX4-only behavior is intentionally retained as an operator gate. HTTP
operations are not evidence for Master Tempo, scratch, Hot Cue, Beat FX or
audible MAIN/cue quality.

The Stage 2 operator sequence is deliberately deck-by-deck: complete scratch,
Hot Cue and near-EOF Shift+Jog search on D1 and restore stable PLAY before
repeating on D2. This keeps the other deck continuously rendering while making
the physical sequence reproducible. It does not relax the zero-PCM-underrun
gate or permit a transient to be relabelled as a pass.

Example commands:

```powershell
# Combined 50-minute functional run.
.\tools\run_p4_release_qualification.ps1 `
  -Mode Functional -DurationMinutes 50 `
  -OperationIntervalSeconds 20 `
  -ExpectedVersion RC2-150-g909e068 `
  -Deck1TrackKey 1 -Deck2TrackKey 56

# Automated three-hour soak after the physical stress state is prepared.
.\tools\run_p4_release_qualification.ps1 `
  -Mode Soak -DurationMinutes 180 `
  -ExpectedVersion RC2-150-g909e068 `
  -Deck1TrackKey 115 -Deck2TrackKey 18
```

The short MP3/96 kHz FLAC pair in the functional example is the reproduced
dual-Master-Tempo fixture. The soak example uses the current Library's long MP3
plus long FLAC pair so natural EOF cannot outrun the operation cadence. Other
real WAV/FLAC assignments should be selected for the functional matrix; track
keys are generation-specific and must be confirmed from the live Library before
each declared run.

## Acceptance and failure policy

The harness fails immediately or finally on:

- reboot or firmware/slot change;
- USB0, FLX4 profile, MIDI or UAC loss;
- PCM underrun, UAC drop/overflow/packet loss or active data-loss flag;
- locked backend reads, USB recovery/daemon/runtime queue failure or service
  log drop;
- current Task-WDT evidence, OTA error or loss of dual playback;
- output-late growth above the declared bounded allowance;
- rejected or missing operator listening confirmation in functional mode.

Isolated output-late warnings remain recorded rather than automatically being
equated with audible failure. Any late warning correlated with PCM/UAC loss,
reset or audible interruption is a failure regardless of the numeric allowance.

## Definition of M2 beta done

M2 beta is ready only when both combined runs, the reduced physical OTA matrix,
the prefix migration, the final fresh-checkout automated/build gates and the
exact-image post-install smoke all pass and are documented. A production claim
still additionally requires the deferred enclosure and security decisions.

## 2026-09-19 current-image checkpoint

The signed `RC2-151-g838c254-dirty` image on `ota_0` includes the P4 timeline
fast path and bounded WSOLA search. The full host suite and ESP-IDF v6.0.2
`build_signed` passed; the installed image is 2,459,136 bytes with SHA-256
`a96e78e9fb498decd7820846c1105221d21b4989bc4e733280b0d00496be40b0`.

Focused hardware evidence is positive: the exact mixed-rate dual-Master-Tempo
fixture ran 184 seconds with zero PCM underruns and no UAC loss or WDT, with
clean operator listening. A near-EOF Shift+Jog search with an active loop first
reproduced a false-PLAYING frozen D2 waveform. The source now exits the loop and
clamps search to the last valid millisecond; the post-OTA physical reproduction
no longer freezes, reaches natural EOF and restarts cleanly with zero PCM,
locked-read and UAC errors.

The first declared 45-minute functional attempt remains a FAIL at Stage 2:
after approximately 16.8 minutes it recorded D1 `+165` PCM underrun frames and
USB-headphone `+47,234` underflow frames during the combined manual sequence.
There was no WDT, reboot, UAC data-loss flag, packet failure, locked read or
confirmed audible interruption. Subsequent isolated D1/D2 Hot Cue, scratch and
search tests all completed without another PCM or USB underflow; D1 search added
one non-audible locked read. Therefore the full functional gate is still open
and must be rerun with the deterministic deck-by-deck Stage 2 instructions.

The deterministic rerun also remains a FAIL: at approximately 20.5 minutes it
recorded `+15,510` UAC underflow frames and active loss flag `0x10`, despite
zero deck PCM underruns, dropped blocks, packet failures, locked reads, WDT or
reboot and no operator-audible interruption. Inspection found that active FLX4
UAC raised its consumer task to priority 7 while its sole ring producer,
`ae_output`, remained at priority 6. Dense jog MIDI traffic could therefore
preempt the producer until the UAC ring emptied.

The candidate now keeps both UAC consumer and audio producer at priority 6,
using ESP-IDF's enabled time slicing and the output task's existing blocking
I2S write/yield points. The updated signed image was installed on `ota_1`; its
SHA-256 is
`f1da53d71081bc0fb77a7b3970996307d3858a6ac8fd20287295508decf23322`.
A focused physical rerun generated 2,658 MIDI packets through rapid jog,
scratch/re-grab, Hot Cue and Shift+Jog across both decks with UAC underflow,
overflow and drop deltas all zero, PCM `0/0`, no packet loss, no output-late
event and no WDT. A following strict ten-minute dual-Master-Tempo window also
passed with all those deltas at zero, final ring fill 1,056/2,048 and explicit
operator confirmation of clean MAIN/cue audio. This closes the scheduler
remediation.

The operator then continued the same session beyond one hour. The UAC producer
accounted for 206,853,017 additional frames, or 78.176 minutes at 44.1 kHz.
The complete current-boot service log has no `UAC_DATA_LOSS`,
`AUDIO_UNDERRUN`, disconnect, reset or WDT event; PCM, locked reads, UAC
drop/overflow and packet-loss counters stayed at zero. Six isolated late blocks
(maximum `16,401 us`) had no correlated loss or audible defect. The operator
confirmed uninterrupted, artifact-free sound for the entire period. The
combined 45--60 minute functional gate is therefore **PASS**. The next
accelerated release gate is the automated three-hour combined soak.

## 2026-09-20 combined-soak remediation checkpoint

The first automated three-hour combined soak stopped after `69.187` minutes
with `+18,316` FLX4 UAC underflow frames and active loss flag `0x10`. PCM
underruns, dropped blocks, overflow, packet failures/lost frames, locked reads,
reboot and TWDT evidence all remained zero. The last scheduled operation was
D2 loop clear followed by D1 CUE/restart and loop set.

A seconds-scale focused harness reproduced the same class of underflow on its
first pre-fix cycle. `ae_output_task()` was sleeping when both renderers were
temporarily inactive instead of continuing to clock the FLX4 isochronous sink.
The candidate now submits silence to UAC and MAIN through that state. The full
host suite, clean ESP-IDF v6.0.2 signed build and signed-package verification
pass. The package was installed on `ota_1`, boot epoch `472`, and completed 126
fully sampled focused transitions plus one partially sampled natural-EOF
transition with zero UAC/PCM/packet loss, no reboot/TWDT and stable post-stop
idle continuity. See
[`validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md`](validation/P4_UAC_IDLE_CONTINUITY_REMEDIATION_20260920.md).

This closes the deterministic defect. The subsequent fresh combined soak on
the same installed candidate passed for `180.156` minutes and 60 scheduled
operations. UAC underflow, overflow, drops, packet failures/lost frames, PCM
underruns, locked reads and active loss flags all had zero delta; firmware,
slot and boot identity stayed fixed and no TWDT occurred. The `+68`
output-late count remained below the declared 120 allowance without correlated
loss. The operator confirmed that audio was clean throughout. See
[`validation/P4_FINAL_COMBINED_SOAK_20260920.md`](validation/P4_FINAL_COMBINED_SOAK_20260920.md).

The accelerated combined functional and soak gates are now closed. Continue
with the reduced OTA/fault matrix, `RC2` to `M2` prefix migration and final
fresh-checkout release pass.
