# Documentation status

Status: **current P4-only source of truth, reconciled 2026-09-23**.

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

| Item | M2.2 value |
| --- | --- |
| Annotated tag | `M2.2` |
| Frozen source | `2c2ec32c253d368765123d7bbf8d37389b790b55` |
| Tag object | `d4e344cab3fb06c43d2b4bf78218032545e1c1ca` |
| Toolchain | ESP-IDF v6.0.2 |
| Installed release record | `M2.2`, `ota_0`, OTA state `idle` and empty `last_error` |
| Application | 2,493,472 bytes; SHA-256 `d2aeced882c1c80c0df4b3a00e371898b3da7a1ffb470bb4be4162b4a2427e9b` |
| Signed OTA bundle | 2,493,660 bytes; SHA-256 `5552d32527e55d7393fe89a49bdf1b753209af8d2a788f9e8d83fbda84ba0676` |
| Public channel | `https://ota.pajoniiir.eu` |
| GitHub Release | `https://github.com/dvucinozd/Pajoniiir/releases/tag/M2.2` |

The installation identity above records the accepted release session; it is
not a claim about a later live boot unless `/api/firmware` is checked again.
Commits after the immutable tag are maintenance/development commits and do not
change the published M2.2 artifact.

## Acceptance summary

M2.2 inherits the completed M2.1 P4 qualification and passed its focused
Wi-Fi Remote gates: exact-tagged build, signature/package verification, signed
OTA installation, opposite-slot boot, 324-track USB0 library, embedded UI and
authoritative SYNC smoke, public-channel verification and GitHub asset
round-trip verification. The production release record is
[`validation/M2_2_PRODUCTION_RELEASE_20260923.md`](validation/M2_2_PRODUCTION_RELEASE_20260923.md).

The retained supporting evidence covers:

- complete lifecycle accounting: 43 PASS, seven explicitly waived I/J cycles,
  zero pending and 39 accepted physical attachment/reconnect actions;
- real MP3/WAV/FLAC playback, FAT32/exFAT and superfloppy/MBR/GPT media;
- dual Master Tempo remediation, UAC idle continuity and a clean
  180.156-minute combined soak with operator-confirmed audio;
- public pull OTA, interrupted-transfer recovery, signed rollback and
  post-reboot dual-USB recovery;
- accepted common 5 V and protected dual-VBUS bench/enclosure wiring.

## Post-release maintenance candidate

Merge image `M2.2-37-g751d3c6` at `751d3c6` has passed a separate
180.058-minute dual-deck soak with operator-confirmed MAIN/cue audio, real-media
load timing, a 324-row catalog reboot/remount check, slow-network mobile
viewport emulation and an API-level MAIN meter decay precheck. This evidence
does not change the published M2.2 identity or public channel.

The candidate still requires real Rekordbox cue/loop comparison, duplicate raw
track-ID isolation across two media, instrumented P4 timeline wrap/handoff
timing, and physical-phone/visible-meter acceptance before a new immutable
release decision. The exact scope and measurements are recorded in
[`validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md`](validation/P4_POST_REVIEW_RELEASE_QUALIFICATION_20260925.md).

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
