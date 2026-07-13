# FLX4 Jog Search And Master Cue Design

Document status (2026-07-13): implemented for its original scope. Its explicit
Vinyl exclusion is historical and superseded by `../../VINYL_SCRATCH_PLAN.md`;
the controller's MIDI Vinyl-mode toggle and audible scratch engine are separate
concerns.

## Goal

Add the remaining practical DDJ-FLX4 controls for jog search and physical Master Cue while explicitly leaving Vinyl mode out of firmware behavior.

## Scope

Implement:

- Jog + Shift search rotate:
  - Deck 1: `0xB0/0x29`
  - Deck 2: `0xB1/0x29`
- Jog touch + Shift high-speed/search touch:
  - Deck 1: `0x90/0x67`
  - Deck 2: `0x91/0x67`
- Physical Master Cue:
  - normal press: `0x96/0x63`
  - shifted press: `0x96/0x68`, mapped to the same semantic event for now
  - LED output: `0x96/0x63`

Do not implement Vinyl mode. The official MIDI list says Vinyl cannot be changed from the unit and must be changed by MIDI OUT from the DJ application. Firmware keeps current jog/scratch behavior and documents Vinyl as intentionally out of scope.

## Architecture

S3 remains a mapper/transport bridge. It maps the XML/PDF MIDI controls to semantic control-link IDs and forwards them to P4. P4 owns behavior.

Jog Search is a deck-local encoder semantic separate from scratch/bend. P4 treats it as a fast seek relative to the current deck position. This avoids changing normal jog scratch and pitch-bend behavior.

Master Cue is a global P4 audio monitor state. The current default monitor behavior remains unchanged: master cue is enabled by default, so existing headphone/speaker behavior does not regress. Pressing Master Cue toggles whether the master mix is admitted into the monitor/headphone path. PFL/cue remains controlled by CH CUE buttons.

Master Cue LED is P4-owned and included in reconnect LED snapshots.

## Data flow

1. FLX4 sends MIDI input to S3.
2. S3 maps Jog Search rotate to `CTRL_ID_DECK*_JOG_SEARCH` as an encoder event.
3. S3 maps Master Cue press to `CTRL_ID_MASTER_CUE` as a button event.
4. P4 handles Jog Search encoder events with a bounded fast seek.
5. P4 toggles `audio_engine` master-cue monitor state on Master Cue press.
6. P4 publishes the Master Cue LED in the FLX4 LED snapshot.

## Behavior

Jog Search:

- Relative positive deltas seek forward.
- Relative negative deltas seek backward.
- Seeking clamps at `0 ms`.
- It works while playing and paused by using the existing audio-engine seek path.
- It does not affect Beat Sync, pitch, or normal jog behavior.

Master Cue:

- Default is enabled to match existing monitor behavior.
- Press toggles enabled/disabled.
- Release is ignored.
- When disabled, the headphone/monitor path receives no master contribution from the Headphones Mix master side; cue/PFL contribution remains available.
- Main/RCA output is never affected.

## Testing

Host tests should cover:

- S3 maps `B0/B1 0x29` to deck-specific Jog Search semantic events.
- S3 maps `96 63` and `96 68` to Master Cue.
- Shared S3/P4 control-link constants match.
- P4 routes Jog Search encoder events to a fast deck seek.
- P4 routes Master Cue press to the audio-engine toggle and ignores release.
- Audio output mixer keeps main master unchanged while Master Cue disabled removes master from headphone/monitor output.
- S3 LED mapping produces Master Cue LED MIDI OUT `0x96/0x63`.

Firmware verification should run S3 host tests, P4 host tests, and both S3/P4 firmware builds because this changes shared protocol IDs and both firmware targets.
