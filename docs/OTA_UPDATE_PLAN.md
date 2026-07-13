# DDJ-FFL4 OTA Update Plan

## Implementation status

Batch 1 completed and hardware-smoked on 2026-07-13:

- P4 boots from the 4 MB `factory` recovery slot at `0x20000`;
- S3 boots from the 1.875 MB `ota_0` slot at `0x20000`, reported `VALID`, and
  completed subsystem startup;
- both targets were fully wired-flashed with the rollback bootloader,
  partition table, initial OTA data, and application;
- P4 HTTP upload, S3 HTTP upload, and intentional rollback-cycle testing remain
  for the following development batches.

Batch 2 implementation is now present pending hardware OTA smoke:

- `GET /api/firmware` reports the running/target slot, version, byte progress,
  state, and last error;
- `POST /api/ota/p4` accepts a raw P4 application `.bin` with the mandatory
  `X-DDJ-OTA: p4` header;
- the handler validates the 24-byte ESP header and ESP32-P4 chip ID before
  touching flash, stops playback, streams 4 KB chunks into the inactive slot,
  requires the exact declared content length, calls ESP-IDF image validation,
  requires the `main-deck-p4` project name, selects the new boot partition,
  replies to the client, and restarts after a short delay;
- the Wi-Fi Remote page exposes firmware status, file selection, upload
  progress, confirmation, and failure text.

Batch 4 implementation is now present pending hardware OTA smoke:

- `GET /api/firmware` on the S3 Debug AP reports the S3 running slot, version,
  upload progress, state, and last error;
- `POST /api/ota/s3` accepts a raw S3 application `.bin` only with the
  mandatory `X-DDJ-OTA: s3` header;
- the first 24-byte ESP image header is buffered and checked for the ESP32-S3
  chip ID before flash erase begins;
- the completed image must pass ESP-IDF validation and report the
  `control-board-s3` project name before its slot can become bootable;
- the dedicated `/update` page is separate from the long-lived `/events` log
  stream, exposes progress and confirmation, and restarts S3 after success;
- upload receive timeouts are bounded, and an interrupted transfer aborts the
  write handle without changing the current boot partition.

The Batch 4 image was wired-flashed to S3 on 2026-07-13. Boot verification
reported `slot=ota_0`, `state=valid`, completed mandatory subsystem startup,
and showed no reset or panic during the observation window. HTTP A/B and
rollback behavior remain pending.

Batch 5 firmware reporting was implemented and hardware-smoked on 2026-07-13:

- S3 publishes a `0xA6 FIRMWARE_REPORT` immediately and every five seconds with
  its running slot, ESP-IDF image state, and application version;
- P4 keeps the latest report and logs changes without blocking its UART RX task;
- P4 Settings displays real P4 and S3 version/slot/state values;
- P4 `GET /api/firmware` adds the S3 status while keeping the existing P4 fields;
- shared codec, protocol-parity, UI helper, and both complete host suites pass.

After both targets were wired-flashed, a P4-only restart recovered the periodic
S3 report at 3150 ms: `version=RC1-104-g2f710fb7-dirty slot=1 state=3`, meaning
`ota_0 / VALID`. S3 rollback testing remains pending with the AP acceptance
work.

An unsigned combined release package tool is also available at
`tools/package_ota_release.ps1`. It checks project metadata, binary chip IDs,
slot limits, and matching P4/S3 versions, then emits both images plus a
deterministic SHA-256 manifest under ignored `releases/`. Cryptographic manifest
signing remains Batch 6 work.

Batch 3 hardware testing was intentionally deferred until the development
laptop can retain Internet access over Ethernet while its Wi-Fi adapter is
connected to the target AP. The same setup will be used for the S3 Batch 4
hardware smoke.

## Safety baseline

OTA support uses ESP-IDF bootloader rollback on both processors. The first OTA
layout must be installed with a full wired flash of the bootloader, partition
table, and application before the S3 and P4 are enclosed. An application-only
update must never attempt to migrate the legacy single-app partition table.

The shared `firmware_health` component inspects and logs the running image at
boot. A newly selected OTA image remains `PENDING_VERIFY` until the complete
mandatory startup path reaches `firmware_health_mark_ready()`. A reset before
that point causes the bootloader to roll back on the following boot.

## Partition layouts

### ESP32-P4 (16 MB)

- factory recovery: 4 MB
- OTA slot 0: 4 MB
- OTA slot 1: 4 MB
- NVS, PHY init, OTA selection data, and coredump partitions

The factory image is the wired recovery baseline. Normal OTA updates alternate
between `ota_0` and `ota_1`.

### ESP32-S3 (4 MB)

- OTA slot 0: 1.875 MB
- OTA slot 1: 1.875 MB
- NVS, PHY init, OTA selection data, and coredump partitions

There is no separate S3 factory slot because retaining two adequately sized
update slots is more important on the 4 MB device. Wired bootloader mode remains
the final recovery path.

## Delivery batches

1. Install and hardware-smoke the dual-slot layouts and rollback health gate.
2. Add P4 HTTP streaming OTA to the existing Wi-Fi Remote web server. Code
   complete; hardware A/B smoke pending.
3. Test P4 success, interrupted upload, invalid image, and rollback paths.
4. Add S3 HTTP streaming OTA to the existing S3 Debug AP. Code complete;
   hardware A/B smoke pending.
5. Report S3 version/update state to P4 and test S3 rollback. Reporting and
   wired smoke complete; rollback test pending.
6. Add signed manifests after both independent update paths are stable. The
   unsigned checked release package is complete.

P4-to-S3 firmware forwarding over the UART `0xA6` layer is deferred. Controller
profile transfer has shown checksum/retry pressure, so independent Wi-Fi OTA is
the safer first implementation for firmware-sized payloads.

## Required acceptance tests before enclosure

- [x] full wired flash boots both new partition layouts;
- [x] running slot, image version, and image state are logged correctly;
- a valid OTA image boots and is marked valid only after startup completes;
- a deliberately non-confirming image rolls back to the prior slot;
- power loss during upload leaves the current image bootable;
- wrong-target and oversized images are rejected before activation;
- at least two successive A/B update cycles pass on each processor.
