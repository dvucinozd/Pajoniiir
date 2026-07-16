# DDJ-FLX4 MIDI Map

Status: current mapping and acceptance ledger, audited 2026-07-13. `Implemented`
means a firmware path exists; hardware acceptance is stated separately per row
or section. XML supplies addresses, never standalone runtime behavior.

Source file:
[docs/reference/Pioneer-DDJ-FLX4.midi.xml](reference/Pioneer-DDJ-FLX4.midi.xml)

Additional official controller MIDI list:
[docs/reference/DDJ-FLX4_MIDI_message_List.md](reference/DDJ-FLX4_MIDI_message_List.md)

The XML is a Mixxx controller preset. Physical capture on 2026-06-14 confirmed
that all MVP control addresses and encodings below match the connected
DDJ-FLX4. Remaining controls may therefore use the XML as the implementation
seed for status, midino, message type, deck/shift channel, and 14-bit pairing.
The official Pioneer MIDI message list is also tracked as a reference for
controller-visible input/output notes, especially illumination and settings
rows that are not present in the Mixxx XML. If the official list, XML, and
hardware smoke disagree, record the conflict here and do not implement behavior
until the hardware path is verified.

The XML is not executable runtime logic. `Script-Binding` entries identify MIDI
addresses, but the standalone behavior must be defined as a semantic S3 event
and P4-owned state transition. Hardware capture remains the acceptance test for
each newly delivered control group, and any difference must be recorded here.

Additional physical capture on 2026-06-20 verified SMART CFX and SMART FADER
button inputs. They are implemented as semantic press/release events that toggle
P4-owned Smart CFX and Smart Fader state; Smart CFX drives the P4 filter DSP and
Smart Fader drives the conservative transition-assist curve.

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
| Beat Sync | Deck 1 | `0x90` | `0x58` | semantic input mapped; P4 BPM-match-on-press plus one-shot signed intra-beat phase-align behavior implemented |
| Beat Sync | Deck 2 | `0x91` | `0x58` | semantic input mapped; P4 BPM-match-on-press plus one-shot signed intra-beat phase-align behavior implemented |
| Load | Deck 1 | `0x96` | `0x46` | global button status, deck from midino |
| Load | Deck 2 | `0x96` | `0x47` | global button status, deck from midino |
| Load + Shift | Deck 1 | `0x96` | `0x68` | distinct semantic ID; P4 currently loads selected browser track like normal Load |
| Load + Shift | Deck 2 | `0x96` | `0x7A` | distinct semantic ID; P4 currently loads selected browser track like normal Load |
| Browse rotate | Library | `0xB6` | `0x40` | signed 7-bit relative encoder: `0x01` = +1 step, `0x7F` = -1 step |
| Browse press | Library | `0x96` | `0x41` | toggles the P4 UI between Library and Overview; does not load a deck |
| Smart CFX | Global | `0x96` | `0x00` | press `0x7F`, release `0x00`; toggles P4 Smart CFX state |
| Smart Fader | Global | `0x96` | `0x01` | press `0x7F`, release `0x00`; toggles P4 Smart Fader state |
| Shift + Smart CFX | Global | `0x96` | `0x08` | press `0x7F`, release `0x00`; mapped as P4 no-op placeholder |
| Shift + Smart Fader | Global | `0x96` | `0x09` | press `0x7F`, release `0x00`; mapped as P4 no-op placeholder |

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
| Master Level | Master | `0xB6` | `0x08` | `0x28` | master output volume |
| Trim | Deck 1 | `0xB0` | `0x04` | `0x24` | trim/pregain |
| Trim | Deck 2 | `0xB1` | `0x04` | `0x24` | trim/pregain |
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
#define FLX4_BTN_SMART_CFX_SHIFT 0x08
#define FLX4_BTN_SMART_FADER_SHIFT 0x09

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
- **Mapped output only:** S3 can emit the official LED packet, but P4 does not
  yet publish reconnect-safe state for that indicator.
- **Implemented snapshot output:** P4 owns the state and includes the LED in the
  reconnect-safe FLX4 snapshot.

### Transport, Browser, Jog, And Loop Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Browse rotate | `0xB6/0x40` | relative encoder | global | `CTRL_ID_BROWSE_DELTA` | UI Library / Overview waveform zoom | Implemented | Verified 2026-06-14 / 2026-06-20; Overview zoom hardware smoke passed 2026-07-01 |
| Browse press | `0x96/0x41` | press/release | global | `CTRL_ID_BROWSE_PRESS` | UI navigation | Implemented | Verified 2026-06-20 |
| Browse + Shift rotate | `0xB6/0x64` | relative encoder | shifted global | `CTRL_ID_BROWSE_SHIFT_DELTA` | UI Library / Overview accelerated navigation | Implemented | Host-tested from XML; hardware smoke pending |
| Browse + Shift press | `0x96/0x42` | press/release | shifted global | `CTRL_ID_BROWSE_SHIFT_PRESS` | UI Library force-open | Implemented | Host-tested from XML; hardware smoke pending |
| Load Deck 1 / Deck 2 | `0x96/0x46`, `0x96/0x47` | press/release | global, deck from midino | `CTRL_ID_LOAD_DECK1`, `CTRL_ID_LOAD_DECK2` | UI Library | Implemented | Verified 2026-06-14 / 2026-06-20 |
| Shift + Load Deck 1 / Deck 2 | `0x96/0x68`, `0x96/0x7A` | press/release | shifted global, deck from midino | `CTRL_ID_SHIFT_LOAD_DECK1`, `CTRL_ID_SHIFT_LOAD_DECK2` | UI Library | Implemented; currently same load behavior as normal Load with distinct observability IDs | Host-tested from official PDF/XML; hardware smoke pending |
| Shift Deck 1 / Deck 2 | `0x90/0x3F`, `0x91/0x3F` | press/release | deck-local modifier | `CTRL_ID_DECK1_SHIFT`, `CTRL_ID_DECK2_SHIFT` | P4 input mode state | Mapped only | Verified 2026-06-20 |
| Play/Pause Deck 1 / Deck 2 | `0x90/0x0B`, `0x91/0x0B` | press/release | deck-local | `CTRL_ID_DECK1_PLAY`, `CTRL_ID_DECK2_PLAY` | `deck_core` | Implemented | Verified 2026-06-14 |
| Play + Shift / Censor | `0x90/0x0E`, `0x91/0x0E` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_CENSOR` | `deck_core` slip-censor MVP | Implemented MVP | Host-tested from XML; hardware smoke pending |
| Cue Deck 1 / Deck 2 | `0x90/0x0C`, `0x91/0x0C` | press/release | deck-local | `CTRL_ID_DECK1_CUE`, `CTRL_ID_DECK2_CUE` | `deck_core` | Implemented | Verified 2026-06-14 |
| Fader-start generated Play/Cue | D1 `0x90/0x66`, `0x90/0x52`; D2 `0x91/0x66`, `0x91/0x52` | generated note press/release from official PDF fader-start behavior | deck-local generated automation | none | none | Explicitly ignored by product decision; S3 must not emit PLAY, CUE, or any other semantic event | Host-tested as ignored; fader-start playback automation not implemented |
| Cue + Shift / track start | `0x90/0x48`, `0x91/0x48` | press/release | shifted deck-local | `CTRL_ID_DECK1_TO_START`, `CTRL_ID_DECK2_TO_START` | `deck_core` seek | Implemented | Verified end-to-end 2026-06-20 / 2026-06-21 |
| Jog platter scratch | `0xB0/0x22`, `0xB1/0x22` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_SCRATCH`, `CTRL_ID_DECK2_JOG_SCRATCH` | `deck_core` / audio seek | Implemented MVP input | Verified 2026-06-14 |
| Jog platter bend | `0xB0/0x23`, `0xB1/0x23` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_BEND`, `CTRL_ID_DECK2_JOG_BEND` | `deck_core` / tempo bend | Implemented MVP input | Verified 2026-06-14 |
| Jog side bend | `0xB0/0x21`, `0xB1/0x21` | relative/encoder CC | deck-local | `CTRL_ID_DECK1_JOG_BEND`, `CTRL_ID_DECK2_JOG_BEND` | `deck_core` / tempo bend | Implemented MVP input | Verified 2026-06-14 |
| Jog touch | `0x90/0x36`, `0x91/0x36` | press/release | deck-local | `CTRL_ID_DECK1_JOG_TOUCH`, `CTRL_ID_DECK2_JOG_TOUCH` | `deck_core` jog mode | Implemented MVP input | Verified 2026-06-14 |
| Jog + Shift search | `0xB0/0x29`, `0xB1/0x29` | relative/encoder CC; `0x40` neutral, above/below center is signed delta | shifted deck-local | `CTRL_ID_DECK1_JOG_SEARCH`, `CTRL_ID_DECK2_JOG_SEARCH` | `deck_core` relative seek, 1000 ms per encoder step | Implemented | Host-tested from XML; hardware smoke passed D1/D2 2026-07-02 |
| Jog touch + Shift highspeed | `0x90/0x67`, `0x91/0x67` | press/release | shifted deck-local | `CTRL_ID_DECK1_JOG_SEARCH_TOUCH`, `CTRL_ID_DECK2_JOG_SEARCH_TOUCH` | reserved jog-search touch/highspeed state | Mapped only | Host-tested from XML; hardware smoke passed with Jog Search slice 2026-07-02 |
| Tempo fader | D1 `0xB0/0x00+0x20`, D2 `0xB1/0x00+0x20` | 14-bit MSB+LSB | deck-local | `CTRL_ID_DECK1_TEMPO`, `CTRL_ID_DECK2_TEMPO` | audio pitch/resampler | Implemented with selected tempo range | Verified 2026-06-14; range behavior smoke passed 2026-06-25 |
| Beat Sync | `0x90/0x58`, `0x91/0x58` | press/release in XML; official list notes Beat Sync is sent on button release rather than press | deck-local | `CTRL_ID_DECK1_SYNC`, `CTRL_ID_DECK2_SYNC` | beat/sync model | Implemented: BPM match to the other deck using precise ANLZ BPM when available, internally clamped to ±20%; one-shot phase-align seek to a matching beat while preserving the reference deck's signed intra-beat offset when both beatgrids are available, including while the target deck is playing | Verified 2026-06-21; BPM-match behavior smoke passed 2026-06-25; playing-deck phase-align and waveform beat-match-line alignment hardware smoke passed 2026-07-01; continuous following not implemented |
| Beat Sync long press / master | `0x90/0x5C`, `0x91/0x5C` | press/release or long-press semantic | deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_SYNC_MASTER` | beat/sync model | Implemented | Host-tested from XML; hardware smoke pending |
| Beat Sync + Shift / tempo range | `0x90/0x60`, `0x91/0x60` | press/release | shifted deck-local | `CTRL_ID_DECK1_TEMPO_RANGE`, `CTRL_ID_DECK2_TEMPO_RANGE` | deck settings | Implemented: cycles `±6%`, `±10%`, `±16%` per deck; default `±10%` | Verified 2026-06-20 / 2026-06-21; range behavior smoke passed 2026-06-25 |
| Loop In / 4 Beat | `0x90/0x10`, `0x91/0x10` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_IN`, `CTRL_ID_DECK2_LOOP_IN` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior smoke passed 2026-06-21 |
| Loop Out | `0x90/0x11`, `0x91/0x11` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_OUT`, `CTRL_ID_DECK2_LOOP_OUT` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior smoke passed 2026-06-21 |
| Reloop/Exit | `0x90/0x4D`, `0x91/0x4D` | press/release | deck-local | `CTRL_ID_DECK1_RELOOP_EXIT`, `CTRL_ID_DECK2_RELOOP_EXIT` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior smoke passed 2026-06-21 |
| Reloop/Exit + Shift | `0x90/0x50`, `0x91/0x50` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_RELOOP_STOP` | `deck_core` loop stop/forget | Implemented | Host-tested from XML; hardware smoke pending |
| Shift + Loop In adjust | `0x90/0x4C`, `0x91/0x4C` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_LOOP_ADJUST_IN` | loop boundary edit | Implemented | Host-tested from XML; hardware smoke pending |
| Shift + Loop Out adjust | XML: `0x90/0x4E`, `0x91/0x4E`; official list input says D2 `0x91/0x4F` but official output says D2 `0x91/0x4E` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_LOOP_ADJUST_OUT` | loop boundary edit | Implemented from XML `0x4E` | Host-tested from XML; D2 `0x4E`/`0x4F` smoke pending |
| Cue/Loop Call Left / halve loop | `0x90/0x51`, `0x91/0x51` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_HALVE`, `CTRL_ID_DECK2_LOOP_HALVE` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior smoke passed 2026-06-21 |
| Cue/Loop Call Right / double loop | `0x90/0x53`, `0x91/0x53` | press/release | deck-local | `CTRL_ID_DECK1_LOOP_DOUBLE`, `CTRL_ID_DECK2_LOOP_DOUBLE` | `deck_core` loop | Implemented | Verified D1/D2 2026-06-21; P4 behavior smoke passed 2026-06-21 |
| Cue/Loop Call Left + Shift / jump back | `0x90/0x3E`, `0x91/0x3E` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_BACK`, `CTRL_ID_DECK2_BEAT_JUMP_BACK` | `deck_core` beat jump | Implemented | Verified 2026-06-21; behavior smoke pending |
| Cue/Loop Call Right + Shift / jump forward | `0x90/0x3D`, `0x91/0x3D` | press/release | shifted deck-local | `CTRL_ID_DECK1_BEAT_JUMP_FORWARD`, `CTRL_ID_DECK2_BEAT_JUMP_FORWARD` | `deck_core` beat jump | Implemented | Verified 2026-06-21; behavior smoke pending |
| Shift + channel CUE / quantize | `0x90/0x68`, `0x91/0x68` | press/release | shifted deck-local | `CTRL_ID_DECK*_EXT_ACTION` / `CTRL_DECK_EXT_ACTION_QUANTIZE` | deck quantize state | Implemented for loop in/out/adjust | Host-tested from XML; hardware smoke pending |

### Mixer, Monitoring, And Effects Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Channel fader | D1 `0xB0/0x13+0x33`, D2 `0xB1/0x13+0x33` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_VOLUME`, `CTRL_ID_CH2_VOLUME` | audio mixer | Implemented | Verified 2026-06-14 |
| Crossfader | `0xB6/0x1F+0x3F` | 14-bit MSB+LSB | global mixer | `CTRL_ID_CROSSFADER` | audio mixer | Implemented | Verified 2026-06-14 |
| Master Level | `0xB6/0x08+0x28` from official MIDI PDF | 14-bit MSB+LSB | global mixer | `CTRL_ID_MASTER_VOLUME` | audio mixer | Implemented | Host-tested and hardware smoke OK 2026-07-01 |
| Trim / pregain | D1 `0xB0/0x04+0x24`, D2 `0xB1/0x04+0x24` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_TRIM`, `CTRL_ID_CH2_TRIM` | audio mixer | Implemented | Verified 2026-06-21; P4 pregain DSP host-tested; hardware smoke OK 2026-07-01 |
| EQ High | D1 `0xB0/0x07+0x27`, D2 `0xB1/0x07+0x27` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_HIGH`, `CTRL_ID_CH2_EQ_HIGH` | EQ/DSP | Implemented | Verified 2026-06-21; DSP implemented in P4 |
| EQ Mid | D1 `0xB0/0x0B+0x2B`, D2 `0xB1/0x0B+0x2B` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_MID`, `CTRL_ID_CH2_EQ_MID` | EQ/DSP | Implemented | Verified 2026-06-21; DSP implemented in P4 |
| EQ Low | D1 `0xB0/0x0F+0x2F`, D2 `0xB1/0x0F+0x2F` | 14-bit MSB+LSB | deck-local | `CTRL_ID_CH1_EQ_LOW`, `CTRL_ID_CH2_EQ_LOW` | EQ/DSP | Implemented | Verified 2026-06-21; DSP implemented in P4 |
| Headphone cue/PFL | `0x90/0x54`, `0x91/0x54` | press/release | deck-local | `CTRL_ID_DECK1_PFL`, `CTRL_ID_DECK2_PFL` | mixer/cue routing | Implemented | Verified 2026-06-14 / 2026-06-20 |
| Headphones mix | `0xB6/0x0C+0x2C` | 14-bit MSB+LSB | global monitor | `CTRL_ID_HEADPHONE_MIX` | cue routing/settings | Implemented | Verified 2026-06-21; P4 monitor/headphone DSP host-tested 2026-07-01; FLX4 USB headphones hardware smoke passed 2026-07-07 |
| Headphones level | `0xB6/0x0D+0x2D` from official MIDI PDF | 14-bit MSB+LSB | global monitor output | `CTRL_ID_HEADPHONE_LEVEL` | headphone/monitor output gain | Implemented | Host-tested from official PDF; FLX4 USB headphones hardware smoke passed 2026-07-07 |
| Master Cue | official MIDI list/PDF: normal `0x96/0x63`, shifted `0x96/0x78`; LED output `0x96/0x63` | press/release; press toggles, release ignored | global monitor | `CTRL_ID_MASTER_CUE` / `LED_MASTER_CUE` | P4 monitor master-cue gate + reconnect-safe LED snapshot | Implemented | Host-tested from official MIDI list; hardware smoke passed 2026-07-02; shifted address corrected from `0x96/0x68` to official PDF `0x96/0x78` |
| Filter CH1 / CH2 | CH1 `0xB6/0x17+0x37`, CH2 `0xB6/0x18+0x38` | 14-bit MSB+LSB | channel-specific global CC | `CTRL_ID_CH1_FILTER`, `CTRL_ID_CH2_FILTER` | filter/DSP | Implemented behind Smart CFX | Verified 2026-06-21; DSP behavior enabled when Smart CFX is on |
| Smart CFX | `0x96/0x00` | press/release | global | `CTRL_ID_SMART_CFX` | P4 Smart CFX filter DSP + LED | Implemented with softened macro curve and balanced HI side | Verified 2026-06-20 input/LED address; curve host-tested 2026-07-01; HI/LOW hardware DSP smoke passed 2026-07-01 |
| Smart Fader | `0x96/0x01` | press/release | global | `CTRL_ID_SMART_FADER` | P4 Smart Fader transition assist + LED | Implemented | Verified 2026-06-20 input/LED address; hardware behavior smoke passed 2026-07-01 |
| Shift + Smart CFX | official MIDI list/PDF: `0x96/0x08` | press/release | shifted global | `CTRL_ID_SMART_CFX_SHIFT` | future Smart CFX alternate behavior | Mapped only; P4 consumes press/release as no-op placeholder | Host-tested from official PDF; hardware smoke pending |
| Shift + Smart Fader | official MIDI list/PDF: `0x96/0x09` | press/release | shifted global | `CTRL_ID_SMART_FADER_SHIFT` | future Smart Fader alternate behavior | Mapped only; P4 consumes press/release as no-op placeholder | Host-tested from official PDF; hardware smoke pending |
| Beat FX select next / previous | `0x94/0x63`, `0x94/0x64` | press/release | FX section | `CTRL_ID_BEAT_FX_SELECT_NEXT`, `CTRL_ID_BEAT_FX_SELECT_PREV` | P4 Beat FX state model | Implemented state/mapping; cycle `FILTER → ECHO → FLANGER → DELAY → FILTER`, reverse for Previous, no selectable `NONE` | Input mapping hardware smoke passed 2026-07-01; DELAY selection/DSP hardware smoke pending |
| Beat FX beat left / right | `0x94/0x4A`, `0x94/0x4B` | press/release | FX section | `CTRL_ID_BEAT_FX_BEAT_DEC`, `CTRL_ID_BEAT_FX_BEAT_INC` | P4 Beat FX state model | Implemented state/mapping | Host-tested from XML; hardware smoke passed 2026-07-01 |
| Beat FX beat left / right + Shift | official MIDI PDF: `0x94/0x66`, `0x94/0x6B` | press/release; press steps two beat-size enum positions with min/max saturation, release ignored by P4 | shifted FX section | `CTRL_ID_BEAT_FX_BEAT_DEC_SHIFT`, `CTRL_ID_BEAT_FX_BEAT_INC_SHIFT` | P4 Beat FX state model | Implemented state/mapping | Host-tested from official PDF; hardware smoke pending |
| Beat FX channel select | CH1 `0x94/0x10`, CH2 `0x95/0x11` | stateful semantic target select; both active maps to `1&2` | FX channel selector | `CTRL_ID_BEAT_FX_TARGET` | P4 Beat FX state model | Implemented state/mapping | Host-tested from XML; hardware smoke passed 2026-07-01 for CH1/CH2/1&2 |
| Beat FX level/depth | `0xB4/0x02` | 7-bit CC MSB in XML | FX section | `CTRL_ID_BEAT_FX_DEPTH` | P4 Beat FX state model | Implemented state/mapping | Host-tested from XML; hardware smoke passed 2026-07-01 |
| Beat FX on/off | CH1/global `0x94/0x47`, CH2 `0x95/0x47` | press/release; press toggles P4 Beat FX enabled state | FX channel selector | `CTRL_ID_BEAT_FX_ON` / `LED_BEAT_FX_ON` | P4 Beat FX state model + LED feedback | Implemented state/mapping; FILTER, damped multi-repeat ECHO, FLANGER, and one-shot full-band DELAY (`4`) DSP; DELAY Level/Depth controls wet gain; ON/OFF LED follows P4 state | Host-tested from XML; FILTER/Echo hardware smoke and BPM-sync/beat-size smoke passed 2026-07-01; DELAY hardware smoke pending |
| Beat FX on/off + Shift | CH1/global `0x94/0x43`, CH2 `0x95/0x43` | press/release | shifted FX channel selector | `CTRL_ID_BEAT_FX_CLEAR` | P4 Beat FX state reset | Implemented state/mapping; resets Beat FX selection/parameters and deactivates FILTER/ECHO/FLANGER/DELAY, while ECHO/DELAY may finish their bounded audio tail | Host-tested from XML; existing reset hardware smoke passed 2026-07-01; DELAY reset smoke pending |

Snapshot/reconnect note: S3 now replays the last known absolute input values
for the mixer/monitoring/effect-depth rows above after a heartbeat-driven FLX4
connection refresh. This is a known-value cache, not a physical USB query:
unknown controls are skipped, tempo faders are excluded, and buttons/toggles
are not replayed.

### Performance Pad Mode Inventory

The DDJ-FLX4 has four direct physical pad mode buttons: `HOT CUE`, `PAD FX1`,
`BEAT JUMP`, and `SAMPLER`. Secondary modes are reached with `SHIFT` plus one
of those four buttons. The Mixxx XML still lists each secondary mode as its own
MIDI note, so this table keeps the XML addresses but labels whether the control
is direct or shifted.

Project scope decision 2026-07-07: `Keyboard/Stems`, `Sampler`, and `Key Shift`
are excluded from DDJ-FFL4 standalone behavior. Their XML addresses remain
documented for trace analysis, and the `control_link` numeric constants are kept
stable for compatibility, but S3 ignores those input messages and P4 ignores
stale/manual events for those modes.

For Pad FX performance-pad inputs, `docs/reference/DDJ-FLX4_MIDI_message_List_E1.pdf`
is the source of truth because the Mixxx XML reference does not expose the
complete Pad FX pad-action range. The official list defines Pad FX1 pads as
notes `0x10..0x17` and Pad FX2 pads as notes `0x50..0x57` on the existing Deck
1/Deck 2 pad statuses (`0x97`/`0x99`), with `0x00` off and `0x7F` on.

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Hot Cue mode | `0x90/0x1B`, `0x91/0x1B` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_HOT_CUE`, `CTRL_ID_DECK2_PAD_MODE_HOT_CUE` | P4 pad mode state | Implemented state | Verified 2026-06-21; LED/state smoke passed 2026-06-21 |
| Shift + Hot Cue / Keyboard-Stems mode | `0x90/0x69`, `0x91/0x69` | press/release | shifted deck-local mode select | retained compatibility ID only | none | Out of scope / ignored | XML address verified 2026-06-21; product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Pad FX1 mode | `0x90/0x1E`, `0x91/0x1E` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX1`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX1` | P4 pad mode state / Pad FX DSP model | Implemented state + DSP path | Mode verified D1/D2 2026-06-21; pad input ranges host-tested from official PDF |
| Shift + Pad FX1 / Pad FX2 mode | `0x90/0x6B`, `0x91/0x6B` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_PAD_FX2`, `CTRL_ID_DECK2_PAD_MODE_PAD_FX2` | P4 pad mode state / Pad FX DSP model | Implemented state + DSP path | Mode verified D1/D2 2026-06-21; Pad FX2 input ranges host-tested from official PDF |
| Beat Jump mode | `0x90/0x20`, `0x91/0x20` | press/release | deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_BEAT_JUMP`, `CTRL_ID_DECK2_PAD_MODE_BEAT_JUMP` | P4 pad mode state | Implemented state | Verified 2026-06-21; LED/state smoke passed 2026-06-21 |
| Shift + Beat Jump / Beat Loop mode | `0x90/0x6D`, `0x91/0x6D` | press/release | shifted deck-local mode select | `CTRL_ID_DECK1_PAD_MODE_BEAT_LOOP`, `CTRL_ID_DECK2_PAD_MODE_BEAT_LOOP` | P4 pad mode state | Implemented state | Verified D1/D2 2026-06-21; normal and shifted Beat Loop behavior smoke passed 2026-07-01 |
| Sampler mode | `0x90/0x22`, `0x91/0x22` | press/release | deck-local mode select | retained compatibility ID only | none | Out of scope / ignored | XML address verified 2026-06-21; product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Shift + Sampler / Key Shift mode | `0x90/0x6F`, `0x91/0x6F` | press/release | shifted deck-local mode select | retained compatibility ID only | none | Out of scope / ignored | XML address verified 2026-06-21; product support removed 2026-07-07; inert smoke passed 2026-07-07 |

### Performance Pad Action Inventory

| Physical control | XML status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Hot Cue pads 1-8 | D1 `0x97/0x00..0x07`, D2 `0x99/0x00..0x07` | press/release | active Hot Cue mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 hot cue state | Implemented behavior | Verified D1/D2 pads 1-8 2026-06-21; D1 set/recall behavior smoke pass 2026-06-21 |
| Hot Cue clear pads 1-8 | D1 `0x98/0x00..0x07`, D2 `0x9A/0x00..0x07` | press/release | shifted Hot Cue mode | same pad action ID with shift flag | P4 hot cue state | Implemented behavior | D1 shifted clear behavior smoke pass 2026-06-21; D2 shifted clear behavior smoke pass 2026-06-26 |
| Pad FX1 pads 1-8 | D1 `0x97/0x10..0x17`, D2 `0x99/0x10..0x17` | press `0x7F`, release `0x00` | active Pad FX1 mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 Pad FX DSP slice | Implemented from official PDF | Host-tested from official PDF; hardware smoke passed 2026-07-01 for filter/echo pad behavior and Echo release tail |
| Pad FX2 pads 1-8 | D1 `0x97/0x50..0x57`, D2 `0x99/0x50..0x57` | press `0x7F`, release `0x00` | active Pad FX2 mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | P4 Pad FX DSP slice | Implemented from official PDF | Host-tested from official PDF; hardware smoke passed 2026-07-01 for filter/echo pad behavior and Echo release tail |
| Keyboard/Stems pads 1-8 | D1 `0x97/0x40..0x47`, D2 `0x99/0x40..0x47` | press/release | active Keyboard mode | none | none | Out of scope / ignored | Product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Keyboard/Stems shifted pads 1-8 | D1 `0x98/0x40..0x47`, D2 `0x9A/0x40..0x47` | press/release | shifted Keyboard mode | none | none | Out of scope / ignored | Product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Beat Loop pads 1-8 | D1 `0x97/0x60..0x67`, D2 `0x99/0x60..0x67` | press/release | active Beat Loop mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | `deck_core` beat loop | Implemented | Verified D1/D2 pads 1-8 2026-06-21; hardware behavior smoke passed 2026-07-01 on both decks |
| Shifted Beat Loop pads 1-8 | D1 `0x98/0x60..0x67`, D2 `0x9A/0x60..0x67` | press/release | shifted Beat Loop mode | same pad action ID with shift flag | `deck_core` momentary beat loop | Implemented | Hardware behavior smoke passed 2026-07-01 |
| Beat Jump pads 1-8 | D1 `0x97/0x20..0x27`, D2 `0x99/0x20..0x27` | press/release | active Beat Jump mode | `CTRL_ID_DECK1_PAD_ACTION`, `CTRL_ID_DECK2_PAD_ACTION` with mode+pad | `deck_core` beat jump | Implemented | Verified D1 pads 1-8 and D2 pads 1,3-8 2026-06-21; hardware behavior smoke passed 2026-07-01 on both decks |
| Beat Jump shifted size pads | D1 `0x98/0x26..0x27`, D2 `0x9A/0x26..0x27` | press/release | shifted Beat Jump mode | pad action with size inc/dec | future beat jump size state | Deferred | Not captured |
| Sampler pads 1-8 left/right | left `0x97/0x30..0x37`, right `0x99/0x30..0x37` | press/release | active Sampler mode | none | none | Out of scope / ignored | XML address verified 2026-06-21; product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Sampler shifted pads 1-8 left/right | left `0x98/0x30..0x37`, right `0x9A/0x30..0x37` | press/release | shifted Sampler mode | none | none | Out of scope / ignored | Product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Key Shift pads 1-8 | D1 `0x97/0x70..0x77`, D2 `0x99/0x70..0x77` | press/release | active Key Shift mode | none | none | Out of scope / ignored | XML address verified 2026-06-21; product support removed 2026-07-07; inert smoke passed 2026-07-07 |
| Key Shift shifted pads 1-8 | D1 `0x98/0x70..0x77`, D2 `0x9A/0x70..0x77` | press/release | shifted Key Shift mode | none | none | Out of scope / ignored | Product support removed 2026-07-07; inert smoke passed 2026-07-07 |

### Candidate LED Output Inventory

| LED/output group | Output status/midino | Source state | S3/P4 state owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- |
| Play LEDs | `0x90/0x0B`, `0x91/0x0B` | deck playing | P4 `deck_core` | Implemented | Verified 2026-06-20 reconnect |
| Play + Shift / Censor LEDs | official list/PDF output: `0x90/0x0E`, `0x91/0x0E` | P4 `deck_state_t.censor_active` | P4 `deck_core` snapshot | Implemented snapshot output | Host-tested from official packet; hardware smoke passed 2026-07-07 |
| Play/Cue shifted alternate LEDs | XML output: `0x90/0x47`, `0x91/0x47` | Mixxx maps both play/cue indicators here | Not defined for DDJ-FFL4 | Deferred | Not captured |
| Cue LEDs | `0x90/0x0C`, `0x91/0x0C` | cue state | P4 `deck_core` | Implemented | Verified 2026-06-20 reconnect |
| Cue + Shift / track-start LEDs | official list/PDF output: `0x90/0x48`, `0x91/0x48` | track-start action acknowledgement is momentary | P4 `deck_core` action sends on/off flash | Implemented momentary output | Host-tested packet mapping and P4 action flash; post-flash FLX4 behavior smoke passed 2026-07-07 |
| Beat Sync LEDs | `0x90/0x58`, `0x91/0x58` | P4 sync-enabled state | P4 `deck_core.sync_enabled` | Implemented output | Hardware smoke passed 2026-06-21; output probe verified 2026-06-20 |
| PFL LEDs | `0x90/0x54`, `0x91/0x54` | PFL enabled | P4 mixer/cue routing | Implemented | Verified 2026-06-20 reconnect |
| Master Cue LED | official MIDI list: `0x96/0x63` | P4 monitor master-cue enabled state | P4 `audio_engine` / `deck_core` snapshot | Implemented output | Host-tested; hardware smoke passed 2026-07-02 |
| Pad mode LEDs | supported: Hot Cue `0x1B`, Pad FX1 `0x1E`, Beat Jump `0x20`, shifted Pad FX2 `0x6B`, shifted Beat Loop `0x6D`; unsupported mode LEDs remain OFF: Sampler `0x22`, Keyboard `0x69`, Key Shift `0x6F` | selected supported controller pad mode | P4 `deck_core.pad_mode` | Implemented output for supported modes only | Hardware smoke passed 2026-06-21; post-removal smoke passed 2026-07-07 |
| Loop In LEDs | `0x90/0x10`, `0x91/0x10` | loop-in marker exists or active audio loop exists | P4 `deck_core` pending loop-in marker plus `audio_engine` loop state | Implemented output | Active-loop smoke passed D1/D2 2026-06-21; output probe verified 2026-06-20; Loop In marker LED smoke passed D1/D2 2026-06-26 |
| Shift + Loop In adjust LEDs | official list/PDF output: `0x90/0x4C`, `0x91/0x4C` | loop-adjust action acknowledgement is momentary | P4 loop edit action sends on/off flash | Implemented momentary output | Host-tested packet mapping and P4 action flash; post-flash FLX4 behavior smoke passed 2026-07-07 |
| Loop Out LEDs | `0x90/0x11`, `0x91/0x11` | active audio loop exists | P4 `audio_engine` loop state | Implemented output | Behavior smoke passed D1/D2 2026-06-21; output probe verified 2026-06-20; remains off while only Loop In marker is pending |
| Shift + Loop Out adjust LEDs | official list/PDF output: `0x90/0x4E`, `0x91/0x4E` | loop-adjust action acknowledgement is momentary | P4 loop edit action sends on/off flash | Implemented momentary output | Host-tested packet mapping and P4 action flash from official output `0x4E`; post-flash FLX4 behavior smoke passed 2026-07-07 |
| Hot Cue pad LEDs | normal D1 `0x97/0x00..0x07`, normal D2 `0x99/0x00..0x07`; shifted mirror D1 `0x98/0x00..0x07`, D2 `0x9A/0x00..0x07` | hot cue slot exists while Hot Cue mode is selected | P4 hot cue store state | Implemented output for normal pad LEDs; shifted mirror LEDs deferred | Host-tested from official PDF/XML-derived ranges; hardware LED smoke passed 2026-07-01 |
| Pad FX1 pad LEDs | normal D1 `0x97/0x10..0x17`, normal D2 `0x99/0x10..0x17`; shifted mirror D1 `0x98/0x10..0x17`, D2 `0x9A/0x10..0x17` | momentary active Pad FX1 pad while Pad FX1 mode is selected | P4 `deck_core` Pad FX momentary state | Implemented output for normal pad LEDs; shifted mirror LEDs deferred | Host-tested from official PDF; hardware LED smoke passed 2026-07-01 |
| Keyboard/Stems pad LEDs | official list pad-8 example: normal D1 `0x97/0x47`, normal D2 `0x99/0x47`; shifted mirror D1 `0x98/0x47`, D2 `0x9A/0x47` | none | none | Out of scope | Product support removed 2026-07-07 |
| Pad FX2 pad LEDs | normal D1 `0x97/0x50..0x57`, normal D2 `0x99/0x50..0x57`; shifted mirror D1 `0x98/0x50..0x57`, D2 `0x9A/0x50..0x57` | momentary active Pad FX2 pad while Pad FX2 mode is selected | P4 `deck_core` Pad FX momentary state | Implemented output for normal pad LEDs; shifted mirror LEDs deferred | Host-tested from official PDF; hardware LED smoke passed 2026-07-01 |
| Beat Loop pad LEDs | normal D1 `0x97/0x60..0x67`, normal D2 `0x99/0x60..0x67`; shifted mirror D1 `0x98/0x60..0x67`, D2 `0x9A/0x60..0x67` | P4-owned active Beat Loop pad while Beat Loop mode is selected | P4 loop state, selected Beat Loop pad, and pad mode | Implemented output for normal pad LEDs; shifted mirror LEDs deferred | Hardware LED smoke passed 2026-07-01 on both decks; fixed to track P4 pad state instead of 120-BPM duration inference |
| Beat Jump pad LEDs | normal D1 `0x97/0x20..0x27`, normal D2 `0x99/0x20..0x27`; shifted mirror D1 `0x98/0x20..0x27`, D2 `0x9A/0x20..0x27` | P4 `audio_engine_deck_status_t.loaded` while Beat Jump mode is selected | P4 `deck_core` publishes normal pad LED diff and reconnect refresh | Implemented output for normal pad LEDs; shifted mirror LEDs outside helper 7/8 deferred | Host-tested packet mapping and P4 loaded-state/mode publish; post-flash FLX4 smoke passed 2026-07-07: all 8 normal Beat Jump pads lit |
| Beat Jump shifted helper LEDs | D1 `0x98/0x26..0x27`, D2 `0x9A/0x26..0x27` | P4 `audio_engine_deck_status_t.loaded` while Beat Jump mode is selected and deck Shift is held | P4 `deck_core` publishes helper 7/8 LED diff and reconnect refresh | Implemented shifted helper output | Host-tested packet mapping and P4 loaded-state/mode/shift publish; post-flash FLX4 smoke passed 2026-07-07 |
| Sampler pad LEDs | left normal `0x97/0x30..0x37`, left shifted `0x98/0x30..0x37`, right normal `0x99/0x30..0x37`, right shifted `0x9A/0x30..0x37` | none | none | Out of scope | Product support removed 2026-07-07 |
| Key Shift pad LEDs | official list pad-8 example: normal D1 `0x97/0x77`, normal D2 `0x99/0x77`; shifted mirror D1 `0x98/0x77`, D2 `0x9A/0x77` | none | none | Out of scope | Product support removed 2026-07-07 |
| Loaded / Track Load Illumination | official list/PDF output: D1 `0x9F/0x00`, D2 `0x9F/0x01` | P4 `audio_engine_deck_status_t.loaded` | P4 `deck_core` publishes load-state LED diff and reconnect refresh | Implemented state-driven output | Host-tested packet mapping and P4 loaded-state publish; post-flash FLX4 behavior smoke passed 2026-07-07. Known cosmetic issue: a brief one-time pad sweep can appear around the initial D1/D2 load sequence; 2026-07-13 capture confirmed correct deck MIDI routing and no functional Pad FX/playback state change. |
| Channel 1 VU meter (5 LEDs) | `0xB0/0x02` | channel 1 level (0-127) | P4 audio peak timer | Implemented output | Not captured |
| Channel 2 VU meter (5 LEDs) | `0xB1/0x02` | channel 2 level (0-127) | P4 audio peak timer | Implemented output | Not captured |

### Official Settings / Device Mode Inventory

These rows come from `DDJ-FLX4_MIDI_message_List.md`. They are controller
configuration messages rather than normal performance controls. Do not map them
to live P4 behavior without hardware smoke because some official-list rows
overlap with XML live-control addresses.

| Setting | Official status/midino | Encoding | Deck/shift | Semantic ID | P4 owner | Status | HW verification |
| --- | --- | --- | --- | --- | --- | --- | --- |
| Vinyl mode on/off | official list: D1 `0xB0/0x17`, D2 `0xB0/0x17` | CC value `0x00` off, `0x7F` on; default on | deck setting; official note says it is changed by MIDI OUT from DJ app, not from unit | none | none | Out of scope by product decision 2026-07-02 | Do not implement unless the product decision changes |

## Hardware Acceptance

The MVP capture is complete. For each additional delivered control group,
capture and verify:

- button press/release values and deck/shift status;
- relative encoder direction and acceleration range;
- 14-bit analog minimum, center, maximum, and MSB/LSB order;
- mode-dependent pad messages;
- LED output values and reconnect resynchronization.

Hardware smoke passed for the 2026-07-02 Jog Search / Master Cue slice:

- Shift + Jog rotate on Deck 1 and Deck 2 seeks by one-second steps in both
  directions and clamps at track start.
- MASTER CUE toggles only the monitor/headphone master contribution; RCA/main
  output must not change.
- MASTER CUE LED follows P4 state and recovers after FLX4 reconnect, S3 reset,
  and P4 reset snapshot refresh.
