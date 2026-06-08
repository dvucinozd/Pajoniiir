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
| P4 -> S3 | `0x81` | LED |
| P4 -> S3 | `0x82` | state feedback/reserved |

## DDJ-FFL4 Extension Strategy

The current frame has only one `id` byte and one 16-bit value. That is enough
for MVP if the id namespace becomes deck-aware.

Recommended near-term extension:

```text
id high nibble: deck or mixer namespace
id low nibble:  control id inside namespace
value:          signed delta, 0/1 button state, or 0..16383 absolute value
```

Suggested namespaces:

| Namespace | ID range | Meaning |
| --- | ---: | --- |
| Deck 1 | `0x10`-`0x1F` | transport, jog, tempo |
| Deck 2 | `0x20`-`0x2F` | transport, jog, tempo |
| Mixer | `0x30`-`0x3F` | channel faders, crossfader, PFL |
| Browser | `0x40`-`0x4F` | browse/load/navigation |
| System | `0x70`-`0x7F` | heartbeat, diagnostics |

This avoids changing the wire frame before the first MVP test.

## MVP Semantic IDs

| ID | Event | Value |
| ---: | --- | --- |
| `0x10` | Deck 1 Play | `0` release, `1` press |
| `0x11` | Deck 1 Cue | `0` release, `1` press |
| `0x12` | Deck 1 Jog scratch delta | signed delta |
| `0x13` | Deck 1 Jog bend delta | signed delta |
| `0x14` | Deck 1 Jog touch | `0` release, `1` touch |
| `0x15` | Deck 1 Tempo | `0..16383` |
| `0x20` | Deck 2 Play | `0` release, `1` press |
| `0x21` | Deck 2 Cue | `0` release, `1` press |
| `0x22` | Deck 2 Jog scratch delta | signed delta |
| `0x23` | Deck 2 Jog bend delta | signed delta |
| `0x24` | Deck 2 Jog touch | `0` release, `1` touch |
| `0x25` | Deck 2 Tempo | `0..16383` |
| `0x30` | Channel 1 volume | `0..16383` |
| `0x31` | Channel 2 volume | `0..16383` |
| `0x32` | Crossfader | `0..16383` |
| `0x33` | Deck 1 PFL | `0` release, `1` press/toggle |
| `0x34` | Deck 2 PFL | `0` release, `1` press/toggle |
| `0x40` | Browse delta | signed delta |
| `0x41` | Load Deck 1 | `0` release, `1` press |
| `0x42` | Load Deck 2 | `0` release, `1` press |

## LED Feedback

The P4 should send LED commands only from confirmed state. For example, Deck 1
Play LED turns on only when P4 `deck_core` confirms Deck 1 is playing.

Near-term LED id namespace:

| LED ID | Meaning |
| ---: | --- |
| `0x10` | Deck 1 Play |
| `0x11` | Deck 1 Cue |
| `0x12` | Deck 1 PFL |
| `0x20` | Deck 2 Play |
| `0x21` | Deck 2 Cue |
| `0x22` | Deck 2 PFL |

States remain inherited:

- `0`: off
- `1`: on
- `2`: blink

## Future Protocol Versioning

After MVP, consider a versioned 8-byte or 9-byte frame with explicit deck and
control fields. Do not change frame length before the first S3/P4 integration
test unless the 7-byte frame blocks a real requirement.
