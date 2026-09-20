# P4 bounded-media-cache exact-image evidence — 2026-09-19

Status: **focused cache/load/playback PASS; full Phase 4 functional matrix remains open**.

## Exact image

- Branch: `feat/p4-dual-usb-host`
- Commit: `c21ad8628ba62be7d9e113cff46787a0a6c828f3`
- Version: `RC2-147-gc21ad86`
- Installed slot: `ota_1` (signed push OTA from `ota_0`)
- OTA state after reboot: `idle`, empty `last_error`
- Application: `2,458,496` bytes
- Application SHA-256:
  `bdf90aa7b186822f0d4c43d1157dda635fa116335f34929b044a1a77aa7fb254`
- Signed bundle: `2,458,684` bytes
- Signed bundle SHA-256:
  `0bf3b5698ae47a9c97fbb0db5114b7a6baf34398af528f729b7969d4ad96a8da`

The complete P4 host suite passed, including 85 bounded-cache assertions. A
clean ESP-IDF v6.0.2 `build_signed` build, signed-bundle packaging and
independent bundle verification also passed. The exact local and remote branch
SHA matched before packaging.

## Defect and remediation

The initial diagnostic image `RC2-146-ge3b05b9` showed that the FLAC decoder's
actual 32 KiB backend page was only 1--5.5 KiB ahead of the predicted stream
cursor, yet reads still occurred under `AE_LOCK`. The cache was not losing a
far-ahead decoder request. Instead, a prefetch hit did not refresh the page's
LRU stamp. Warming the remaining forward window could therefore evict the
current, formerly oldest page immediately before decode.

Commit `c21ad86` makes a successful prefetch an LRU touch and adds a regression
that fills all eight slots, re-prefetches the current page, loads a ninth page
and proves that the current page remains resident.

## Physical fixture and playback results

USB0 mounted automatically after OTA, the Library published 324 tracks and
USB1 restored the exact FLX4 profile with MIDI and USB audio active.

Real files exercised from USB0:

- MP3: `TAINTED DUB - CLIP.mp3`, 44.1 kHz playback path;
- WAV: `file_example_WAV_10MG.wav`, stereo PCM16/44.1 kHz, about 59 s;
- FLAC: `Sample_BeeMoved_96kHz24bit.flac`, stereo 96 kHz/24-bit, about 39 s.

Results:

- FLAC load/prefill finished with `locked_backend_reads2=0`, versus six on the
  diagnostic image before the LRU fix.
- The complete FLAC track reached natural EOF with locked reads, PCM underruns
  and BNA recoveries all remaining zero. USB0 and FLX4 stayed present.
- The complete WAV track reached natural EOF with zero PCM underruns and no
  USB fault. One boot-cumulative locked read appeared only at the first full
  D1 EOF. Two targeted near-EOF repeats, one on each deck, added zero locked
  reads, so it was isolated and non-growing rather than a repeatable EOF fault.
- Simultaneous MP3 + FLAC ran for 30 s with zero deltas in both locked-read and
  PCM-underrun counters, output-late count and BNA recovery. The operator
  confirmed both decks were audible and free of interruption.
- Simultaneous MP3 + WAV ran for 30 s with zero deltas in the same counters;
  the operator confirmed both decks were audible and free of interruption.
- One isolated output-late sample occurred at FLAC natural EOF: `10,688 us`
  against a `10,668 us` warning threshold. It was a 20 us overrun with no PCM
  underrun, BNA recovery, USB/controller loss or subsequent counter growth.

## Boundary and remaining work

This closes the measured FLAC cache-thrashing defect and the focused real-file
load/play/EOF plus mixed-format playback slice. It does not yet close all of
Phase 4. The remaining media-functional matrix is mixed-format seek, loop,
CUE, scratch/hold and near-EOF manipulation on both decks, with explicit
operator listening confirmation and flat counter deltas. Phase 5 P4 CPU/I2S
deadline measurement is also separate from this cache result.
