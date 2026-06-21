# Control Link Protocol

DDJ-FFL4 keeps the inherited UART `control_link` as the internal protocol
between the ESP32-S3 and ESP32-P4.

## Current Frame

```text
[0] 0xA5        start byte
[1] type        event or command type
[2] id          control id
[3] val_lo      value LSB
[4] val_hi      value MSB
[5] seq         rolling sequence counter
[6] checksum    XOR of bytes [1]..[5]
```

Baud rate: `115200`.

## Existing Types

| Direction | Type | Meaning |
| --- | ---: | --- |
| S3 -> P4 | `0x01` | button |
| S3 -> P4 | `0x02` | encoder |
| S3 -> P4 | `0x03` | pitch |
| S3 -> P4 | `0x04` | heartbeat |
| S3 -> P4 | `0x82` | semantic state, currently FLX4 USB connection state |
| P4 -> S3 | `0x81` | LED |
| P4 -> S3 | `0x82` | state feedback/reserved |

## DDJ-FFL4 Semantic Namespace

The current frame has only one `id` byte and one 16-bit value. DDJ-FFL4 keeps
that wire format for MVP and makes the `id` namespace deck-aware instead.

Implemented namespace layout:

```text
id range:       deck, mixer, browser, or system namespace
deck control:   id - deck namespace base (32 controls per deck)
value:          signed delta, 0/1 button state, or 0..16383 absolute value
```

Suggested namespaces:

| Namespace | ID range | Meaning |
| --- | ---: | --- |
| Deck 1 | `0x10`-`0x2F` | transport, jog, tempo, loop, pad mode/action |
| Deck 2 | `0x30`-`0x4F` | transport, jog, tempo, loop, pad mode/action |
| Mixer | `0x50`-`0x5F` | channel faders, crossfader, PFL, trim/EQ/filter monitor controls |
| Browser | `0x60`-`0x6F` | browse/load/navigation |
| System | `0x70`-`0x7F` | heartbeat, diagnostics |

The S3 and P4 headers carry matching constants for this layout. Host tests
verify that the shared MVP IDs, Smart control IDs, and FLX4 connection state
IDs stay aligned across both firmware targets.

This avoids changing the wire frame before the first MVP hardware integration
test.

## MVP Semantic IDs

| ID | Event | Value |
| ---: | --- | --- |
| `0x10` | Deck 1 Play | `0` release, `1` press |
| `0x11` | Deck 1 Cue | `0` release, `1` press |
| `0x12` | Deck 1 Jog scratch delta | signed delta |
| `0x13` | Deck 1 Jog bend delta | signed delta |
| `0x14` | Deck 1 Jog touch | `0` release, `1` touch |
| `0x15` | Deck 1 Tempo | `0..16383` |
| `0x16` | Deck 1 Shift | `0` release, `1` press |
| `0x17` | Deck 1 Cue+Shift track start | `0` release, `1` press |
| `0x18` | Deck 1 Beat Sync | `0` release, `1` press; P4 sync behavior deferred |
| `0x19` | Deck 1 Tempo Range | `0` release, `1` press; behavior deferred |
| `0x1A`-`0x20` | Deck 1 loop / beat-jump buttons | `0` release, `1` press; behavior deferred |
| `0x21`-`0x24` | Deck 1 pad mode select | `0` release, `1` press |
| `0x25` | Deck 1 pad action | packed pad mode/index/shift/press |
| `0x30` | Deck 2 Play | `0` release, `1` press |
| `0x31` | Deck 2 Cue | `0` release, `1` press |
| `0x32` | Deck 2 Jog scratch delta | signed delta |
| `0x33` | Deck 2 Jog bend delta | signed delta |
| `0x34` | Deck 2 Jog touch | `0` release, `1` touch |
| `0x35` | Deck 2 Tempo | `0..16383` |
| `0x36`-`0x45` | Deck 2 extension controls | same control order as Deck 1 |
| `0x50` | Channel 1 volume | `0..16383` |
| `0x51` | Channel 2 volume | `0..16383` |
| `0x52` | Crossfader | `0..16383` |
| `0x53` | Deck 1 PFL | `0` release, `1` press/toggle |
| `0x54` | Deck 2 PFL | `0` release, `1` press/toggle |
| `0x55`-`0x5F` | Trim, EQ, Filter, Headphone Mix | `0..16383`; P4 DSP behavior deferred |
| `0x60` | Browse delta | signed delta |
| `0x61` | Load Deck 1 | `0` release, `1` press |
| `0x62` | Load Deck 2 | `0` release, `1` press |
| `0x63` | Browse press | `0` release, `1` press; toggles Library/Overview |
| `0x64` | Browse+Shift delta | signed delta; behavior deferred |
| `0x70` | FLX4 connection state | `0` disconnected, `1` connected; sent with `CTRL_TYPE_STATE` |
| `0x71` | Smart CFX | `0` release, `1` press; input only, P4 DSP deferred |
| `0x72` | Smart Fader | `0` release, `1` press; input only, P4 DSP deferred |

In S3 translator mode, `flx4_map` converts the DDJ-FLX4 MIDI controls from
`docs/DDJ_FLX4_MIDI_MAP.md` into these semantic IDs. High-rate jog, tempo,
channel fader, and crossfader events are locally coalesced before UART send;
button edges and load/PFL events remain FIFO.

## LED Feedback

The P4 sends LED commands only from confirmed state. For example, Deck 1
Play LED turns on only when P4 `deck_core` confirms Deck 1 is playing.

The current MVP LED payload uses inherited `led_id_t` values with the deck
packed into the high byte of the 16-bit value by `control_link_send_led_deck()`:

| LED ID | Meaning |
| ---: | --- |
| `0x00` | Cue |
| `0x01` | Play |
| `0x04` | PFL |
| `0x05` | VU meter level |

```text
value low byte:  LED state, 0 off / 1 on / 2 blink
value high byte: deck, 0 Deck 1 / 1 Deck 2
```

States remain inherited:

- `0`: off
- `1`: on
- `2`: blink

`LED_VU_METER` uses the same deck-aware LED frame but interprets the low byte
as a FLX4 VU level `0..127`. P4 samples per-deck audio peaks from the output
path, resets the peak when read, and publishes levels at a controlled 30 ms
period. S3 forwards this as USB MIDI CC `0x02` on `0xB0` for Deck/Channel 1 and
`0xB1` for Deck/Channel 2.

P4 also owns reconnect recovery. When S3 reports
`CTRL_TYPE_STATE / CTRL_ID_FLX4_CONNECTION / CTRL_FLX4_CONNECTED`, P4 forces a
complete MVP LED snapshot for Deck 1/2 Cue, Play, and PFL. Host tests verify
normal diff suppression, failed-send retry, and forced reconnect publication.

## Future Protocol Versioning

After MVP, consider a versioned 8-byte or 9-byte frame with explicit deck and
control fields. Do not change frame length before the first S3/P4 integration
test unless the 7-byte frame blocks a real requirement.
