# Documentation status

Status: **current P4-only source of truth, reconciled 2026-09-20**.

## Product boundary

The active product has one firmware and release target:
`firmware/main-deck-p4`. The ESP32-P4 owns USB0 Rekordbox storage, direct USB1
DDJ-FLX4 MIDI/audio, controller profiles and LEDs, dual-deck playback,
mixer/DSP, PCM5102A MAIN, FLX4 headphone cue, LVGL UI, Wi-Fi Remote and signed
OTA. No S3 firmware, UART control link or inter-board PCM bridge belongs to the
active product.

Compatibility names such as `control_link`, `S3CP` and `profile.s3bin` remain
in current code/file formats. They do not imply an active S3 processor.

## Production release

| Item | M2.1 value |
| --- | --- |
| Annotated tag | `M2.1` |
| Frozen source | `70824d24dbb1c8d72d19f15797afa2946c5eb909` |
| Tag object | `517bacaf04552f591d7f42fe3201ecbe10f2241c` |
| Toolchain | ESP-IDF v6.0.2 |
| Installed release record | `M2.1`, `ota_1`, service-log boot `483` |
| Application | 2,459,664 bytes; SHA-256 `73260a2d529fb7ee5e7f6dbf2c839cb769d3f06e01f56b095d93fdaaa6f39e76` |
| Signed OTA bundle | 2,459,852 bytes; SHA-256 `a93f1a4cfab91d4c5f39abba70cfc011183f8b1e2666da219fd1592241ef2425` |
| Public channel | `https://ota.pajoniiir.eu` |
| GitHub Release | `https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.1` |

The installation identity above records the accepted release session; it is
not a claim about a later live boot unless `/api/firmware` is checked again.
Commits after the immutable tag are maintenance/development commits and do not
change the published M2.1 artifact.

## Acceptance summary

M2.1 inherits the completed P4 qualification and passed its own exact-tagged
build, signature/package verification, signed OTA installation, opposite-slot
boot, USB0/USB1 recovery, dual-deck strict-counter smoke and operator-confirmed
audio/display/touch smoke. The production release record is
[`validation/M2_1_PRODUCTION_RELEASE_20260920.md`](validation/M2_1_PRODUCTION_RELEASE_20260920.md).

The retained supporting evidence covers:

- complete lifecycle accounting: 43 PASS, seven explicitly waived I/J cycles,
  zero pending and 39 accepted physical attachment/reconnect actions;
- real MP3/WAV/FLAC playback, FAT32/exFAT and superfloppy/MBR/GPT media;
- dual Master Tempo remediation, UAC idle continuity and a clean
  180.156-minute combined soak with operator-confirmed audio;
- public pull OTA, interrupted-transfer recovery, signed rollback and
  post-reboot dual-USB recovery;
- accepted common 5 V and protected dual-VBUS bench/enclosure wiring.

## Accepted limitations

- I3-I5 and J2-J5 remain operator-waived; they are not reported as passes.
- Numeric enclosure thermal/RF/strain margins were not captured. Repeat
  qualification after any enclosure, wiring, supply or RF-layout change.
- The shared service credential is accepted for this deployment. WPA2/WPA3
  transition mode with PMF capability is enabled; per-device credentials and
  WPA3-only/PMF-required mode remain future hardening.
- Secure Boot, Flash Encryption and security eFuse provisioning are disabled
  because the sole board has no sacrificial provisioning/recovery pilot.
- Encrypted offline primary and separate encrypted backup signing-key copies
  are operator-confirmed. Recovery signing from the backup remains deferred
  maintenance, not completed evidence.
- The recorder is compiled out. Non-FLX4 profiles are host evidence only.

## Source-of-truth order

1. active firmware, tests, build configuration and immutable release artifacts;
2. this status, `PROJECT_OVERVIEW.md`, `ARCHITECTURE.md`,
   `HARDWARE_WIRING.md`, `OTA-UPDATE.md` and
   `SECURITY_PROVISIONING_POLICY.md`;
3. `STARTUP_CHECKLIST.md`, `DEVELOPMENT_PLAN.md` and `RISK_REGISTER.md`;
4. retained dated validation records;
5. Git history for removed superseded plans and intermediate evidence.
