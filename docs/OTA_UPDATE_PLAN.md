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
2. Add P4 HTTP streaming OTA to the existing Wi-Fi Remote web server.
3. Test P4 success, interrupted upload, invalid image, and rollback paths.
4. Add S3 HTTP streaming OTA to the existing S3 Debug AP.
5. Report S3 version/update state to P4 and test S3 rollback.
6. Add signed manifests and a combined release package after both independent
   update paths are stable.

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
