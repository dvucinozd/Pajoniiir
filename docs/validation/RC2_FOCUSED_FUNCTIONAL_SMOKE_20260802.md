# RC2 focused functional smoke and media audit — 2026-08-02

## Scope

This record captures the operator-confirmed functional checks performed after
both boards received RC2 applications through OTA and then received complete
ESP-IDF v6.0.2 wired boot-chain flashes. It also records why the attempted WAV
check did not exercise the decoder. This is a focused smoke, not the remaining
long-duration, disconnect/reconnect or fault-injection acceptance matrix.

## Installed state

- P4: `RC2-3-g136aad7`, factory slot, ESP-IDF v6.0.2 bootloader and
  application. The shared ESP-Hosted/microSD SDMMC fix is present and the
  59,688 MB SDHC card mounts in 4-bit mode.
- S3: exact clean `RC2`, `ota_0`, `VALID`, with the ESP-IDF v6.0.2 bootloader
  and application written and verified over COM10.
- Before the wired flashes, the operator installed the signed RC2 application
  packages successfully through OTA on both targets and confirmed both
  reported `RC2`.

## Operator-confirmed functional smoke

The operator explicitly confirmed every item in the proposed P4 display/UI
set and every item in the FLX4/audio set:

- P4 display, touch, PSRAM-backed UI path and Settings operate normally;
- Settings reports the repaired microSD online rather than
  `Offline (/sd unavailable)`;
- the paginated Library can be viewed, navigated and used for the focused load
  check;
- the DDJ-FLX4 connects to the S3 and the exercised MIDI input and LED feedback
  paths respond;
- PCM5102A MAIN output and the FLX4 headphone CUE/MONITOR path are audible and
  functional in the focused run;
- real MP3 playback works in the focused run.

This confirmation closes the short display/touch/Library, FLX4 MIDI/UAC and
MAIN/monitor functional rows for the migrated firmware. It does not replace
the numeric monitor-link soak, USB disconnect/reconnect matrix, sustained BNA
recovery observation, locked-backend-read counter check or long dual-deck DSP
soak.

## WAV attempt and USB audit

The operator reported that the selected WAV entry did not load or play. A
read-only audit of the Rekordbox USB drive as Windows `L:` established:

- healthy exFAT volume labelled `test`, 31,449,939,968 bytes;
- `L:\PIONEER\rekordbox\export.pdb` exists (266,240 bytes);
- 68 physical `.mp3` files exist;
- zero physical `.wav`, `.wave` or `.flac` files exist.

The database still contains at least these WAV paths:

```text
/Contents/UnknownArtist/UnknownAlbum/file_example_WAV_10MG.wav
/Contents/UnknownArtist/UnknownAlbum/sample-15s.wav
```

Neither file exists on the volume. This matches the previously documented
dead-PDB-row trap: the catalog entry exists, but Rekordbox did not copy the
audio file to the device. Therefore this attempt ended at file open and is not
evidence of a WAV decoder failure.

A 45-second COM15 capture was also taken after the drive had been moved to the
Windows PC. The P4 correctly reported that
`/usb/PIONEER/rekordbox/export.pdb` could not be opened and exhausted USB
enumeration recovery. Because the audio USB device was physically absent from
P4 during that capture, it is mount-state evidence only, not a playback trace.

The current WAV implementation intentionally accepts classic RIFF/WAVE linear
PCM (`format=1`), 16-bit, mono or stereo. It rejects 24/32-bit PCM, IEEE float
and `WAVE_FORMAT_EXTENSIBLE`. The replacement fixture must use the supported
subset unless format support is deliberately expanded first.

## Result and remaining work

- **Pass:** RC2 OTA application installation on both targets.
- **Pass:** focused P4 display/touch/PSRAM UI, Settings and paginated Library
  operation.
- **Pass:** focused FLX4 MIDI/LED, PCM5102A MAIN and FLX4 CUE/MONITOR operation.
- **Pass:** focused real-MP3 playback.
- **Not run:** real WAV decode; the two catalogued WAV files are absent.
- **Not run:** real FLAC decode; no FLAC file is present.
- **Pending:** re-export real supported WAV and FLAC fixtures through Rekordbox,
  verify the files physically exist under `Contents`, then run sustained
  dual-deck cache playback while capturing BNA recovery, underruns and
  `audio_engine_locked_backend_read_count()`.
- **Pending:** the remaining USB recovery, AP/STA/OTA, held-control,
  fault-injection and long-duration acceptance rows.
