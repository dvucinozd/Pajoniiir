# DDJ-FFL4 OTA Update Plan

## Implementation status

Batch 1 completed and hardware-smoked on 2026-07-13:

- P4 boots from the 4 MB `factory` recovery slot at `0x20000`;
- S3 boots from the 1.875 MB `ota_0` slot at `0x20000`, reported `VALID`, and
  completed subsystem startup;
- both targets were fully wired-flashed with the rollback bootloader,
  partition table, initial OTA data, and application;
- P4 HTTP upload, S3 HTTP upload, and intentional rollback-cycle testing were
  left for the following development batches.

Batch 2 implementation and hardware OTA smoke are complete:

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

Batch 4 implementation and hardware OTA smoke are complete:

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
and showed no reset or panic during the observation window. HTTP acceptance
then passed for `ota_0 -> ota_1 -> ota_0`, including wrong-target/chip rejection
and an interrupted 64 KiB upload that left the current image bootable. A
deliberately non-confirming image then proved rollback from `ota_1` to the prior
valid `ota_0` image.

Batch 5 firmware reporting was implemented and hardware-smoked on 2026-07-13:

- S3 publishes a `0xA6 FIRMWARE_REPORT` immediately and every five seconds with
  its running slot, ESP-IDF image state, and application version;
- P4 keeps the latest report and logs changes without blocking its UART RX task;
- P4 Settings displays real P4 and S3 version/slot/state values;
- P4 `GET /api/firmware` adds the S3 status while keeping the existing P4 fields;
- shared codec, protocol-parity, UI helper, and both complete host suites pass.

After both targets were wired-flashed, a P4-only restart recovered the periodic
S3 report at 3150 ms: `version=RC1-104-g2f710fb7-dirty slot=1 state=3`, meaning
`ota_0 / VALID`. The later AP acceptance independently confirmed both OTA slots
and the P4-visible S3 report after reboot.

An unsigned combined release package tool is also available at
`tools/package_ota_release.ps1`. It checks project metadata, binary chip IDs,
slot limits, and matching P4/S3 versions, then emits both images plus a
deterministic SHA-256 manifest under ignored `releases/`. Cryptographic manifest
signing remains Batch 6 work.

Batch 3 and Batch 4 AP testing completed on 2026-07-13 while the development
laptop retained Internet access over its wired `Ethernet 2` route and dedicated
its Wi-Fi adapter to each target AP in turn.

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
2. Add P4 HTTP streaming OTA to the existing Wi-Fi Remote web server. Code and
   hardware A/B smoke complete.
3. Test P4 success, interrupted upload, invalid image, and rollback paths.
   All four paths and physical power-loss interruption pass.
4. Add S3 HTTP streaming OTA to the existing S3 Debug AP. Code and hardware A/B
   smoke complete.
5. Report S3 version/update state to P4 and test S3 rollback. Reporting, wired
   smoke, and forced rollback complete.
6. Add signed manifests after both independent update paths are stable. The
   unsigned checked release package is complete.

P4-to-S3 firmware forwarding over the UART `0xA6` layer is deferred. Controller
profile transfer has shown checksum/retry pressure, so independent Wi-Fi OTA is
the safer first implementation for firmware-sized payloads.

## Required acceptance tests before enclosure

- [x] full wired flash boots both new partition layouts;
- [x] running slot, image version, and image state are logged correctly;
- [x] valid P4 and S3 OTA images boot as `PENDING_VERIFY` and are marked valid
  only after mandatory startup completes;
- [x] deliberately non-confirming P4 and S3 images roll back to the prior slot;
- [x] client disconnect after 64 KiB aborts the inactive-slot write and leaves
  the current image bootable on both processors;
- [x] physical power loss during an inactive-slot write leaves the current image
  bootable on both processors;
- [x] missing/wrong target, undersized image, and wrong-chip image are rejected
  before activation on both processors;
- [x] a valid target header with a declared image size one byte over the slot
  limit is rejected with `ESP_ERR_INVALID_SIZE` before flash activation on both
  processors;
- [x] two successive A/B update cycles pass on each processor (`P4 factory ->
  ota_0 -> ota_1`; `S3 ota_0 -> ota_1 -> ota_0`).

The 2026-07-13 AP acceptance used release `RC1-105-gf1c176e2`. Final status via
the P4 endpoint was P4 `ota_1 / valid` and S3 `ota_0 / valid`, with the same
version reported for both targets.

Forced rollback is reproducible without timing a manual reset. The shared
`CONFIG_DDJ_OTA_FORCE_ROLLBACK_TEST` option defaults off. Test builds explicitly
include `firmware/common/sdkconfig.rollback_test.defaults`, use a clearly marked
`ROLLBACK-TEST-*` application version, and restart only when the running image is
`PENDING_VERIFY`. On 2026-07-13 the P4 test image booted once from `ota_0` and
returned to valid `ota_1`; the S3 test image booted once from `ota_1` and returned
to valid `ota_0`. Normal P4 and S3 build configurations were separately checked
to confirm the option remained unset.

Physical power-loss acceptance also passed on 2026-07-13. P4 lost power during
an upload to inactive `ota_0` and returned to valid `ota_1`. The first S3 attempt
completed before power was removed and was not counted; the repeated upload was
limited to 20 KiB/s, lost power while writing inactive `ota_0`, and returned to
valid `ota_1` without activating the partial image.

After acceptance, both targets were restored over OTA to the clean packaged
release `RC1-106-g717b6ab3`. Final P4 status reported `ota_0 / valid`, and its
embedded S3 report independently confirmed `ota_0 / valid` with the same version.
