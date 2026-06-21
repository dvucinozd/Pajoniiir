# FLX4 Shifted Beat Loop Pads and Beat Loop Pad LEDs Design

## Goal

Close the next Beat Loop slice by adding shifted Beat Loop pad behavior and
P4-owned Beat Loop pad LED feedback.

## Shifted Beat Loop Pad Behavior

Shifted Beat Loop pads act as momentary loop-roll controls:

1. On shifted Beat Loop pad press, P4 snapshots the addressed deck's current
   loop state.
2. P4 sets a temporary loop at the current deck position using the same pad
   length table as normal Beat Loop pads:
   `1/32, 1/16, 1/8, 1/4, 1/2, 1, 2, 4` beats.
3. On shifted Beat Loop pad release, P4 restores the previous loop if one was
   active, or clears the loop if no loop was active before the shifted press.
4. Deck play/pause state is not changed.

The behavior is deck-local and P4-owned. S3 MIDI mapping and the `0xA5` frame
format remain unchanged.

## Beat Loop Pad LED Behavior

Beat Loop pad LEDs are driven from P4-confirmed state:

- Add LED IDs for Beat Loop pad 1-8.
- S3 maps these LED IDs to FLX4 MIDI output notes:
  - Deck 1: `0x97/0x60..0x67`
  - Deck 2: `0x99/0x60..0x67`
- P4 publishes Beat Loop pad LED states only from the LED snapshot pipeline.
- A Beat Loop pad LED is on only when:
  - the deck pad mode is `CTRL_PAD_MODE_BEAT_LOOP`;
  - the deck has an active audio loop;
  - the loop duration matches one of the 8 Beat Loop pad lengths within a small
    tolerance.
- All Beat Loop pad LEDs are off when the deck is not in Beat Loop mode or no
  matching loop length exists.

Shifted mirror LEDs (`0x98/0x60..0x67`, `0x9A/0x60..0x67`) remain out of scope.

## Verification

Host tests must cover:

- shifted press sets a temporary loop;
- shifted release restores a previous loop;
- shifted release clears when no previous loop existed;
- normal Beat Loop behavior remains unchanged;
- P4 LED snapshot publishes exactly one matching Beat Loop pad LED in Beat Loop
  mode;
- Beat Loop pad LEDs turn off outside Beat Loop mode;
- S3 LED MIDI mapping produces expected FLX4 notes for Beat Loop pad LEDs.

Firmware verification:

- `.\tests\run_p4_host_tests.ps1`
- `.\tests\run_s3_host_tests.ps1`
- `idf.py build` in both `firmware\main-deck-p4` and
  `firmware\control-board-s3`

Hardware smoke remains pending.
