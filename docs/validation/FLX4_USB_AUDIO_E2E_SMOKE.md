# FLX4 USB Audio End-to-End Smoke

Date: 2026-07-02
Branch: `codex/flx4-usb-audio-headphones`
Boards: P4 on COM15, S3 on COM3, DDJ-FLX4 on the S3 USB host.

Full path proven: P4 `audio_engine` cue/monitor mix (`hp_out`) -> `monitor_pcm_link`
I2S TX -> inter-board I2S -> S3 `p4_audio_link` ring -> `flx4_usb_audio` UAC OUT
transfers -> DDJ-FLX4 headphones jack.

## Step 1 — cue audible in FLX4 headphones (PASS)

Config: ES8311 paces on I2S unit 0, monitor link on unit 1, PCM5102A off
(`sdkconfig.flx4_hp_e2e` Step 1 topology, commit `0c06516`).

| Check | Result |
| --- | --- |
| Deck cue audible in FLX4 headphones | PASS (confirmed by operator) |
| PFL / Headphones Mix follow P4 monitor DSP | PASS |
| FLX4 MIDI controls responsive while streaming | PASS |
| Sample-rate match (44.1 / 48 kHz tracks) | Ring autostart matches FLX4 endpoint rate to the P4 output rate |
| S3 / P4 stability | No reboot |

## Step 2 — RCA MAIN + FLX4 cue simultaneously (PASS)

Config (product topology, commit `c9bf679`): PCM5102A RCA MAIN on I2S unit 1
(paces the output loop), monitor link on unit 0, ES8311 disabled
(`CONFIG_BSP_ES8311_MONITOR=n`). Physical link wiring unchanged from Step 1;
only the internal I2S unit moved.

| Check | Result |
| --- | --- |
| MAIN mix on PCM5102A RCA | PASS (confirmed by operator) |
| Cue on FLX4 headphones, simultaneously | PASS |
| PFL / Headphones Mix behaviour unchanged from Step 1 | PASS |
| No audible stutter (loop paces on the PCM5102A blocking write) | PASS |
| Dual-deck playback stability | No reboot on either board |

## Result

FLX4 USB headphones (CUE/MONITOR) and PCM5102A RCA (MAIN OUT) run at the same
time from the two usable P4 I2S units. The eco2 I2S-unit-2 freeze is avoided;
ES8311 onboard monitor is dropped in the product build (not needed as a
simultaneous fallback per the 2026-07-02 decision). The end-to-end acceptance
criteria in the plan are met.

## XIAO ESP32S3/Sense migration re-smoke -- PASS (2026-07-07)

Branch: `codex/s3-supermini-migration`

After replacing the S3 control board with the Seeed Studio XIAO ESP32S3 /
XIAO ESP32S3 Sense, the full product headphone path was re-smoked with the
XIAO GPIO7/GPIO8/GPIO9 monitor PCM link wiring.

Builds flashed:

- S3: `build_flx4_hp_e2e_xiao`, using `sdkconfig.flx4_hp_e2e` with
  `CONFIG_P4_AUDIO_LINK_BCLK_GPIO=7`,
  `CONFIG_P4_AUDIO_LINK_WS_GPIO=8`, and
  `CONFIG_P4_AUDIO_LINK_DIN_GPIO=9`.
- P4: `build_flx4_hp_e2e_tcmguard`, using the product `flx4_hp_e2e` topology
  plus the P4 TCM heap guard needed for stable ESP-IDF 5.5 boot on ESP32-P4.

Observed result:

| Check | Result |
| --- | --- |
| P4 boot stability after flash | PASS; no FreeRTOS TCM stack/TCB bootloop |
| USB media library load | PASS; P4 reported 18 tracks |
| P4 monitor PCM TX | PASS; `submitted` and `sent` rose together, `dropped=0` |
| S3 P4-audio-link RX while P4 playback was active | PASS; `rx blocks` rose, `gaps=0`, `crc=0` |
| FLX4 headphones audible | PASS; confirmed by operator |

The first S3 RX check with FLX4 not yet consuming audio showed the expected
full ring and increasing overruns: the P4-to-S3 link was already carrying PCM,
but no USB Audio consumer was draining it. After the FLX4 headphones path was
connected, the operator confirmed audible audio in the FLX4 headphones.

## XIAO Wi-Fi UDP telemetry soak -- PASS (2026-07-07)

After adding the S3 Wi-Fi UDP debug-log profile, the XIAO product build was
flashed with local Wi-Fi credentials and the PC UDP listener at
`192.168.0.239:3333`. The S3 joined Wi-Fi as `192.168.0.235`, mirrored normal
ESP logs over UDP, and captured a 300-second soak while FLX4 headphones were
audibly playing.

Capture file: `logs/s3_wifi_udp_soak_20260707.log`.

| Metric | Start | End | Delta |
| --- | --- | --- | --- |
| S3 `P4_AUDIO_LINK rx blocks` | `36297` | `87786` | `+51489` |
| S3 sequence gaps | `0` | `0` | `+0` |
| S3 CRC errors | `0` | `0` | `+0` |
| S3 ring frames | `2103` | `2679` | `+576` |
| S3 underruns / overruns | `0 / 0` | `0 / 0` | `+0 / +0` |
| FLX4 USB submitted packets | `210032` | `510032` | `+300000` |
| FLX4 USB completed packets | `210008` | `510008` | `+300000` |
| FLX4 USB skipped / underrun packets | `0 / 0` | `0 / 0` | `+0 / +0` |
| FLX4 USB payload bytes | `74099288` | `179939288` | `+105840000` |

Result: no link corruption, no ring underrun/overrun, and no USB Audio skipped
or underrun packets during the 5-minute product-path soak.

## Operator acceptance -- PASS (2026-07-07)

After the UDP soak, the operator manually checked the live setup and confirmed
that the current XIAO ESP32S3/Sense product path works in practice: FLX4
headphones are audible and the connected controls behaved correctly during the
smoke check. This acceptance was performed with the Wi-Fi debug build still
running, so UART0 was not required for runtime monitoring.

## Product overrun regression fix -- PASS (2026-07-09)

Branch/worktree state: S3 product build from the main worktree, S3 on `COM6`,
P4 product audio build already running on `COM15`, DDJ-FLX4 connected and
audible.

Symptom before the fix:

- `P4_AUDIO_LINK rx blocks` kept increasing and `FLX4_USB_AUDIO submitted` /
  `completed` also kept increasing, proving both the P4 producer and FLX4 USB
  transfer path were alive.
- The S3 `p4_audio_link` ring stayed near full (`ring` close to 4096 frames) and
  `overruns` climbed quickly.
- Packet deltas showed the P4 monitor link producing 48 kHz audio while the
  already-started FLX4 USB Audio packetizer could continue draining at the
  earlier selected rate.

Fix:

- `flx4_usb_audio_poll_ring_autostart()` now also runs the rate-match path while
  already in `FLX4_USB_AUDIO_MODE_RING`.
- When the active `p4_audio_link.sample_rate` differs and the FLX4 format
  supports it, S3 applies the endpoint rate, updates `s_stream_sample_rate`, and
  reinitializes the UAC packetizer so packet sizing matches the P4 monitor
  stream.
- A host regression test covers an already-started ring stream following a
  supported P4 link rate and ignoring an unsupported rate.

Verification:

| Check | Result |
| --- | --- |
| S3 host tests | PASS (`tests/run_s3_host_tests.ps1`) |
| S3 product build | PASS (`build_audio_20260709_s3`, `sdkconfig.defaults;sdkconfig.flx4_hp_e2e`) |
| S3 flash | PASS on `COM6` |
| `P4_AUDIO_LINK rx blocks` | rose from `47` to `21949` during the captured window |
| `P4_AUDIO_LINK gaps / crc` | `0 / 0` throughout the captured window |
| `P4_AUDIO_LINK overruns` | `0` throughout the captured window |
| Ring fill | oscillated below full, roughly `1024` to `2048` frames in steady playback |
| `FLX4_USB_AUDIO submitted / completed` | rose from `5024 / 5000` to `115024 / 115000` |
| `FLX4_USB_AUDIO skipped / underrun` | `0 / 0` throughout the captured window |

Result: the product-path S3 ring no longer overruns during steady FLX4 headphone
playback. The startup `underruns` counter may move while the stream primes, but
the steady-state acceptance condition is zero growth in `overruns`, `gaps`,
`crc`, FLX4 USB `skipped`, and FLX4 USB `underrun`.
