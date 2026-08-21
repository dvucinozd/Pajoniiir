# Hercules DJControl Inpulse 500 MIDI Map

Status: host-qualified Pajoniiir controller profile; physical Inpulse 500 MIDI,
LED, reconnect and USB-audio acceptance is still pending.

Authoritative mapping sources:

- [Hercules DJControl Inpulse 500 MIDI Commands](https://ts.hercules.com/download/sound/manuals/DJC_Inpulse500/DJControlInpulse500_MIDI_Commands.pdf)
- [Hercules product support](https://support.hercules.com/en/product/djcontrolinpulse500-en/)
- [Serato DJControl Inpulse 500 quick-start guide](https://support.serato.com/hc/en-us/articles/10173750138639-Hercules-DJControl-Inpulse-500-Quickstart-Guide)

The committed source and compiled fixtures are
`controllers/hercules_djcontrol_inpulse_500/profile.json` and
`profile.s3bin`. The fixture is regenerated and exercised by both host suites.

## Identity and scope

| Parameter | Profile value | Status |
| --- | --- | --- |
| USB VID:PID | `06F8:B12B` | Matches published Inpulse 500 hardware IDs; confirm from the S3 descriptor report on the target unit |
| Decks | 2 | Profile/runtime verified |
| MIDI input/output | Enabled | Host verified; hardware pending |
| Jog touch | Enabled | Host/profile path verified; hardware pending |
| Pitch | 14-bit CC | Host/profile path verified; hardware pending |
| USB audio | 4 channels: MAIN 1/2, headphones 3/4 | Device capability is documented, but Pajoniiir routing is not hardware-qualified for Hercules |

`06F8:B105` belongs to a different Hercules product family and must not match
this profile. Exact VID/PID matching is intentional.

## MIDI channels

| Section | Normal Note / CC | Shift Note / CC |
| --- | --- | --- |
| Deck 1 | `0x91` / `0xB1` | `0x94` / `0xB4` |
| Deck 2 | `0x92` / `0xB2` | `0x95` / `0xB5` |
| Mixer/global | `0x90` / `0xB0` | `0x93` / `0xB3` |
| Deck 1 pads | `0x96` | same channel, shifted note range |
| Deck 2 pads | `0x97` | same channel, shifted note range |

## Deck controls

Addresses below show Deck 1; Deck 2 uses `0x92/0x95/0xB2/0xB5` in the same
positions.

| Physical control | MIDI | Pajoniiir semantic | Behaviour |
| --- | --- | --- | --- |
| Play/Pause | `91 07` | `deckN.play` | Transport toggle |
| Shift+Play | `94 07` | not mapped | Native stutter-play has no Pajoniiir semantic; it must not toggle normal Play |
| Cue | `91 06` | `deckN.cue` | Cue |
| Shift+Cue | `94 06` | `deckN.to_start` | Standalone return-to-start adaptation |
| Shift | `91 04` | `deckN.shift` | Modifier state |
| Sync | `91 05` | `deckN.sync` | Toggle Beat Sync |
| Shift+Sync | `94 05` | `deckN.ext_action(sync_off)` | Idempotently disable Sync |
| Quantize | `91 02` | `deckN.ext_action(quantize)` | Toggle Quantize |
| Shift+Quantize | `94 02` | `deckN.tempo_range` | Cycle tempo range |
| Loop In / Out | `91 09` / `91 0A` | `deckN.loop_in/out` | Set loop bounds |
| Shift+Loop In / Out | `94 09` / `94 0A` | `loop_adjust_in/out` | Adjust active loop bounds |
| Autoloop push | `91 2C` | `deckN.reloop_exit` | Reloop/Exit |
| Shift+Autoloop push | `94 2C` | `reloop_stop` | Stop and forget active/remembered loop |
| Autoloop rotate | `B1 0E` | `deckN.loop_size` | Two's-complement relative; left halves, right doubles active loop |
| Shift+Autoloop rotate | `B4 0E` | `deckN.loop_size` | Same function while Shift is held |
| Tempo fader | MSB `B1 08`, LSB `B1 28` | `deckN.tempo` | 14-bit absolute |
| Jog touch / Shift+touch | `91 08` / `94 08` | `jog_touch` / `jog_search_touch` | Scratch hold / search touch |
| Jog scratch / bend | `B1 0A` / `B1 09` | `jog_scratch` / `jog_bend` | Two's-complement relative |
| Shift+jog | `B4 09` and `B4 0A` | `jog_search` | Fast search |
| PFL / Shift+PFL | `91 0C` / `94 0C` | `deckN.pfl` | Deck cue monitor |

## Performance modes and pads

The physical selectors use notes `0x0F` through `0x14` for modes 1 through 6.
Pajoniiir deliberately adapts modes 3, 4 and 6 to its available performance
engine. Mode 5 selects Key/Pitch Play, but P4 currently ignores that mode and
its pads are intentionally unmapped.

| Selector | Note | Pajoniiir mode | Pad notes normal / shifted |
| --- | --- | --- | --- |
| Mode 1 | `0x0F` | Hot Cue | `0x00..0x07` / `0x08..0x0F` |
| Mode 2 | `0x10` | Beat Loop | `0x10..0x17` / `0x18..0x1F` |
| Mode 3 | `0x11` | Pad FX 1 | `0x20..0x27` / `0x28..0x2F` |
| Mode 4 | `0x12` | Beat Jump | `0x30..0x37` / `0x38..0x3F` |
| Mode 5 | `0x13` | Key/Pitch Play selector only | `0x40..0x4F` intentionally unmapped |
| Mode 6 | `0x14` | Pad FX 2 | `0x50..0x57` / `0x58..0x5F` |

Deck 1 pads use status `0x96`; Deck 2 pads use `0x97`.

## Mixer, browser and adapted FX

The profile maps both channel faders, crossfader, trims, three-band EQ,
filters, master volume, headphone mix/level and PFL. All absolute analog
controls except tempo are replayed after reconnect. Browse rotate/press,
Shift+browse and both normal/shifted LOAD buttons use the standard Pajoniiir
browser semantics.

The four physical FX buttons are a Pajoniiir adaptation, not a Serato layout:

| Input | Pajoniiir action |
| --- | --- |
| FX1 / FX2 | previous / next Beat FX |
| FX3 / FX4 | decrease / increase Beat FX beat size |
| Shift+FX1 / Shift+FX2 | Beat FX on / clear |
| Shift+FX3 / Shift+FX4 | accelerated beat-size decrease / increase |

## LED output and RGB values

Transport, Cue, Sync, PFL, loop, mode and master-cue LEDs use normal Note On
values `0x7F`/`0x00`. The VU meters pass through `0x00..0x7F` on CC `0x40`.
Pad banks use controller-specific RGB Data2 values:

| Bank | Note range | Off | On / blink-state fallback |
| --- | --- | --- | --- |
| Hot Cue | `0x00..0x07` | `0x00` | red `0x60` |
| Beat Loop | `0x10..0x17` | `0x00` | green `0x1C` |
| Pad FX 1 | `0x20..0x27` | `0x00` | cyan `0x1F` |
| Beat Jump | `0x30..0x37` | `0x00` | orange `0x74` |
| Pad FX 2 | `0x50..0x57` | `0x00` | fuchsia `0x63` |

The profile format carries one `blink` byte, but this controller's RGB Data2
is a colour selector rather than a generic hardware blink command. The fixture
therefore uses the same colour for on and blink states.

## Deliberately deferred controls

The profile does not invent behavior for Vinyl, Slip, Shift+Play stutter,
crossfader curve, Assistant/Beatmatch Guide, guide LEDs, or physical modes 7/8.
Key/Pitch Play pads remain deferred until P4 has an explicit musical-key
performance feature. USB audio remains a separate hardware gate even though
the profile advertises the controller's documented four-channel capability.

## Acceptance state

Host coverage verifies deterministic fixture regeneration, exact VID/PID
matching and rejection of `B105`, representative transport/shift/loop/pad
input mapping, Mode 6 addresses, RGB MIDI output, registry discovery, all 69
remote LED IDs, shared S3/P4 protocol values and P4 `sync_off`/`loop_size`
behavior.

Before this profile can be called hardware-supported, connect a physical
Inpulse 500 and record:

1. S3 descriptor VID/PID/product and successful P4 match/transfer/activation.
2. A two-deck MIDI input sweep, including releases and Shift variants.
3. Pad colours, mode LEDs, VU meters, reconnect and P4/S3 reboot resync.
4. Jog/scratch, Sync Off, Quantize/tempo-range and autoloop rotary behavior.
5. MAIN/headphone USB-audio channel routing, rate selection, underrun counters
   and simultaneous PCM5102A MAIN behavior.
