# FLX4 USB Audio Tone Smoke

Date: pending
S3 commit: pending
Descriptor accepted format: interface 1, alternate 2, endpoint 0x01, 4 channels, 16-bit
Tone sample rate: 48000 Hz selected when available
Channels: default tone candidate on channels 3/4, channels 1/2 silent
Bits: 16
Audible on headphones: pending
Audible on FLX4 RCA: pending
MIDI responsive while streaming: pending
USB skipped packets: pending
USB underrun packets: pending
Decision: pending

## Current software status

The S3 firmware can parse the FLX4 playback descriptor, select the preferred
4-channel 16-bit playback alternate setting, claim the interface when
`CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES=y`, allocate four isochronous transfer
objects, and generate deterministic 1 kHz -18 dBFS tone packets.

The normal firmware default keeps `CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES=n`.
Hardware smoke must be run before enabling this by default.
