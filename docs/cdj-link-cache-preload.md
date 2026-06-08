# CDJ Link Cache/Preload V1

## Scope

V1 implements a remote USB library between two CDJ100S players. One player owns the USB stick and runs as the host. The joined player browses the host library, downloads the selected track assets to its local SD card, then plays the cached MP3 locally through the existing `audio_engine`.

V1 deliberately does not stream live audio and does not implement beat-sync, phase-sync, shared hot-cue edits, library writeback, or Pioneer Pro DJ Link compatibility.

## Topology

- Host player: Wi-Fi SoftAP `CDJ100S-<id>`, USB stick mounted at `/usb`, SD optional.
- Joined player: Wi-Fi STA, SD required, cache mounted at `/sd`.
- Wireless stack: ESP32-P4 uses the onboard ESP32-C6 via Espressif hosted Wi-Fi (`esp_hosted` / `esp_wifi_remote`).
- V1 scale: one host and one joined client.

The host advertises itself with UDP beacons on port `42424`. The joined player stores the most recent peer in RAM and expires it after 5 seconds without a beacon.

## Cache Layout

Remote playback uses a stable local cache path:

```text
/sd/cdjlink/<peer_id>/<track_key>/
  manifest.bin
  ANLZ0000.DAT
  ANLZ0000.EXT   optional
  audio.mp3
```

Downloads are written to `.part` files first. A file is reused only when its byte count matches the remote manifest. Completed `.part` files are renamed into place so a power loss does not leave a partial file with the final name.

Required assets are:

- `manifest.bin`
- `ANLZ0000.DAT`
- `audio.mp3`

`ANLZ0000.EXT` is optional. If it is unavailable or fails to download, playback can still continue with low-resolution waveform and beat/cue metadata from DAT.

## Protocol

All large library data uses fixed-size little-endian binary records. JSON is intentionally avoided for the library list because the ESP32-P4 UI should be able to copy and sort records without parsing text.

Track record:

```c
typedef struct {
    uint32_t track_key;
    uint32_t rekordbox_track_id;
    uint16_t bpm;
    uint32_t duration_ms;
    char title[96];
    char artist[64];
    char album[64];
} cdj_link_track_record_t;
```

Track manifest:

```c
typedef struct {
    uint32_t track_key;
    uint32_t audio_size;
    uint32_t dat_size;
    uint32_t ext_size;
    uint8_t has_ext;
} cdj_link_track_manifest_t;
```

`track_key` is the Rekordbox track ID when available. If the ID is zero, it is an FNV-1a hash of the USB audio path.

## Host HTTP Endpoints

The host HTTP server listens on port `8080`.

- `GET /v1/hello.txt`
- `GET /v1/library.bin`
- `GET /v1/track/<track_key>/manifest.bin`
- `GET /v1/track/<track_key>/ANLZ0000.DAT`
- `GET /v1/track/<track_key>/ANLZ0000.EXT`
- `GET /v1/track/<track_key>/audio.mp3`

Every USB file read is protected by the global `media_io_gate`. The HTTP server uses a 250 ms try-lock and returns `503 BUSY` instead of blocking indefinitely when the local deck is currently loading from USB.

File endpoints send fixed `Content-Length` responses and pipeline USB reads with HTTP writes using a small SPIRAM-backed block queue. On the single-host bench, `track_key=5` `audio.mp3` best-case transferred 9047128 bytes in 9.40-9.42 s over the SoftAP path, about 0.96 MB/s. Later Windows-client runs varied around 0.75 MB/s and one transfer reset, so full stability and client-side SD write validation still need the second P4.

## Firmware Components

- `wifi_link`: hosted Wi-Fi init, SoftAP/STA mode, link status.
- `cdj_link_protocol`: binary library, manifest and discovery structures.
- `media_io_gate`: single global gate for USB file reads.
- `cdj_link_server`: host library snapshot and binary file endpoints.
- `cdj_link_client`: UDP discovery and HTTP downloads.
- `remote_cache`: SD cache population and reuse.
- `media_catalog`: common UI facade for local USB and joined remote library.

## UI Flow

1. In Settings, set Link mode to `HOST USB` or `JOIN PLAYER`. The Wi-Fi role is applied on reboot.
2. On the host, insert USB and let the local library index load.
3. On the joined player, choose `JOINED` in the Library tab.
4. Select a remote track and press `LOAD TRACK`.
5. The joined player downloads required assets to `/sd/cdjlink/...`.
6. When cache completes, it parses cached DAT/EXT and calls `audio_engine_load()` with the cached MP3 path.

The Overview screen reads title, artist, BPM, waveform, hot cues and beat indicator metadata through the same loaded-track facade for local and remote tracks.

## Verification Checklist

- Host SoftAP starts and joined client obtains IP.
- Joined player receives beacons within 5 seconds.
- `library.bin` fetch completes under 2 seconds for a 308-track USB library.
- `audio.mp3` transfer sustains at least 0.8 MB/s from host USB to client SD. Host-to-Wi-Fi-client transfer has reached this target in best-case testing; stable second-player SD cache validation remains open.
- Host local load during a remote request returns `503 BUSY` and does not crash USB host.
- A cached track survives client power cycle and reloads without Wi-Fi transfer.
- Removing host USB marks the remote peer/library unavailable while already cached tracks remain playable.

## Known V1 Limits

- One host, one joined client.
- Full-track cache must complete before playback starts.
- SD card is required on the joined player.
- No live audio streaming.
- No beat-sync or phase sync.
- No remote library writeback.
- No shared hot-cue edit persistence.
- No Pro DJ Link wire compatibility.
