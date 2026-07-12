# Controller Profile Schema v1

Data-driven controller profiles let the S3 map a USB MIDI controller to the
existing deck-aware `control_link` semantic events without a firmware rebuild.
This document is the authoritative specification for both formats:

- `profile.json` — human/tool-friendly source format (Windows Profile Builder
  output, hand-editable).
- `profile.s3bin` — compact binary runtime format the P4 sends to the S3 over
  the UART link and the S3 executes as a lookup table.

The compiler between the two is `tools/controller_profile/compile_profile.py`.
The S3-side parser/matcher is
`firmware/control-board-s3/components/controller_profile/`.

Design constraints:

- The semantic vocabulary is exactly the existing `control_link.h` ID space
  (`CTRL_TYPE_*` / `CTRL_ID_*`). Profiles translate raw MIDI into that
  vocabulary; they cannot invent new semantics.
- The S3 runtime is a table interpreter: no JSON, no allocation surprises, no
  per-controller code. Everything stateful it needs (14-bit pairing, paired
  toggle state) is declared in the table.
- One profile targets one controller (VID/PID). The FLX4 built-in C map stays
  as fallback; the hand-written FLX4 profile in
  `controllers/pioneer_ddj_flx4/profile.json` must reproduce it exactly (the
  `controller_profile` host parity test enforces this).

## SD/TF layout

```text
/controllers/
    pioneer_ddj_flx4/
        profile.json      source of truth, editable
        profile.s3bin     compiled runtime table (what P4 sends to S3)
```

## profile.json

```json
{
  "schema": "p4-controller-profile-v1",
  "name": "Pioneer DDJ-FLX4",
  "vendor": "Pioneer DJ",
  "vid": "0x2B73",
  "pid": "0x0045",
  "decks": 2,
  "capabilities": {
    "led_feedback": true,
    "usb_audio": true,
    "jog_touch": true,
    "pitch_14bit": true
  },
  "inputs": [ ... ],
  "outputs": [ ... ],
  "audio": {
    "enabled": true,
    "headphone_channels": [3, 4],
    "preferred_sample_rate": 48000
  }
}
```

Numeric fields accept `"0xNN"` hex strings or plain integers. `audio` is
informational for the P4 (and future audio generalisation); only its presence
sets the USB-audio capability flag in the binary. The FLX4 USB audio path
stays hardcoded until the audio profile work matures.

### Input entry types

Every input entry matches on `(status, data1)` of a 3-byte MIDI message and
emits one semantic event. `event` names come from the vocabulary below.

| `type` | Fields | Behaviour |
| --- | --- | --- |
| `button` | `event`, `status`, `data1` | Emits `value = data2 > 0 ? 1 : 0` on both edges. |
| `ext_action` | `deck` (1/2), `action`, `status`, `data1` | Emits `deckN.ext_action` with `value = action \| (pressed ? 0x80 : 0)` (`CTRL_DECK_EXT_VALUE`). |
| `pad_bank` | `deck`, `shifted` (bool), `status`, `first_data1`, `count`, `mode` | Compiler expands to `count` entries; each emits `deckN.pad_action` with the packed `CTRL_PAD_ACTION_VALUE(mode, pad, shifted, pressed)` value (press bit `0x80`). |
| `encoder_rel64` | `event`, `status`, `data1` | Relative encoder centred at 64: `delta = data2 - 64`; zero deltas are dropped. |
| `encoder_2c` | `event`, `status`, `data1` | Two's-complement relative: `0x00`/`0x40` drop; `< 0x40` positive; else `data2 - 0x80`. |
| `cc14` | `event`, `status`, `msb`, `lsb`, `replay` (bool) | 14-bit CC pair. Compiler emits two table entries sharing one pairing slot; the runtime emits `value = msb<<7 \| lsb` only when both halves have been seen. |
| `cc7_abs` | `event`, `status`, `data1`, `replay` (bool) | 7-bit absolute → `value = data2 & 0x7F`. |
| `state_pair` | `event`, `members` (2× `{status,data1}`), `values` (4 entries, `null` = no emit) | Two buttons share latched pressed-state bits; on every edge the runtime emits `values[member0_bit \| member1_bit<<1]`. Used for FLX4 Beat FX target CH1/CH2/BOTH. |

`replay: true` marks absolute controls whose last complete value the S3
re-emits after a P4 heartbeat/reconnect recovery (input snapshot replay). It
must mirror what `flx4_map_emit_snapshot()` covers today: channel volumes,
crossfader, trim, EQ, filter, master volume, headphone mix/level, Beat FX
depth — deliberately **not** tempo faders or buttons.

### Semantic event vocabulary

Deck events exist as `deck1.*` and `deck2.*`; the compiler resolves them to
`CTRL_NS_DECK1/2` offsets. Wire values are defined in `control_link.h` and
asserted by the `control_link_protocol` host test.

| Event | Type on wire | Notes |
| --- | --- | --- |
| `deckN.play` `deckN.cue` `deckN.shift` `deckN.to_start` `deckN.sync` `deckN.tempo_range` `deckN.loop_in` `deckN.loop_out` `deckN.reloop_exit` `deckN.loop_halve` `deckN.loop_double` `deckN.beat_jump_back` `deckN.beat_jump_forward` `deckN.jog_touch` `deckN.jog_search_touch` `deckN.pfl` | BUTTON | 0/1 |
| `deckN.pad_mode_hot_cue` `deckN.pad_mode_beat_loop` `deckN.pad_mode_beat_jump` `deckN.pad_mode_pad_fx1` `deckN.pad_mode_pad_fx2` | BUTTON | pad mode select |
| `deckN.pad_action` | BUTTON | packed value; use `pad_bank` |
| `deckN.ext_action` | BUTTON | packed value; use `ext_action` with action names `censor`, `sync_master`, `reloop_stop`, `loop_adjust_in`, `loop_adjust_out`, `quantize` |
| `deckN.jog_scratch` `deckN.jog_bend` `deckN.jog_search` | ENCODER | signed delta |
| `deckN.tempo` | PITCH | 14-bit |
| `mixer.ch1_volume` `mixer.ch2_volume` `mixer.crossfader` `mixer.ch1_trim` `mixer.ch2_trim` `mixer.ch1_eq_high` `mixer.ch2_eq_high` `mixer.ch1_eq_mid` `mixer.ch2_eq_mid` `mixer.ch1_eq_low` `mixer.ch2_eq_low` `mixer.ch1_filter` `mixer.ch2_filter` `mixer.headphone_mix` | PITCH | 14-bit |
| `browser.delta` `browser.shift_delta` | ENCODER | relative |
| `browser.press` `browser.shift_press` `browser.load_deck1` `browser.load_deck2` `browser.shift_load_deck1` `browser.shift_load_deck2` | BUTTON | |
| `system.smart_cfx` `system.smart_fader` `system.smart_cfx_shift` `system.smart_fader_shift` `system.master_cue` | BUTTON | |
| `system.beat_fx_select_next` `system.beat_fx_select_prev` `system.beat_fx_beat_dec` `system.beat_fx_beat_inc` `system.beat_fx_beat_dec_shift` `system.beat_fx_beat_inc_shift` `system.beat_fx_on` `system.beat_fx_clear` | BUTTON | |
| `system.beat_fx_target` | BUTTON | via `state_pair`, values 0/1/2 |
| `system.beat_fx_depth` | PITCH | `cc7_abs`, replayable |
| `system.master_volume` `system.headphone_level` | PITCH | 14-bit |

### Output (LED) entry types

LED entries map the P4's semantic LED frames (`led_id` + deck + state) to
controller-specific MIDI OUT.

| `kind` | Fields | Behaviour |
| --- | --- | --- |
| `note` | `led`, `deck_status` (2-element array) or `status` + `"deck": "any"`, `data1`, optional `on`/`off`/`blink` (default `0x7F`/`0x00`/`0x7F`) | state 0 → off byte, 1 → on byte, 2 → blink byte. |
| `cc_value` | same addressing, no on/off | data byte = LED state value `& 0x7F` (value passthrough, e.g. VU meter level). |
| `note_bank` | `led_bank`, `deck_status`, `first_data1`, `count` | Compiler expands to `count` sequential `note` entries for pad LED banks. |

`deck_status: ["0x90", "0x91"]` expands to two entries (deck 0 and deck 1).
`"deck": "any"` produces one entry matching any deck (global-status LEDs).

LED names mirror `control_link.h`: `cue`, `play`, `pfl`, `vu_meter`,
`pad_mode_*`, `sync`, `loop_in`, `loop_out`, `smart_cfx`, `smart_fader`,
`beat_fx_on`, `master_cue`, `censor`, `cue_shift`, `loop_adjust_in`,
`loop_adjust_out`, `track_load_deck1`, `track_load_deck2`, plus pad banks
`hot_cue_pads`, `pad_fx1_pads`, `pad_fx2_pads`, `beat_jump_pads`,
`beat_loop_pads`, `beat_jump_shift_helpers`.

## profile.s3bin (S3CP v2)

Version 2 invalidates older binaries whose numeric LED vocabulary can alias
newer Track Load IDs to older pad-bank IDs. Recompile `profile.json` and replace
the SD-card `.s3bin`; v1 is rejected instead of silently emitting an unrelated
MIDI LED message.

Little-endian, packed. CRC32 is IEEE 802.3 (zlib `crc32`) computed over all
bytes from offset 16 to the end of the file.

### Header — 32 bytes

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 4 | magic `"S3CP"` |
| 4 | 2 | version (1) |
| 6 | 2 | header_size (32) |
| 8 | 4 | profile_size (total file bytes) |
| 12 | 4 | crc32 over bytes `[16, profile_size)` |
| 16 | 2 | vid |
| 18 | 2 | pid |
| 20 | 4 | flags: bit0 LED_FEEDBACK, bit1 USB_AUDIO, bit2 JOG_TOUCH, bit3 PITCH_14BIT |
| 24 | 2 | input_count |
| 26 | 2 | output_count |
| 28 | 1 | pair_slot_count |
| 29 | 1 | decks |
| 30 | 2 | reserved (0) |

Input entries follow the header; output entries follow the inputs.

### Input entry — 16 bytes

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | match_status |
| 1 | 1 | match_data1 |
| 2 | 1 | raw_type (below) |
| 3 | 1 | pair_slot (0xFF = none) |
| 4 | 1 | semantic_type (`CTRL_TYPE_*`) |
| 5 | 1 | semantic_id (`CTRL_ID_*`) |
| 6 | 2 | flags: bit0 REPLAY, bit1 PAIR_MEMBER_B |
| 8 | 2 | base_value (int16) |
| 10 | 2 | press_mask |
| 12 | 4 | lut (4× int8; `-1` = no emit) |

`raw_type`:

| Value | Name | Semantics |
| ---: | --- | --- |
| 0 | NOTE_BUTTON | `value = data2 > 0 ? 1 : 0` |
| 1 | NOTE_VALUE | `value = base_value \| (data2 > 0 ? press_mask : 0)` |
| 2 | CC_REL64 | `delta = data2 - 64`; drop 0 |
| 3 | CC_REL_2C | two's complement; drop 0 |
| 4 | CC14_MSB | store MSB in pair_slot; emit when pair complete |
| 5 | CC14_LSB | store LSB in pair_slot; emit when pair complete |
| 6 | CC7_ABS | `value = data2 & 0x7F` |
| 7 | NOTE_STATE_PAIR | latch member bit (flags bit1 selects member B) in pair_slot; emit `lut[bitA \| bitB<<1]`, `-1` = no emit |

### Output entry — 12 bytes

| Offset | Size | Field |
| ---: | ---: | --- |
| 0 | 1 | led_id (`control_link.h` LED id) |
| 1 | 1 | deck (0, 1, or 0xFF = any) |
| 2 | 1 | out_kind: 0 NOTE_ONOFF, 1 CC_VALUE |
| 3 | 1 | status |
| 4 | 1 | data1 |
| 5 | 1 | off_value |
| 6 | 1 | on_value |
| 7 | 1 | blink_value |
| 8 | 2 | flags (0) |
| 10 | 2 | reserved (0) |

Runtime lookup key is `(led_id, deck)`; entries with `deck = 0xFF` match any
deck. NOTE_ONOFF picks the byte by LED state (0/1/2); CC_VALUE passes the
state byte through (`& 0x7F`).

## Runtime state requirements

The S3 runtime allocates per active profile:

- `pair_slot_count` × 14-bit pairing slots (msb/lsb + valid bits) shared with
  NOTE_STATE_PAIR latched bits;
- last-value storage for REPLAY-flagged entries so the input snapshot replay
  after P4 heartbeat recovery keeps working (parity with
  `flx4_map_emit_snapshot()`).

## Known limitations (v1)

- Only 3-byte channel messages (note/CC) are matchable. SysEx is out of scope.
- Stateful behaviours are limited to what the table can express (14-bit
  pairing, two-member state pairs). Anything beyond that stays in built-in C
  profiles.
- Audio layout is capability metadata only; the FLX4 USB audio path remains
  hardcoded.
- LED blink is a value choice per entry, not an S3-side timer.

## Verification

- `tools/controller_profile/compile_profile.py profile.json -o profile.s3bin`
  compiles; `--dump profile.s3bin` pretty-prints a binary for debugging.
- Host tests (`tests/controller_profile/`, run by
  `tests/run_s3_host_tests.ps1`):
  - header/CRC/bounds validation of the parser;
  - **golden parity**: a brute-force sweep of MIDI messages through the
    compiled FLX4 profile and the built-in `flx4_map` must produce identical
    semantic events, and snapshot replay sets must match.
