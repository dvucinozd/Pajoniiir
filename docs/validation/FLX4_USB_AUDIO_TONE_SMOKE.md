# FLX4 USB Audio Tone Smoke

Date: 2026-07-02
S3 commit: `codex/flx4-usb-audio-headphones` @ "feat: stream usb audio tone to flx4"
Descriptor accepted format: interface 1, alternate 2, endpoint 0x01, 4 channels, 16-bit
Tone sample rate: 48000 Hz (SET_CUR accepted by endpoint 0x01)
Channels: tone on channels 3/4, channels 1/2 silent
Bits: 16
Audible on headphones: yes — 1 kHz tone confirmed on the FLX4 3.5 mm headphones jack
Audible on FLX4 RCA: not measured this pass (channels 1/2 were driven silent)
MIDI responsive while streaming: yes — interactive controls confirmed responsive
while the tone streamed (MIDI interface claimed, OUT traffic active; one queue
drop during the reconnect burst only)
USB skipped packets: 0 over 25 s observation
USB underrun packets: 0 over 25 s observation
Packet pacing: submitted +5000/5 s (exactly 1000 packets/s), bytes +1,920,000/5 s
(exactly 48 kHz x 4 ch x 16-bit)
Decision: FLX4 headphones are driven by channels 3/4. Task 8 end-to-end must map
P4 `hp_out` to channels 3/4 and keep channels 1/2 silent for the first product
slice. `CONFIG_DDJ_FLX4_USB_AUDIO_TONE_ON_CHANNELS_1_2` remains available only
as a diagnostic inversion.

## Current software status

The S3 firmware can parse the FLX4 playback descriptor, select the preferred
4-channel 16-bit playback alternate setting, claim the interface when
`CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES=y`, allocate four isochronous transfer
objects, and generate deterministic 1 kHz -18 dBFS tone packets.

After the first silent smoke attempt, the streamer now also performs the two
mandatory UAC 1.0 bus requests that `usb_host_interface_claim()` does not send:

- `SET_INTERFACE` (interface 1, alternate 2) so the FLX4 leaves the
  zero-bandwidth alt 0 and activates endpoint `0x01`;
- `SET_CUR SAMPLING_FREQ_CONTROL` on endpoint `0x01` with the selected sample
  rate (the captured CS endpoint descriptor advertises this control).

Additional runtime behavior for the smoke pass:

- aggregate packet stats are logged every 5 seconds
  (`FLX4_USB_AUDIO tx submitted=... completed=... skipped=... underrun=...`);
- 100 consecutive transfer errors stop the stream with an error log instead of
  silently resubmitting forever;
- `flx4_usb_audio_stop()` releases the claimed audio interface so
  `usb_host_device_close()` succeeds on disconnect and the FLX4 can
  re-enumerate without a reboot.

The normal firmware default keeps `CONFIG_DDJ_FLX4_USB_AUDIO_HEADPHONES=n`.
Hardware smoke must be run before enabling this by default.
