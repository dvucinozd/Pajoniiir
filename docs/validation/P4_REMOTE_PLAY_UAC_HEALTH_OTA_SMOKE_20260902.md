# P4 remote-play and UAC-health exact-image smoke — 2026-09-02

## Scope

This record validates two focused fixes on `feat/p4-dual-usb-host`:

1. an authenticated web transport command must wake the two-minute idle
   screensaver and still execute the first requested action; and
2. USB Audio Class lifetime silence-underflow telemetry must not be reported as
   active-playback data loss.

It is a focused exact-image acceptance record. It does not close electrical,
repeated-reconnect, remove-during-decode or multi-hour product gates.

## Source and artifact identity

- Exact source commit: `af597d8813f8a6a7fc20898bb25b5943230edab1`
- Commit subject: `fix: confirm remote playback and UAC health`
- Branch: `feat/p4-dual-usb-host`
- Toolchain: ESP-IDF v6.0.2
- Application version: `RC2-113-gaf597d8`
- Application size: `2,451,840` bytes
- Application SHA-256:
  `e9966017d078dece284ee1e8c7813ea1820ebc65022a618101572641b4d40eca`
- Signed bundle size: `2,452,028` bytes
- Signed bundle SHA-256:
  `6858a36f714e61926f12f88ad4c3d3b506b9a5fb728f28e3f0c43931a29a3e17`
- Signing key ID: `rel-001`
- Installed slot: `ota_1`
- OTA service after reboot: `state=idle`, empty `last_error`

The isolated `build_signed` build completed with 42% of the 4 MiB application
slot free. Packaging verified both the signed application bundle and outer
manifest before upload. `dependencies.lock` was unchanged.

## Software gates

- Complete P4 host suite: PASS
- `audio_engine`: `393 PASS / 0 FAIL`
- UAC health-monitor regression: PASS
- OTA signing suite: `6/6 PASS`
- Clean ESP-IDF v6.0.2 `build_signed`: PASS
- `git diff --check`: PASS before commit
- Local and remote branch SHA after push: exact match at `af597d8`

## Hardware setup

- P4 connected to the `Pajoniiir` service network
- Rekordbox USB medium on USB0
- Pioneer DDJ-FLX4 on direct USB1
- USB0 library generation: `1`, `100` tracks
- Deck 1 fixture: `069. Humble Pie - I Don't Need No Doctor.mp3`
- Deck 2 fixture: `061. Eric Carmen - All By Myself.mp3`

After the exact-image reboot, USB0 mounted, the FLX4 profile became active,
MIDI IN was available and direct FLX4 USB audio was available. USB host daemon
errors were zero.

## Screensaver and first remote PLAY

Both loaded decks remained stopped from `15:19:28` until `15:21:31`, more than
the fixed 120-second screensaver timeout. Exactly one authenticated request was
then sent:

```text
POST /api/control?deck=1&action=play_pause
X-DDJ-Control: 1
```

Observed result:

- HTTP status: `200`
- response body: `OK`
- Deck 1 state after 700 ms: `PLAYING`
- Deck 1 position after 700 ms: `963 ms`

The first remote action was therefore not consumed solely as a wake event.

## UAC health result

Immediately after the first PLAY:

```text
submitted_blocks=167
dropped_blocks=0
overflow_frames=0
underflow_frames=8697578
data_loss=false
data_loss_flags=0
ring_state=nominal
```

The large lifetime underflow count was accumulated while idle silence was being
served. The new session-scoped health result correctly remained clear.

Deck 2 was then started and both decks ran simultaneously for 30 seconds.
Counter deltas over the active window were:

```text
dropped_blocks=0
overflow_frames=0
underflow_frames=0
output_late_count=0
data_loss=false
data_loss_flags=0
```

Both stop requests returned HTTP 200. Final state remained on the same boot:

```text
deck1.playing=false
deck2.playing=false
USB0 mounted=true
FLX4 present=true
USB host daemon errors=0
service log drops=0
data_loss=false
data_loss_flags=0
```

## Acceptance decision

PASS for the two focused fixes on the exact committed image.

Still open:

- measured and protected common 5 V/VBUS topology;
- repeated cold/warm boot and insertion-order matrix;
- repeated USB0/USB1 reconnect and USB0 removal during load/decode;
- real, physically present WAV/FLAC fixtures under sustained dual-deck load;
- P4 Master Tempo CPU/I2S deadline and listening-quality measurement;
- guarded pull-OTA/web/profile fault matrix;
- multi-hour and closed-enclosure power/thermal/RF acceptance.
