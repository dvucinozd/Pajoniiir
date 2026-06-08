# DDJ-FLX4 MIDI Map

Source file:
[docs/reference/Pioneer-DDJ-FLX4.midi.xml](reference/Pioneer-DDJ-FLX4.midi.xml)

The XML is a Mixxx controller preset. It is a strong mapping reference, not a
hardware datasheet. Every MVP control must still be verified with a raw MIDI
capture from the physical DDJ-FLX4 connected to the S3 USB host.

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
| Beat Sync | Deck 1 | `0x90` | `0x58` | document only for MVP; implementation later |
| Beat Sync | Deck 2 | `0x91` | `0x58` | document only for MVP; implementation later |
| Load | Deck 1 | `0x96` | `0x46` | global button status, deck from midino |
| Load | Deck 2 | `0x96` | `0x47` | global button status, deck from midino |
| Browse rotate | Library | `0xB6` | `0x40` | relative encoder |
| Browse press | Library | `0x96` | `0x41` | focus/select action |

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
```

## Verification Required

Before implementing behavior, capture raw MIDI for:

- Play/Cue press and release on both decks.
- Jog rotate slow/fast in both directions.
- Tempo fader full range and center.
- Channel faders and crossfader full range.
- Headphone cue buttons.
- FLX4 LED feedback for Play/Cue/PFL if available in the XML/script pair.
