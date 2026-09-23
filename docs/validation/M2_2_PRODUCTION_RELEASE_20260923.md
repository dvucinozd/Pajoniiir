# M2.2 production release — 2026-09-23

Status: **PASS — exact tagged image built, signed, installed, hardware-smoked
and published**.

## Frozen source and automated gates

- Annotated immutable tag: `M2.2`
- Source commit: `2c2ec32c253d368765123d7bbf8d37389b790b55`
- Tag object: `d4e344cab3fb06c43d2b4bf78218032545e1c1ca`
- Toolchain: ESP-IDF v6.0.2
- Pre-tag GitHub Actions run:
  [`35792104604`](https://github.com/dvucinozd/Pajoniiir/actions/runs/35792104604)

The hosted run passed the complete P4 host regression, seven M2.2 browser
contract tests, UI simulator screenshots, ESP32-P4 build, dependency-lock and
binary/provenance gates. The tag points at the same tested commit.

## Exact tagged build and signed artifacts

An isolated `build_signed` full-clean build reported the bare application
version `M2.2` and fit the 4 MiB OTA slot with 41% free.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.bin` | 2,493,472 | `d2aeced882c1c80c0df4b3a00e371898b3da7a1ffb470bb4be4162b4a2427e9b` |
| `main-deck-p4.ddjota` | 2,493,660 | `5552d32527e55d7393fe89a49bdf1b753209af8d2a788f9e8d83fbda84ba0676` |
| `manifest.json` | 644 | `f5609f4140f652137020534d82bef793a42f00b40715da132d256c881d9887b0` |
| `manifest.sig` | 64 | `80d4083d63f5390f7188a0fca68ae5ad3c9dc9f3a6d580e11aa35d52b01a5042` |

Packaging and independent verification passed with ECDSA P-256/SHA-256 key
ID `rel-001`. No private key or hosting credential is in Git or release assets.

## Installation and exact-image smoke

The first transfer attempt was interrupted after 658,590 of 2,493,472 image
bytes. Firmware reported `HTTP upload interrupted`, retained the running slot
and did not activate the incomplete image. After reconnecting the Windows
client to the service AP, a complete signed upload returned
`{"ok":true,"rebooting":true}`.

The device then reported `M2.2` on `ota_0`, OTA state `idle` and an empty
`last_error`. Exact-image smoke confirmed:

- embedded page title `Pajoniiir M2.2 Web Controller`;
- USB host ready with zero daemon errors;
- USB0 mounted with `ESP_OK` and a coherent 324-track library;
- zero PCM underruns, output-late events and current UAC data-loss state;
- no current TWDT indication;
- authoritative D1 SYNC changed ON and returned OFF through marked POST/API
  state without fabricated browser state.

Before tagging, the operator visually accepted the source-identical candidate
UI as excellent. Desktop, phone portrait and phone landscape real-browser
smoke had zero JavaScript errors or warnings after the final CSS correction.
M2.2 changes the embedded Wi-Fi Remote and its SYNC API only; M2.1 remains the
qualification authority for unchanged dual-deck audio, USB lifecycle and the
three-hour combined soak.

## Publication

`master` and the peeled `M2.2` tag both resolve to `2c2ec32`. GitHub Release
[`M2.2`](https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.2) is the latest
non-draft, non-prerelease release. All four assets were downloaded again and
their sizes and hashes matched the table above.

The versioned bundle was uploaded first and independently downloaded over
HTTPS with the expected size and SHA-256. Only then was `latest.json` switched.
The public channel now exposes:

- `https://ota.pajoniiir.eu/latest.json`
- `https://ota.pajoniiir.eu/M2.2/main-deck-p4.ddjota`

The channel declares release `M2.2`, size 2,493,660 and bundle SHA-256
`5552d32527e55d7393fe89a49bdf1b753209af8d2a788f9e8d83fbda84ba0676`.

## Accepted limitations

M2.2 carries forward the documented M2.1 limitations: shared service
credential, disabled Secure Boot/Flash Encryption/security eFuses on the sole
P4 board, deferred signing-backup recovery exercise, operator-waived lifecycle
cases, and no physical qualification of non-FLX4 controller profiles. See
[`../DOCUMENTATION_STATUS.md`](../DOCUMENTATION_STATUS.md) and
[`../RISK_REGISTER.md`](../RISK_REGISTER.md).
