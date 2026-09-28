# P4 duplicate raw track-ID acceptance procedure

## Purpose

This hardware gate verifies that two independent Rekordbox exports with the
same non-zero numeric track ID do not share persistent Hot Cue state. It covers
media replacement, catalog generation changes, remount and a firmware-owned
software reboot. It does not add an acoustic acceptance claim.

The firmware still exposes the raw Rekordbox ID as `track_key` when the PDB ID
is non-zero. Persistent cue storage must instead use the full
`media_persistent_id_t`, which includes the complete `export.pdb` digest,
USB-relative audio path, file size and modification time.

## Test media

Prepare two labelled removable media, A and B:

- each export contains one dedicated test track with the same non-zero raw
  Rekordbox track ID;
- the two test tracks have different, unique titles so `/api/library` proves
  which export is mounted;
- the exports are independently generated and therefore have different
  `export.pdb` contents; a byte-for-byte copy intentionally has the same
  persistent identity and does not exercise this gate;
- both tracks cover the selected cue positions and the harness's 10-second
  separated recall probe positions; and
- the selected FLX4 Hot Cue pad is empty on both media. The default is pad 8.

Mount each medium once and read `/api/library`. Record the common `track_key`
and verify that the expected title occurs exactly once on each medium. Do not
run the test if the keys differ, a title is ambiguous, or the test pad is in
use.

## Harness

With medium A mounted, the exact candidate installed, USB1 FLX4 active and the
PC connected to the Pajoniiir access point, run from the repository root:

```powershell
pwsh -NoProfile -File .\tools\run_p4_duplicate_track_id_acceptance.ps1 `
  -ExpectedVersion "M2.2-37-g751d3c6" `
  -SharedTrackKey 42 `
  -MediaATitle "ID42-MEDIA-A" `
  -MediaBTitle "ID42-MEDIA-B"
```

Replace the key and titles with the values from the prepared exports. Optional
parameters select another empty pad, cue positions, tolerance, timeout and
evidence directory.

The harness performs API loads and seeks, watches physical MIDI and semantic
event counters, detects media generation changes, requests the software
reboot, validates exact firmware/slot and checks USB, UAC, PCM, packet,
`output_late`, service-log and TWDT health. The operator only:

1. confirms the requested Hot Cue pad is empty and presses it on medium A;
2. swaps to medium B and stores the same pad at the second position;
3. swaps A/B when prompted and presses the pad for recall;
4. reconnects the PC to the Pajoniiir AP after reboot if Windows drops it; and
5. clears the dedicated pad on both media when prompted.

Before each monitored pad action, the harness asks the operator to select and
visually verify D1 HOT CUE mode, then press Enter. This keeps the mode-button
MIDI event outside the pad-action evidence window. Media changes and the
following pad press are polled automatically. After each clear event, the
operator must confirm with Enter that the selected pad LED is OFF. Abort with
`Ctrl+C` if an expected-empty pad is already lit.

## Pass conditions

The result is PASS only when:

- the two catalogs expose the same requested `track_key` and distinct expected
  titles;
- medium A recalls only its A position after remount and after software reboot;
- medium B recalls only its B position after remount;
- every store, recall and clear step produces a physical MIDI packet and a
  semantic event;
- the firmware version and slot remain unchanged; and
- all strict health checks remain clean before and after reboot.

`underflow_frames` is not an absolute idle-health counter: the continuous UAC
consumer can zero-fill an empty ring while no deck is playing, and that
expected path contributes to the raw value. The harness therefore gates active
UAC `data_loss_flags` at every monitored pad/recall snapshot and keeps
dropped, overflow, packet, PCM, `output_late`, USB and log counters strict. A
non-zero active data-loss flag still fails immediately.

The harness writes JSON plus a Markdown summary under
`tmp/p4-duplicate-track-id` by default. Retain the passing evidence in the
release validation record before closing the gate. A self-test is part of
`tests/run_p4_host_tests.ps1` and can also be run directly with `-SelfTest`.
