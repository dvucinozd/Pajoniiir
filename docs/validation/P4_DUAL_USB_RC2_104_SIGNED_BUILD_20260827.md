# P4 dual-USB RC2-104 signed build — 2026-08-27

> **Superseded later the same day.** This record captures the last dual-target
> build before active S3 retirement. The P4-only replacement evidence is
> [`P4_ONLY_RC2_104_BUILD_20260827.md`](P4_ONLY_RC2_104_BUILD_20260827.md).
> Do not use the overlay or dual-target package commands below for current
> builds.

## Scope

This record covers a clean, signed build of commit `6e41d7f9f5634b5dd36ca664aea50be265fe12c3`
on branch `feat/p4-dual-usb-host`. Both targets were built with ESP-IDF v6.0.2
and report the identical application version `RC2-104-g6e41d7f`.

The P4 build explicitly used `sdkconfig.p4_local_controller`. A first generic
`sdkconfig.defaults` build selected `p4_local_controller_disabled.c`; it was
discarded and the same isolated directory was cleaned before the feature image
was rebuilt. The accepted P4 `sdkconfig` contains
`CONFIG_PAJONIIIR_P4_LOCAL_CONTROLLER=y`, the pre-v3 silicon selector and
`CONFIG_ESP32P4_REV_MIN_FULL=100`.

Commands:

```powershell
. C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1

Set-Location firmware\main-deck-p4
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig `
  -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.p4_local_controller" build

Set-Location ..\control-board-s3
idf.py -B build_signed fullclean
idf.py -B build_signed -D SDKCONFIG=build_signed/sdkconfig build

Set-Location ..\..
.\tools\package_ota_release.ps1
```

## Raw application images

| Target | Bytes | OTA slot | Slot free | SHA-256 |
| --- | ---: | ---: | ---: | --- |
| P4 | 2,490,368 | 4,194,304 | 1,703,936 | `29293a38eebe1e1372006de8283ffd22d77cb17d8a29a316c0136687bf444134` |
| S3 | 972,784 | 1,966,080 | 993,296 | `6282d5cbcd200b2e84bd4a3ca246efcd53bfcf898e7750e0fc7c9f87476ffc26` |

The P4 image also passes the stricter 3,670,016-byte P4-local migration budget
with 1,179,648 bytes of headroom. Both clean builds exited with code 0. The P4
build emitted the already-known unused `s_i2s_tx` warning from the retired
speaker path; ESP-IDF also emitted non-fatal upstream Kconfig notifications.

## Signed package

`tools/package_ota_release.ps1` created the ignored local directory
`releases/pajoniiir-RC2-104-g6e41d7f/` using key ID `rel-001` and
ECDSA-P256-SHA256.

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.ddjota` | 2,490,556 | `42a8a786441d7d2e9f230aa66735fca393c4daf8db9e3eb9c58bcbcd85861111` |
| `control-board-s3.ddjota` | 972,972 | `7174d2e84bd774c74314b2162babb36fc9c9ca6a4c96593e4765b8709f403e06` |
| `manifest.json` | 1,105 | `9eeb1c9c6e7d4327f74df57513169ac4b255f0be0bbd7bb040f0de843b7fdf29` |
| `manifest.sig` | 64 | `b08ff9db88a089f2172b3b94567e711aa7287dfd9b153ff8a8c39dc9e29f44b8` |

The packager validated both ESP image headers, target chip IDs, project names,
slot sizes and matching versions. It then verified both generated `.ddjota`
bundles against the committed public key and verified `manifest.sig` over the
outer manifest before exiting with code 0.

## Boundary

Nothing was flashed, installed or uploaded. This record proves clean build,
slot fit, signing and cryptographic package verification only. Protected VBUS,
dual-root enumeration, direct MIDI/UAC behavior, reconnect and sustained-load
hardware acceptance remain open.
