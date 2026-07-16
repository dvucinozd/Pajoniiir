# Signed OTA Deployment — RC1-131

Document status: completed deployment and boot-health record.

- Date: 2026-07-16
- Source commit: `c391e306` (`master`)
- Application version: `RC1-131-gc391e306`
- Signing key ID: `rel-001`
- Algorithm: ECDSA P-256 with SHA-256

## Scope

This record covers isolated signed release builds, cryptographic package
verification, sequential P4/S3 OTA installation and post-reboot firmware/status
checks. It does **not** claim the targeted Phase 20 or Beat FX Flanger/Delay
functional hardware smoke; the last complete functional hardware baseline
remains `RC1-123-g587cd7a1` from 2026-07-14.

## Release artifacts

Both targets were built from clean isolated `build_signed` directories with
ESP-IDF v5.5 and matching application versions. The release helper verified the
target metadata, slot limits, bundle signatures and signed outer manifest.

| Target | Image bytes | Image SHA-256 | Bundle bytes | Bundle SHA-256 | Slot limit |
| --- | ---: | --- | ---: | --- | ---: |
| P4 | 2,136,064 (`0x209800`) | `8b46890402767a58dffd2316c166c618d24d6eac44b681a0fa0c0f49be40ee15` | 2,136,252 | `98ba97b2928e5ea4c566a12417704f60c43f80fd1bc0a1cc9caf5ab496f47924` | 4,194,304 |
| S3 | 943,360 (`0xe6500`) | `adf79ee018d02a73619a5ea134177d0d7cd9aff3f7c7090c51c3459b3abb9409` | 943,548 | `b80f7208338c4d85bb4d88a26157ed886a85d4e26f2f488fe0f82d39cd492d06` | 1,966,080 |

## P4 deployment

- Before: `ota_0 / RC1-126-g812ad70f`.
- The signed `main-deck-p4.ddjota` upload sent 2,136,252 bytes and returned
  HTTP 200 with `{"ok":true,"rebooting":true}`.
- After: `ota_1 / RC1-131-gc391e306`.
- `/api/firmware` remained stable across repeated post-boot reads with an empty
  `last_error`; `/api/status` responded with both decks `IDLE`, no active audio
  output and healthy allocated Echo/Delay diagnostics.

The P4 endpoint's top-level `state` is the OTA transfer-service state (`idle`
after reboot), not the ESP-IDF image state. Mandatory startup reaches
`firmware_health_mark_ready()` only after the required subsystems initialize;
failure there aborts/restarts instead of silently accepting the new image.

## S3 deployment

- Before: `ota_1 / RC1-123-g587cd7a1`.
- The signed `control-board-s3.ddjota` upload sent 943,548 bytes and returned
  HTTP 200 with `{"ok":true,"rebooting":true}`.
- The S3 Debug AP turned off after reboot as designed.
- After reconnecting to the P4 Wi-Fi Remote, the authoritative periodic S3
  firmware report was read twice as
  `ota_0 / valid / RC1-131-gc391e306`.
- P4 remained on `ota_1 / RC1-131-gc391e306`, so both processors finished on
  one matching release; P4 reported no OTA error. The nested S3 report carries
  slot/state/version, not S3's local `last_error` field.

## Deferred functional acceptance

The controller was not present in the final `/api/status` snapshot and no track
was loaded. Therefore this deployment record does not close:

- FLX4 enumeration, controls, reconnect and LED feedback;
- PCM5102A MAIN playback or FLX4 USB headphone cue;
- Beat FX Flanger/Delay sound and target routing, plus Delay beat timing, depth
  response or Echo/Delay switching;
- the Phase 20 queue-pressure, web/profile mutation and long UART-link smokes;
- display/touch/media operation by physical observation.

Those checks remain in `STARTUP_CHECKLIST.md` and `POST_R5_PLAN.md`.
