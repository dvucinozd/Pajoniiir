# FLX4 USB Audio Descriptor Capture

Document status: completed descriptor evidence used by the current S3 USB
Audio path, reviewed 2026-07-13.

Date: 2026-07-02
Controller: Pioneer DDJ-FLX4
S3 port: COM3
Firmware config: `CONFIG_DDJ_FLX4_DUMP_USB_CONFIG_DESCRIPTOR=y`
Fixture: `tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin`

## Summary

The FLX4 active USB configuration descriptor was captured from the S3 USB host.
The captured descriptor is 451 bytes long, matching `wTotalLength=0x01C3`, and
declares 7 interfaces.

After descriptor dump, the S3 still reached the existing MIDI path:

- MIDIStreaming interface 4 alt 0 was claimed.
- MIDI IN endpoint `0x82` and MIDI OUT endpoint `0x03` remained available.
- FLX4 connection state was published as connected.

One transient first-open claim failure was observed immediately after the first
enumeration attempt, followed by a successful reopen/reclaim on the next device
address. This does not block descriptor capture, but it should be kept in mind
when the USB Audio interface claim path is added.

## Interfaces

| Interface | Alternate | Class | Subclass | Protocol | Endpoints | Notes |
| ---: | ---: | --- | --- | ---: | --- | --- |
| 0 | 0 | Audio | AudioControl | 0 | none | Audio control for first audio function |
| 1 | 0 | Audio | AudioStreaming | 0 | none | OUT streaming idle alt |
| 1 | 1 | Audio | AudioStreaming | 0 | `0x01` isoch OUT, 576 bytes | 4-channel, 24-bit, 48/44.1 kHz candidate |
| 1 | 2 | Audio | AudioStreaming | 0 | `0x01` isoch OUT, 384 bytes | 4-channel, 16-bit, 48/44.1 kHz candidate |
| 2 | 0 | Audio | AudioStreaming | 0 | none | IN streaming idle alt |
| 2 | 1 | Audio | AudioStreaming | 0 | `0x81` isoch IN, 288 bytes | 2-channel, 24-bit, 48/44.1 kHz capture candidate |
| 2 | 2 | Audio | AudioStreaming | 0 | `0x81` isoch IN, 192 bytes | 2-channel, 16-bit, 48/44.1 kHz capture candidate |
| 3 | 0 | Audio | AudioControl | 0 | none | Audio control for MIDI function grouping |
| 4 | 0 | Audio | MIDIStreaming | 0 | `0x82` bulk IN, `0x03` bulk OUT | Existing FLX4 MIDI path |
| 5 | 0 | HID | none | 0 | `0x05` interrupt OUT, `0x84` interrupt IN | HID interface, currently unused |
| 6 | 0 | Vendor | `0xF0` | 0 | `0x86` bulk IN, `0x07` bulk OUT | Vendor-specific interface, currently unused |

## Audio streaming candidates

| Interface | Alternate | Direction | Endpoint | Channels | Bits | Sample rates | Max packet | Accepted for first slice |
| ---: | ---: | --- | --- | ---: | ---: | --- | ---: | --- |
| 1 | 1 | OUT | `0x01` | 4 | 24 | 48 kHz, 44.1 kHz | 576 | no |
| 1 | 2 | OUT | `0x01` | 4 | 16 | 48 kHz, 44.1 kHz | 384 | yes |
| 2 | 1 | IN | `0x81` | 2 | 24 | 48 kHz, 44.1 kHz | 288 | no |
| 2 | 2 | IN | `0x81` | 2 | 16 | 48 kHz, 44.1 kHz | 192 | no |

## Decision

The first USB Audio implementation should target interface 1 alternate 2:
4-channel, 16-bit, isochronous OUT on endpoint `0x01`.

Rationale:

- It is the lowest-bandwidth valid playback format exposed by the controller.
- It matches the P4 monitor path's native signed 16-bit PCM format.
- It leaves the 24-bit alternate setting available for later quality work only
  if needed.

The physical channel meaning still requires a tone smoke test. Until measured,
assume the first streamer must support a compile-time channel mapping switch so
we can identify whether FLX4 headphones are on channels 1/2 or 3/4.

## Raw descriptor fixture

The binary descriptor fixture is stored at:

```text
tests/flx4_usb_audio/fixtures/flx4_config_descriptor.bin
```

Fixture checks:

- byte count: 451
- first bytes: `09 02`
- `wTotalLength`: 451
- `bNumInterfaces`: 7

This fixture is the source of truth for the next parser task.
