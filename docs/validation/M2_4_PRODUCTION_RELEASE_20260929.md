# M2.4 production release — 2026-09-29

Status: **PASS — exact tagged image built, signed, installed, production TLS,
cold-boot hardware, functional, acoustic and publication gates passed**.

## Why M2.4 superseded M2.3

`M2.3` was built from `b9d6bf1b5ea4992312669a7679f73a31d011b791`,
signed, installed and published. Its exact image passed the USB, storage,
controller, playback and operator MAIN/cue checks. The first production pull
probe then failed before manifest download with
`ESP_ERR_MBEDTLS_SSL_HANDSHAKE_FAILED`.

The public edge was serving a valid ECDSA chain through a cross-signed root.
ESP-IDF v6.0.2 contained the required ISRG roots, but the M2.3 build had
`CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_CROSS_SIGNED_VERIFY` disabled. M2.4 enables
that certificate-bundle verification mode. CA validation and hostname
verification remain mandatory; no playback, USB, UI, DSP or audio logic changed
between M2.3 and M2.4.

## Frozen source and automated gates

- Annotated immutable tag: `M2.4`
- Source and peeled tag commit: `9d0c954fc502ae237fabedb764368cd9b10f10dc`
- Tag object: `c106f918e3c0919421481cca12ecf94ad7af49fa`
- Fix PR: [#43](https://github.com/dvucinozd/Pajoniiir/pull/43)
- Fix branch head: `ce7c0267f394e8fc16d5040eb8860598518f8a29`
- Required toolchain: ESP-IDF v6.0.2

PR #43 passed the complete host regression, ESP32-P4 firmware build, P4 dual
USB host software gates and CodeRabbit review. The merged source also passed
the `ESP-IDF 6.0.2 migration` workflow in
[run 36503569689](https://github.com/dvucinozd/Pajoniiir/actions/runs/36503569689).
The local full host suite and exact-tag clean firmware build passed before
packaging. The build reported bare version `M2.4`, used the intended P4 silicon
revision settings and left 40% of the 4 MiB OTA slot free.

## Signed artifacts

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.bin` | 2,505,264 | `1bc85aaa2ee26fc5c183ef673e72f6017d87539f4ddd9032f8d403c5bcc2da52` |
| `main-deck-p4.ddjota` | 2,505,452 | `c76b9160bdf9757e04b4b44e92a4f034bf6e913ac35caef7b4fa12092c3e35c4` |
| `manifest.json` | 644 | `b65362edab3c6a4af5d29a709602723136e09bcaf1ef66724355841ed785724e` |
| `manifest.sig` | 64 | `4f313739d5cf698eab06f5920a7e4748e5c348f1b420f66f43f4a2153801b98c` |
| `latest.json` | 210 | `6cb5746b43d33aa04b55331f74a0c89f51b3feefb7c171e5b7bcdfdf6eb37759` |

The bundle and detached manifest signature passed verification against the
firmware trust key `rel-001`. No private key or hosting credential is present in
Git or release assets.

## Installation and TLS acceptance

Signed local push OTA installed M2.4 from M2.3 and returned
`{"ok":true,"rebooting":true}`. Boot 559 ran `M2.4` from `ota_1`, with OTA
state `idle` and an empty `last_error`.

Before changing the public channel, the exact M2.4 image probed the then-public
M2.3 channel. TLS, hostname validation and manifest discovery completed, and
the newer-only policy reported:

```text
older release ignored; use signed local upload to roll back
```

This directly closes the M2.3 cross-signed-chain reproduction. After the
public channel moved to M2.4, a second device probe completed with:

```text
already running this build
```

## Cold boot and exact-image product smoke

The software reboot immediately after push OTA did not enumerate USB0 or USB1,
although the USB host was ready and both root-power bits were set. This repeats
the behavior observed during the M2.3 installation. A full power cycle restored
both roots. The accepted release boot is boot 560 with reset reason `POWERON`,
running `M2.4` from `ota_1`.

The cold-boot snapshot confirmed:

- USB0 mounted with `ESP_OK` and the Rekordbox library available;
- DDJ-FLX4 `0x2B73:0x0045`, active `pioneer_ddj_flx4` profile, MIDI IN/OUT and
  UAC active;
- `root_power_mask=3`, OTA `idle`, empty `last_error`;
- zero USB daemon errors, recovery failures/queue drops, controller probe drops,
  runtime queue failures and service-log drops;
- zero PCM underruns, output-late events, UAC dropped blocks/overflow, UAC
  data-loss flags, packet failures and current TWDT indication.

A controlled dual-deck smoke loaded
`Come To The Light (Original Mix).mp3` on both decks and played for 15 seconds.
Both decks remained in PLAY and advanced about 16.95 seconds. MAIN meter peak
was 25,110 and FLX4 UAC submitted 3,179 blocks. The run ended with both decks
stopped and zero deltas for PCM underruns, output-late, UAC drop/overflow/
underflow/data-loss, packet loss/failure, USB daemon/recovery, runtime queue,
service-log drop and TWDT flags.

For the final exact-image listening gate, Deck 1 played the same track with CH1
PFL enabled. On 2026-09-29 the operator confirmed that both PCM5102A MAIN and
FLX4 cue/headphones were clean. Playback was then stopped, PFL returned OFF and
the final health snapshot remained clean.

The retained coredump still identifies the historical
`sdio_drv.c:1530 (sdio_handle)` event. No new crash or reset occurred during
the M2.4 cold-boot and playback smoke.

## Publication

GitHub Release [M2.4](https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.4)
is the latest non-draft, non-prerelease release. All four release assets were
downloaded again and matched the sizes and hashes above.

The versioned bundle was uploaded before the discovery document, then fetched
through the public HTTPS origin with an exact size/hash match. Only then was
`latest.json` updated. The public channel exposes:

- `https://ota.pajoniiir.eu/latest.json`
- `https://ota.pajoniiir.eu/M2.4/main-deck-p4.ddjota`

The channel advertises M2.4, bundle size 2,505,452 and SHA-256
`c76b9160bdf9757e04b4b44e92a4f034bf6e913ac35caef7b4fa12092c3e35c4`.

## Accepted limitations and follow-up

- Exact-tag repetition of the duplicate raw track-ID hardware gate is not run.
  Its passing evidence remains on `M2.2-37-g751d3c6`.
- Direct comparison of real Rekordbox cue A/C and loop slot/time import is not
  run because the operator removed it from the deployed release scope.
- Automatic USB enumeration after the signed OTA software reboot did not occur;
  a full power cycle restored both roots and all product functions. Treat a
  cold power cycle as the current post-update operating step and investigate
  automatic recovery before a future release that requires unattended OTA.
- Secure Boot, Flash Encryption and security eFuse provisioning remain disabled
  on the sole P4 board. Backup-key recovery signing remains deferred.
- Non-FLX4 profiles remain host evidence only, and the recorder remains
  compiled out.
