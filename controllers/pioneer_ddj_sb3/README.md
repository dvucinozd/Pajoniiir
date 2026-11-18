# Pioneer DDJ-SB3 profile

The profile maps the two-deck layer of the DDJ-SB3 (USB identity `2B73:0019`)
from the Pioneer DDJ-SB3 MIDI Message List ver. 1.01. Deck 3/4 layers and
functions with no current semantic are listed in `omitted_inputs`.

The compiled fixture is S3CP v4, 5956 bytes, with 284 expanded inputs and
115 outputs. Capability flags are `0x5F`: LED feedback, USB audio, jog touch,
14-bit pitch, independent channel filters and 16-bit USB audio (bit 6).
SHA-256:
`530652b62fe05f21e42e365b4cb91970969d028742714ab38489d026e767c645`.

```sh
python3 tools/controller_profile/compile_profile.py controllers/pioneer_ddj_sb3/profile.json
```

## USB audio

The SB3 is a full-speed UAC1 device. Interface 1 alternate 1 is the only audio
OUT stream: 4 channels, synchronous isochronous endpoint `0x01`, 576-byte
maximum packet, 44.1 or 48 kHz. The profile requests 44.1 kHz.

| USB channel | Output |
| ---: | --- |
| 1 | Master left |
| 2 | Master right |
| 3 | Headphones left |
| 4 | Headphones right |

The descriptor advertises 24-bit samples in 3-byte subslots, but the device
plays the stream as **16-bit little-endian** samples, 8 bytes per frame. The
profile sets `"audio": { "wire_format": "s16le" }`, so the firmware sends
16-bit samples in 16-bit-sized packets (44 or 45 frames, 352 or 360 bytes per
millisecond) on the same alternate setting at 44.1 kHz. See
[`audio.wire_format`](../../docs/CONTROLLER_PROFILE_SCHEMA.md#audiowire_format-optional).

How each layout sounded on the hardware:

| Layout sent | Result |
| --- | --- |
| 24-bit LE, 12 bytes per frame (descriptor format) | Distorted and about two-thirds pitch, channels mixed together, one-sided music |
| 24-bit big-endian or right-justified | Static or distortion |
| 16-bit LE frames zero-padded to the 24-bit packet length | Correct channel mapping, but distorted because the padding plays as gaps |
| **16-bit LE frames, 16-bit packet length** | **Clean on all four channels** |

Byte probes, a tone in a single byte position, gave the same picture. Only
odd-offset bytes, the high byte of each little-endian 16-bit word, were loud,
and they landed on the channels that 8-byte frames predict.

Install firmware that supports profile capability bit 6 before uploading this
profile. Older firmware ignores the bit and sends 24-bit audio, which plays
distorted. To confirm, check `/api/status`: `uac_format.wire_format` reads
`"s16le"` once the profile is active.

The configuration descriptor also has a trailing byte after its last
descriptor; the descriptor walker tolerates it. The host test fixture is
`tests/controller_usb_audio/fixtures/ddj_sb3_fs_config.h`.
