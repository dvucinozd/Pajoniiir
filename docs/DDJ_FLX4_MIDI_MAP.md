# DDJ-FLX4 MIDI Map

Source file:
[docs/reference/Pioneer-DDJ-FLX4.midi.xml](reference/Pioneer-DDJ-FLX4.midi.xml)

The XML is a Mixxx controller preset. Physical capture on 2026-06-14 confirmed
that all MVP control addresses and encodings below match the connected
DDJ-FLX4. Remaining controls may therefore use the XML as the implementation
seed for status, midino, message type, deck/shift channel, and 14-bit pairing.

The XML is not executable runtime logic. `Script-Binding` entries identify MIDI
addresses, but the standalone behavior must be defined as a semantic S3 event
and P4-owned state transition. Hardware capture remains the acceptance test for
each newly delivered control group, and any difference must be recorded here.

Additional physical capture on 2026-06-20 verified SMART CFX and SMART FADER
button inputs. They are implemented as input-only semantic press/release events;
P4 DSP/settings behavior is deferred.

## Deck Channels

| Deck | Button status | CC status |
| --- | --- | --- |
| Deck 1 | `0x90` | `0xB0` |
| Deck 2 | `0x91` | `0xB1` |
| Master/global | `0x96` buttons, `0xB6` CC | `0xB6` |

## MVP Transport And Browser

| Control | Deck/group | Status | Midino | Notes |
| --- | --- | ---: | ---: | --- |
| Play/Pause | Deck 1 | `0x90` | `0x0B` | button value > 0 means pressed |
| Play/Pause | Deck 2 | `0x91` | `0x0B` | same midino, deck from status |
| Cue | Deck 1 | `0x90` | `0x0C` | back cue / cue default |
| Cue | Deck 2 | `0x91` | `0x0C` | same midino, deck from status |
| Beat Sync | Deck 1 | `0x90` | `0x58` | semantic input mapped; sync engine behavior deferred |
| Beat Sync | Deck 2 | `0x91` | `0x58` | semantic input mapped; sync engine behavior deferred |
| Load | Deck 1 | `0x96` | `0x46` | global button status, deck from midino |
| Load | Deck 2 | `0x96` | `0x47` | global button status, deck from midino |
| Browse rotate | Library | `0xB6` | `0x40` | signed 7-bit relative encoder: `0x01` = +1 step, `0x7F` = -1 step |
| Browse press | Library | `0x96` | `0x41` | toggles the P4 UI between Library and Overview; does not load a deck |
| Smart CFX | Global | `0x96` | `0x00` | press `0x7F`, release `0x00`; input-only semantic event |
| Smart Fader | Global | `0x96` | `0x01` | press `0x7F`, release `0x00`; input-only semantic event |

## Jogs

| Control | Deck | Status | Midino | Initial semantic event |
| --- | --- | ---: | ---: | --- |
| Platter scratch | 1 | `0xB0` | `0x22` | `CTRL_TYPE_JOG`, deck 1, scratch delta |
| Platter pitch bend | 1 | `0xB0` | `0x23` | `CTRL_TYPE_JOG`, deck 1, bend delta |
| Side pitch bend | 1 | `0xB0` | `0x21` | `CTRL_TYPE_JOG`, deck 1, bend delta |
| Platter touch | 1 | `0x90` | `0x36` | deck 1 jog touch on/off |
| Platter scratch | 2 | `0xB1` | `0x22` | `CTRL_TYPE_JOG`, deck 2, scratch delta |
| Platter pitch bend | 2 | `0xB1` | `0x23` | `CTRL_TYPE_JOG`, deck 2, bend delta |
| Side pitch bend | 2 | `0xB1` | `0x21` | `CTRL_TYPE_JOG`, deck 2, bend delta |
| Platter touch | 2 | `0x91` | `0x36` | deck 2 jog touch on/off |

## Tempo, Mixer, And Cue

The FLX4 sends several analog controls as 14-bit MIDI pairs. The S3 should
combine MSB/LSB into a single `0..16383` value before forwarding when both
halves are available.

| Control | Deck/group | Status | MSB midino | LSB midino | Semantic target |
| --- | --- | ---: | ---: | ---: | --- |
| Tempo fader | Deck 1 | `0xB0` | `0x00` | `0x20` | deck 1 pitch |
| Tempo fader | Deck 2 | `0xB1` | `0x00` | `0x20` | deck 2 pitch |
| Channel fader | Deck 1 | `0xB0` | `0x13` | `0x33` | mixer channel 1 volume |
| Channel fader | Deck 2 | `0xB1` | `0x13` | `0x33` | mixer channel 2 volume |
| Crossfader | Master | `0xB6` | `0x1F` | `0x3F` | mixer crossfader |
| Trim | Deck 1 | `0xB0` | `0x04` | `0x24` | later pregain |
| Trim | Deck 2 | `0xB1` | `0x04` | `0x24` | later pregain |
| Headphone cue | Deck 1 | `0x90` | `0x54` | n/a | cue/PFL toggle |
| Headphone cue | Deck 2 | `0x91` | `0x54` | n/a | cue/PFL toggle |

## Mapping Header Seed

Initial S3 header constants should be generated from this table rather than
hardcoded ad hoc in parser logic.

```c
#define FLX4_STATUS_CH1_BTN      0x90
#define FLX4_STATUS_CH2_BTN      0x91
#define FLX4_STATUS_GLOBAL_BTN   0x96
#define FLX4_STATUS_CH1_CC       0xB0
#define FLX4_STATUS_CH2_CC       0xB1
#define FLX4_STATUS_MASTER_CC    0xB6

#define FLX4_BTN_PLAY            0x0B
#define FLX4_BTN_CUE             0x0C
#define FLX4_BTN_SYNC            0x58
#define FLX4_BTN_LOAD_DECK1      0x46
#define FLX4_BTN_LOAD_DECK2      0x47
#define FLX4_BTN_PFL             0x54
#define FLX4_BTN_SMART_CFX       0x00
#define FLX4_BTN_SMART_FADER     0x01

#define FLX4_CC_JOG_SIDE_BEND    0x21
#define FLX4_CC_JOG_SCRATCH      0x22
#define FLX4_CC_JOG_BEND         0x23
#define FLX4_CC_TEMPO_MSB        0x00
#define FLX4_CC_TEMPO_LSB        0x20
#define FLX4_CC_CH_VOL_MSB       0x13
#define FLX4_CC_CH_VOL_LSB       0x33
#define FLX4_CC_CROSSFADER_MSB   0x1F
#define FLX4_CC_CROSSFADER_LSB   0x3F
#define FLX4_CC_BROWSE           0x40
#define FLX4_CC_VU_METER         0x02
```

## Extended Mapping Rules

- Copy MIDI addresses and encoding from the XML; do not infer them from Mixxx
  callback names.
- Map each physical input to a semantic event. Keep playback, mixer, pad-mode,
  effect, and LED state authoritative on the P4.
- Treat shifted statuses and performance-pad modes as distinct inputs when the
  XML assigns distinct status/midino pairs.
- Do not implement a `Script-Binding` control until standalone P4 behavior is
  defined.
- Add every implemented control to an inventory table with semantic ID,
  implementation status, and hardware acceptance status.

## Phase 7 Extended Controller Inventory

This inventory is generated from the vendored Mixxx XML as the implementation
seed. It is not a claim that Mixxx behavior is implemented. Semantic IDs marked
`proposed` must be added to both S3 and P4 `control_link.h` files before
firmware uses them.

Status legend:

- **Implemented:** routed and hardware-verified in the current DDJ-FFL4 path.
- **Mapped only:** semantic input exists, but no P4 behavior is attached yet.
- **Pending:** XML address is recorded; firmware mapping is not implemented.
- **Deferred:** address is recorded, but standalone P4 behavior is not defined.
- **Candidate LED:** XML output address is recorded; P4-driven LED feedback is
  not implemented unless explicitly noted.

### Transport, Browser, Jog, And Loop Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Browse rotate | `0xB6/0x40` | relative encoder | global | `CTRL_ID_BROWSE_DELTA` | UI Library | Implemented | Verified 2026-06-14 / 2026-06-20 |
| Browse press | `0x96/0x41` | press/release | global | `CTRL_ID_BROWSE_PRESS` | UI navigation | Implemented | Verified 2026-06-20 |
| Browse + Shift rotate | `0xB6/0x64` | relative encoder | shifted global | `CTRL_ID_BROWSE_SHIFT_DELTA` proposed | UI waveform zoom | Pending | Not captured |
| Browse + Shift press | `0x96/0x42` | press/release | shifted global | `CTRL_ID_BROWSE_SHIFT_PRESS` proposed | UI Library/navigation | Pending | Not captured |
| Load Deck 1 / Deck 2 | `0x96/0x46`, `0x96/0x47` | press/release | global, deck from midino | `CTRL_ID_LOAD_DECK1`, `CTRL_ID_LOAD_DECK2` | UI Library | Implemented | Verified 2026-06-14 / 2026-06-20 |
| Shift Deck 1 / Deck 2 | `0x90/0x3F`, `0x91/0x3F` | press/release | deck-local modifier | `CTRL_ID_DECK1_SHIFT`, `CTRL_ID_DECK2_SHIFT` | P4 input mode state | Mapped only | Verified 2026-06-20 |
| Play/Pause Deck 1 / Deck 2 | `0x90/0x0B`, `0x91/0x0B` | press/release | deck-local | `CTRL_ID_DECK1_PLAY`, `CTRL_ID_DECK2_PLAY` | `deck_core` | Implemented | Verified 2026-06-14 |
| Play + Shift / Censor | `0x90/0x0E`, `0x91/0x0E` | press/release | shifted deck-local | `CTRL_ID_DECK1_CENSOR`, `CTRL_ID_DECK2_CENSOR` proposed | `deck_core` transport | Deferred | Not captured |
| Cue Deck 1 / Deck 2 | `0x90/0x0C`, `0x91/0x0C` | press/release | deck-local | `CTRL_ID_DECK1_CUE`, `CTRL_ID_DECK2_CUE` | `deck_core` | Implemented | Verified 2026-06-14 |
| Cue + Shift / track start | `0x90/0x48`, `0x91/0x48` | press/release | shifted deck-local | `CTRL_ID_DECK1_TO_START`, `CTRL_ID_DECK2_TO_START` | `deck_core` seek | Implemented | Verified end-to-end 2026-06-20 / 2026-06-21 |
| Jog platter scratch | `0xB0/0x22`, `0xB1/0x22` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_SCRATCH`, `CTRL_ID_DECK2_JOG_SCRATCH` | `deck_core` / audio seek | Implemented MVP input | Verified 2026-06-14 |
| Jog platter bend | `0xB0/0x23`, `0xB1/0x23` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_BEND`, `CTRL_ID_DECK2_JOG_BEND` | `deck_core` / tempo bend | Implemented MVP input | Verified 2026-06-14 |
| Jog side bend | `0xB0/0x21`, `0xB1/0x21` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_BEND`, `CTRL_ID_DECK2_JOG_BEND` | `deck_core` / tempo bend | Implemented MVP input | Verified 2026-06-14 |
| Jog touch | `0x90/0x36`, `0x91/0x36` | press/release | deck-local | `CTRL_ID_DECK1_JOG_TOUCH`, `CTRL_ID_DECK2_JOG_TOUCH` | `deck_core` jog mode | Implemented MVP input | Verified 2026-06-14 |
| Jog + Shift search | `0xB0/0x29`, `0xB1/0x29` | relative/encoder CC | shifted deck-local | `CTRL_ID_DECK1_JOG_SEARCH`, `CTRL_ID_DECK2_JOG_SEARCH` proposed | `deck_core` seek | Pending | Not captured |
| Jog touch + Shift highspeed | `0x90/0x67`, `0x91/0x67` | press/release | shifted deck-local | `CTRL_ID_DECK1_JOG_SEARCH_TOUCH`, `CTRL_ID_DECK2_JOG_SEARCH_TOUCH` proposed | `deck_core` jog mode | Pending | Not captured |
| Tempo fader | D1 `0xB0/0x00+0x20`, D2 `0xB1/0x00+0x20` | 14-bit MSB+LSB | deck-local | `CTRL_ID_DECK1_TEMPO`, `CTRL_ID_DECK2_TEMPO` | audio pitch/resampler | Implemented MVP input | Verified 2026-06-14 |
| Beat Sync | `0x90/0x58`, `0x91/0x58` | press/release | deck-local | `CTRL_ID_DECK1_SYNC`, `CTRL_ID_DECK2_SYNC` | beat/sync model | Mapped only | Verified 2026-06-21; behavior deferred |
| Beat Sync long press / master | `0x90/0x5C`, `0x91/0x5C` | press/release or long-press semantic | deck-local | `CTRL_ID_DECK1_SYNC_MASTER`, `CTRL_ID_DECK2_SYNC_MASTER` proposed | beat/sync model | Deferred | Not captured |
| Beat Sync + Shift / tempo range | `0x90/0x60`, `0x91/0x60` | press/release | shifted deck-local | `CTRL_ID_DECK1_TEMPO_RANGE`, `CTRL_ID_DECK2_TEMPO_RANGE` | deck settings | Mapped only | Verified 2026-06-20 / 2026-06-21; behavior deferred |
| Loop In / 4 Beat | `0x90/0x10`, `0x91/0x10` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_IN`, `CTRL_ID_DECK2_LOOP_IN` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior host-tested |
| Loop Out | `0x90/0x11`, `0x91/0x11` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_OUT`, `CTRL_ID_DECK2_LOOP_OUT` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior host-tested |
| Reloop/Exit | `0x90/0x4D`, `0x91/0x4D` | press/release | deck-local | `CTRL_ID_DECK1_RELOOP_EXIT`, `CTRL_ID_DECK2_RELOOP_EXIT` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior host-tested |
| Reloop/Exit + Shift | `0x90/0x50`, `0x91/0x50` | press/release | shifted deck-local | `CTRL_ID_DECK1_RELOOP_STOP`, `CTRL_ID_DECK2_RELOOP_STOP` proposed | `deck_core` loop | Deferred | Not captured |
| Shift + Loop In adjust | `0x90/0x4C`, `0x91/0x4C` | press/release | shifted deck-local | `CTRL_ID_DECK1_LOOP_ADJUST_IN`, `CTRL_ID_DECK2_LOOP_ADJUST_IN` proposed | loop edit mode | Deferred | Not captured |
| Shift + Loop Out adjust | `0x90/0x4E`, `0x91/0x4E` | press/release | shifted deck-local | `CTRL_ID_DECK1_LOOP_ADJUST_OUT`, `CTRL_ID_DECK2_LOOP_ADJUST_OUT` proposed | loop edit mode | Deferred | Not captured |
| Cue/Loop Call Left / halve loop | `0x90/0x51`, `0x91/0x51` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_HALVE`, `CTRL_ID_DECK2_LOOP_HALVE` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior host-tested |
| Cue/Loop Call Right / double loop | `0x90/0x53`, `0x91/0x53` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_DOUBLE`, `CTRL_ID_DECK2_LOOP_DOUBLE` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior host-tested |
| Cue/Loop Call Left + Shift / jump back | `0x90/0x3E`, `0x91/0x3E` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_BACK`, `CTRL_ID_DECK2_BEAT_JUMP_BACK` | beat jump | Mapped only | Verified 2026-06-21; behavior deferred |
| Cue/Loop Call Right + Shift / jump forward | `0x90/0x3D`, `0x91/0x3D` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_FORWARD`, `CTRL_ID_DECK2_BEAT_JUMP_FORWARD` | beat jump | Mapped only | Verified 2026-06-21; behavior deferred |
| Shift + channel CUE / quantize | `0x90/0x68`, `0x91/0x68` | press/release | shifted deck-local | `CTRL_ID_DECK1_QUANTIZE`, `CTRL_ID_DECK2_QUANTIZE` proposed | deck quantize state | Deferred | Not captured |

### Mixer, Monitoring, And Effects Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Channel fader | D1 `0xB0/0x13+0x33`, D2 `0xB1/0x13+0x33` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_VOLUME`, `CTRL_ID_CH2_VOLUME` | audio mixer | Implemented | Verified 2026-06-14 |
| Crossfader | `0xB6/0x1F+0x3F` | 14-bit MSB+LSB | global mixer | `CTRL_ID_CROSSFADER` | audio mixer | Implemented | Verified 2026-06-14 |
| Trim / pregain | D1 `0xB0/0x04+0x24`, D2 `0xB1/0x04+0x24` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_TRIM`, `CTRL_ID_CH2_TRIM` | audio mixer | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| EQ High | D1 `0xB0/0x07+0x27`, D2 `0xB1/0x07+0x27` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_HIGH`, `CTRL_ID_CH2_EQ_HIGH` | EQ/DSP | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| EQ Mid | D1 `0xB0/0x0B+0x2B`, D2 `0xB1/0x0B+0x2B` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_MID`, `CTRL_ID_CH2_EQ_MID` | EQ/DSP | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| EQ Low | D1 `0xB0/0x0F+0x2F`, D2 `0xB1/0x0F+0x2F` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_LOW`, `CTRL_ID_CH2_EQ_LOW` | EQ/DSP | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| Headphone cue/PFL | `0x90/0x54`, `0x91/0x54` | press/release | deck-local | `CTRL_ID_DECK1_PFL`, `CTRL_ID_DECK2_PFL` | mixer/cue routing | Implemented | Verified 2026-06-14 / 2026-06-20 |
| Headphones mix | `0xB6/0x0C+0x2C` | 14-bit MSB+LSB | global monitor | `CTRL_ID_HEADPHONE_MIX` | cue routing/settings | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| Filter CH1 / CH2 | CH1 `0xB6/0x17+0x37`, CH2 `0xB6/0x18+0x38` | 14-bit MSB+LSB | channel-specific global CC | `CTRL_ID_CH1_FILTER`, `CTRL_ID_CH2_FILTER` | filter/DSP | Mapped only | Verified 2026-06-21; DSP behavior deferred |
| Smart CFX | `0x96/0x00` | press/release | global | `CTRL_ID_SMART_CFX` | future Smart CFX state | Mapped only | Verified 2026-06-20 |
| Smart Fader | `0x96/0x01` | press/release | global | `CTRL_ID_SMART_FADER` | future Smart Fader state | Mapped only | Verified 2026-06-20 |
| Beat FX select next / previous | `0x94/0x63`, `0x94/0x64` | press/release | FX section | `CTRL_ID_BEAT_FX_SELECT_NEXT`, `CTRL_ID_BEAT_FX_SELECT_PREV` proposed | Beat FX model | Deferred | Not captured |
| Beat FX beat left / right | `0x94/0x4A`, `0x94/0x4B` | press/release | FX section | `CTRL_ID_BEAT_FX_BEAT_DEC`, `CTRL_ID_BEAT_FX_BEAT_INC` proposed | Beat FX model | Deferred | Not captured |
| Beat FX channel select | CH1 `0x94/0x10`, CH2 `0x95/0x11` | press/release | FX channel selector | `CTRL_ID_BEAT_FX_TARGET` proposed | Beat FX model | Deferred | Not captured |
| Beat FX level/depth | `0xB4/0x02` | CC MSB in XML | FX section | `CTRL_ID_BEAT_FX_DEPTH` proposed | Beat FX model | Deferred | Not captured |
| Beat FX on/off | CH1/global `0x94/0x47`, CH2 `0x95/0x47` | press/release | FX channel selector | `CTRL_ID_BEAT_FX_ON` proposed | Beat FX model | Deferred | Not captured |
| Beat FX on/off + Shift | CH1/global `0x94/0x43`, CH2 `0x95/0x43` | press/release | shifted FX channel selector | `CTRL_ID_BEAT_FX_CLEAR` proposed | Beat FX model | Deferred | Not captured |

### Performance Pad Mode Inventory

The DDJ-FLX4 has four direct physical pad mode buttons: `HOT CUE`, `PAD FX1`,
`BEAT JUMP`, and `SAMPLER`. Secondary modes are reached with `SHIFT` plus one
of those four buttons. The Mixxx XML still lists each secondary mode as its own
MIDI note, so this table keeps the XML addresses but labels whether the control
is direct or shifted.

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Hot Cue mode | `0x90/0x1B`, `0x91/0x1B` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_HOT_CUE`, `CTRL_ID_DECK2_PAD_MODE_HOT_CUE` | P4 pad mode state | Mapped only | Verified 2026-06-21 |
| Shift + Hot Cue / Keyboard-Stems mode | `0x90/0x69`, `0x91/0x69` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_KEYBOARD`, `CTRL_ID_DECK2_PAD_MODE_KEYBOARD` | unsupported stems model | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |
| Pad FX1 mode | `0x90/0x1E`, `0x91/0x1E` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX1`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX1` | pad FX model | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |
| Shift + Pad FX1 / Pad FX2 mode | `0x90/0x6B`, `0x91/0x6B` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX2`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX2` | pad FX model | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |
| Beat Jump mode | `0x90/0x20`, `0x91/0x20` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP`, `CTRL_ID_DECK2_PAD_MODE_BEAT_JUMP` | P4 pad mode state | Mapped only | Verified 2026-06-21 |
| Shift + Beat Jump / Beat Loop mode | `0x90/0x6D`, `0x91/0x6D` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP`, `CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP` | P4 pad mode state | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |
| Sampler mode | `0x90/0x22`, `0x91/0x22` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_SAMPLER`, `CTRL_ID_DECK2_PAD_MODE_SAMPLER` | sampler model | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |
| Shift + Sampler / Key Shift mode | `0x90/0x6F`, `0x91/0x6F` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_KEY_SHIFT`, `CTRL_ID_DECK2_PAD_MODE_KEY_SHIFT` | P4 pad mode state | Mapped only | Verified D1/D2 2026-06-21; behavior deferred |

### Performance Pad Action Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Hot Cue pads 1-8 | D1 `0x97/0x00..0x07`, D2 `0x99/0x00..0x07` | press/release | active Hot Cue mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 hot cue state | Mapped only | Verified D1/D2 pads 1-8 2026-06-21; behavior deferred |
| Hot Cue clear pads 1-8 | D1 `0x98/0x00..0x07`, D2 `0x9A/0x00..0x07` | press/release | shifted Hot Cue mode | same pad action ID with shift flag | P4 hot cue state | Mapped only | Not captured |
| Keyboard/Stems pads 1-8 | D1 `0x97/0x40..0x47`, D2 `0x99/0x40..0x47` | press/release | active Keyboard mode | `CTRL_ID_DECK*_PAD_ACTION` candidate | stems model | Deferred | Not captured |
| Keyboard/Stems shifted pads 1-8 | D1 `0x98/0x40..0x47`, D2 `0x9A/0x40..0x47` | press/release | shifted Keyboard mode | `CTRL_ID_DECK*_PAD_ACTION` candidate | stems model | Deferred | Not captured |
| Beat Loop pads 1-8 | D1 `0x97/0x60..0x67`, D2 `0x99/0x60..0x67` | press/release | active Beat Loop mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 loop engine | Mapped only | Verified D1/D2 pads 1-8 2026-06-21; behavior deferred |
| Beat Jump pads 1-8 | D1 `0x97/0x20..0x27`, D2 `0x99/0x20..0x27` | press/release | active Beat Jump mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 beat jump | Mapped only | Verified D1 pads 1-8 and D2 pads 1,3-8 2026-06-21; behavior deferred |
| Beat Jump shifted size pads | D1 `0x98/0x26..0x27`, D2 `0x9A/0x26..0x27` | press/release | shifted Beat Jump mode | pad action with size inc/dec | P4 beat jump size state | Mapped only | Not captured |
| Sampler pads 1-8 left/right | left `0x97/0x30..0x37`, right `0x99/0x30..0x37` | press/release | active Sampler mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | sampler model | Mapped only | Verified D1/D2 pads 1-8 2026-06-21; behavior deferred |
| Sampler shifted pads 1-8 left/right | left `0x98/0x30..0x37`, right `0x9A/0x30..0x37` | press/release | shifted Sampler mode | same pad action ID with shift flag | sampler model | Mapped only | Partial D1 pad 1 2026-06-21; shifted action behavior deferred |
| Key Shift pads 1-8 | D1 `0x97/0x70..0x77`, D2 `0x99/0x70..0x77` | press/release | active Key Shift mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 key shift | Mapped only | Verified D1/D2 pads 1-8 2026-06-21; behavior deferred |
| Key Shift shifted pads 1-8 | D1 `0x98/0x70..0x77`, D2 `0x9A/0x70..0x77` | press/release | shifted Key Shift mode | same pad action ID with shift flag | P4 key shift | Mapped only | Not captured |

### Candidate LED Output Inventory

| LED/output group | XML status/midino | Source state | S3/P4 state owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- |
| Play LEDs | `0x90/0x0B`, `0x91/0x0B` | deck playing | P4 `deck_core` | Implemented | Verified 2026-06-20 reconnect |
| Play/Cue shifted alternate LEDs | `0x90/0x47`, `0x91/0x47` | Mixxx maps both play/cue indicators here | Not defined for DDJ-FFL4 | Deferred | Not captured |
| Cue LEDs | `0x90/0x0C`, `0x91/0x0C` | cue state | P4 `deck_core` | Implemented | Verified 2026-06-20 reconnect |
| Beat Sync LEDs | `0x90/0x58`, `0x91/0x58` | P4 sync-enabled placeholder state | P4 `deck_core.sync_enabled` | Implemented output | Pending hardware smoke; output probe verified 2026-06-20 |
| PFL LEDs | `0x90/0x54`, `0x91/0x54` | PFL enabled | P4 mixer/cue routing | Implemented | Verified 2026-06-20 reconnect |
| Pad mode LEDs | direct: Hot Cue `0x1B`, Pad FX1 `0x1E`, Beat Jump `0x20`, Sampler `0x22`; shifted: Keyboard `0x69`, Pad FX2 `0x6B`, Beat Loop `0x6D`, Key Shift `0x6F` on `0x90`/`0x91` | selected controller pad mode | P4 `deck_core.pad_mode` | Implemented output | Pending hardware smoke |
| Loop In LEDs | `0x90/0x10`, `0x91/0x10` | active audio loop exists | P4 `audio_engine` loop state | Implemented output | Pending hardware smoke; output probe verified 2026-06-20 |
| Loop Out LEDs | `0x90/0x11`, `0x91/0x11` | active audio loop exists | P4 `audio_engine` loop state | Implemented output | Pending hardware smoke; output probe verified 2026-06-20 |
| Hot Cue pad LEDs | normal D1 `0x97/0x00..0x07`, normal D2 `0x99/0x00..0x07`; shifted mirror D1 `0x98/0x00..0x07`, D2 `0x9A/0x00..0x07` | hot cue exists/active | P4 hot cue state | Candidate LED | Not captured |
| Beat Loop pad LEDs | normal D1 `0x97/0x60..0x67`, normal D2 `0x99/0x60..0x67`; shifted mirror D1 `0x98/0x60..0x67`, D2 `0x9A/0x60..0x67` | loop size active | P4 loop state | Candidate LED | Not captured |
| Beat Jump shifted helper LEDs | D1 `0x98/0x26..0x27`, D2 `0x9A/0x26..0x27` | track loaded in XML | P4 beat jump/pad mode state | Candidate LED | Not captured |
| Sampler pad LEDs | left normal `0x97/0x30..0x37`, left shifted `0x98/0x30..0x37`, right normal `0x99/0x30..0x37`, right shifted `0x9A/0x30..0x37` | sampler slot loaded in XML | sampler model | Deferred | Not captured |
| Channel 1 VU meter (5 LEDs) | `0xB0/0x02` | channel 1 level (0-127) | P4 audio peak timer | Implemented output | Not captured |
| Channel 2 VU meter (5 LEDs) | `0xB1/0x02` | channel 2 level (0-127) | P4 audio peak timer | Implemented output | Not captured |

## Hardware Acceptance

The MVP capture is complete. For each additional delivered control group,
capture and verify:

- button press/release values and deck/shift status;
- relative encoder direction and acceleration range;
- 14-bit analog minimum, center, maximum, and MSB/LSB order;
- mode-dependent pad messages;
- LED output values and reconnect resynchronization.
