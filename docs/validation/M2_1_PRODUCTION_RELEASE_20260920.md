# M2.1 production release — 2026-09-20

Status: **PASS — exact tagged image built, signed, installed, physically
smoked and published**.

## Frozen source and automated gates

- Annotated immutable tag: `M2.1`
- Source commit: `70824d24dbb1c8d72d19f15797afa2946c5eb909`
- Tag object: `517bacaf04552f591d7f42fe3201ecbe10f2241c`
- Canonical branch: `master`
- Pre-tag GitHub Actions run:
  [`35532529630`](https://github.com/dvucinozd/Pajoniiir/actions/runs/35532529630)
- Toolchain: ESP-IDF v6.0.2

The pre-tag run passed the complete host regression and headless LVGL gates,
the ESP32-P4 firmware build, dependency-lock check and provenance/artifact
generation. The worktree and `dependencies.lock` were unchanged before the
tagged build.

## Exact tagged build and signed artifacts

`git describe --tags --exact-match` returned `M2.1`. A `fullclean` isolated
`build_signed` build then completed successfully and embedded the bare `M2.1`
version.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| `main-deck-p4.bin` | 2,459,664 | `73260a2d529fb7ee5e7f6dbf2c839cb769d3f06e01f56b095d93fdaaa6f39e76` |
| `main-deck-p4.ddjota` | 2,459,852 | `a93f1a4cfab91d4c5f39abba70cfc011183f8b1e2666da219fd1592241ef2425` |
| `manifest.json` | 644 | `ee0ce85565d2126156d6f7dde1ae80225ba3ceb8f969cce4f22682984ef812d9` |
| `manifest.sig` | 64 | `36c9812588532c8d76e0b52dad3000ba1f93ee1c2d714a64b4c0bbfeb727a9ef` |

The application uses 2,459,664 of the 4,194,304-byte OTA slot. Packaging and
independent verification passed with ECDSA P-256/SHA-256 key ID `rel-001`.
No private-key material or hosting credential is stored in Git or the release
artifacts.

## Installation and exact-image smoke

Signed local push OTA returned `{"ok":true,"rebooting":true}` and moved the
device from `M2 / ota_0` to `M2.1 / ota_1`. The service journal records boot
483 with `FIRMWARE_INFO fw=M2.1 partition=ota_1 reset=SW`, followed by USB0
mount, a 324-track library load, FLX4 connection/profile activation and Wi-Fi
startup. No current-boot TWDT or panic appeared. The coredump exposed through
the API is a retained older SDIO assertion and is not from the M2.1 boot.

A 30-second automated dual-deck smoke loaded `House Of Confusion.mp3` and
`Symphony No.6 (1st movement).flac`. Both decks advanced 30,291 ms while USB0,
FLX4 MIDI IN/OUT and UAC stayed active. Deltas remained zero for dropped
blocks, overflow, underflow, packet failures/lost frames, PCM underruns, locked
backend reads and output-late events; UAC data-loss state/flags and current
TWDT state remained clear.

The operator then confirmed that audio, display and touch all worked normally
on the exact installed M2.1 image.

## Publication

The annotated `M2.1` tag was pushed only after exact-image acceptance. Secure
explicit FTPS publication placed the immutable versioned bundle before
switching `latest.json`. Public HTTPS verification returned HTTP 200 for:

- `https://ota.pajoniiir.eu/latest.json`
- `https://ota.pajoniiir.eu/M2.1/main-deck-p4.ddjota`

The public channel declares `M2.1`, 2,459,852 bytes and bundle SHA-256
`a93f1a4cfab91d4c5f39abba70cfc011183f8b1e2666da219fd1592241ef2425`.
An independently downloaded public bundle matched both size and hash.
[`../../deploy/ota/.htaccess`](../../deploy/ota/.htaccess) is the tracked
Apache MIME policy deployed for JSON and `.ddjota` artifacts.

## Accepted limitations and follow-up

- Secure Boot, Flash Encryption and security eFuse provisioning remain
  intentionally disabled on the sole P4 board.
- The shared service credential and absence of an SBOM are accepted decisions
  for M2.1.
- Encrypted offline primary and separately stored encrypted backup copies of
  the signing key are operator-confirmed. A recovery signing test from the
  backup remains an operational follow-up, not a blocker for this release.
- Multi-key trust overlap must be implemented before a planned future signing
  key rotation; emergency replacement retains the wired recovery path.
