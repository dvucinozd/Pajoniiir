# DDJ-FFL4 OTA Update Procedure

Status: **operational and hardware-accepted on 2026-07-13** for both ESP32-P4
and ESP32-S3. This is the operator procedure. See
[`OTA_UPDATE_PLAN.md`](OTA_UPDATE_PLAN.md) for design decisions, partition
layout, acceptance history and rollback fault injection.

## Safety rules

- Update only one processor at a time and wait until it has rebooted cleanly.
- Use only the application `.bin` for the correct target. Never upload a
  bootloader, partition table, `ota_data_initial.bin` or merged flash image.
- Keep stable power throughout upload and the following reboot.
- Do not play audio during an update. P4 stops playback before writing flash.
- Verify project, version, SHA-256 and image size against `manifest.json`.
- Current manifests are **not cryptographically signed**. Distribute release
  folders through a trusted channel until manifest signing is implemented.
- Keep wired serial access available for the first installation of the OTA
  partition layout and for disaster recovery.

## Prerequisites

The boards must already contain the OTA-capable bootloader and partition table.
A legacy single-slot installation cannot acquire a new partition table through
an application-only OTA upload; install that layout once over USB/UART.

Required PC setup:

```powershell
$env:IDF_PATH = "C:\Espressif\frameworks\esp-idf-v5.5\"
. C:\Espressif\Initialize-Idf.ps1
cd D:\Documents\DDJ-FFL4
idf.py --version
```

Before updating, copy any important SD-card configuration and use a release
whose P4 and S3 versions match.

## Build both application images

Use the same build-directory name for both targets. `build_ota3` is the
repository's established release-build name:

```powershell
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -B build_ota3 build

cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -B build_ota3 build
```

Do not package the release unless both builds exit with code 0.

## Create and verify the release package

From the repository root:

```powershell
cd D:\Documents\DDJ-FFL4
.\tools\package_ota_release.ps1 -BuildName build_ota3
```

The script creates `releases\ddj-ffl4-<version>\` containing:

- `main-deck-p4.bin` for the P4;
- `control-board-s3.bin` for the S3;
- `manifest.json` with version, target, project, ESP chip ID, size, slot size
  and SHA-256.

The packager rejects a missing build, wrong project, wrong chip, mismatched
P4/S3 version or image larger than its OTA slot. The limits are 4 MiB for P4
and `0x1e0000` bytes (1.875 MiB) for S3.

Optional independent checksum check:

```powershell
Get-FileHash .\releases\ddj-ffl4-<version>\main-deck-p4.bin -Algorithm SHA256
Get-FileHash .\releases\ddj-ffl4-<version>\control-board-s3.bin -Algorithm SHA256
```

The results must match `manifest.json`.

## Update the P4

1. On the P4 Settings screen enable **Wi-Fi Remote**.
2. Connect the service laptop to SSID `PAJONIIR` using the configured WPA2
   password. The current development default is `12345678`.
3. Open `http://192.168.4.1`.
4. Read the firmware status and note the running slot/version.
5. In the firmware-update section select **`main-deck-p4.bin`**.
6. Confirm the target and start upload. Do not close the page or remove power.
7. Wait for the success response and automatic restart.
8. Reconnect to `PAJONIIR` if Wi-Fi Remote starts after reboot, then refresh
   `http://192.168.4.1/api/firmware`.
9. Confirm that the version is expected, the running slot changed (`ota_0` to
   `ota_1`, or vice versa), and state becomes `valid` after mandatory startup.
10. Confirm display/touch, USB library, MAIN audio, UART heartbeat and S3
    firmware report before updating the S3.

The raw API equivalent is:

```powershell
curl.exe -X POST `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-OTA: p4" `
  --data-binary "@releases\ddj-ffl4-<version>\main-deck-p4.bin" `
  http://192.168.4.1/api/ota/p4
```

The endpoint checks the ESP image header/chip, exact content length, ESP-IDF
image validity and `main-deck-p4` project name before selecting the new slot.

## Update the S3

The S3 Debug AP is runtime-only and returns to OFF after reboot.

1. On the P4 Settings screen enable **S3 DEBUG AP** and wait for status `ON`.
2. Disconnect the laptop from `PAJONIIR` and connect to
   `PajoNiiiR-S3-DEBUG`. The current service AP is intentionally open, so use it
   only during a supervised update and disable it afterward.
3. Open `http://192.168.4.1/update`.
4. Verify the displayed S3 running slot/version.
5. Select **`control-board-s3.bin`**, confirm and start upload.
6. Keep power stable until success and automatic S3 restart.
7. Because the Debug AP is off after reboot, re-enable **S3 DEBUG AP** from P4
   Settings and reconnect the laptop.
8. Open `http://192.168.4.1/api/firmware` and confirm the new version, opposite
   OTA slot and `valid` state.
9. Turn S3 DEBUG AP off and verify FLX4 reconnect, controls, LEDs, UART link and
   USB headphone cue audio.

The raw API equivalent, while connected to the S3 AP, is:

```powershell
curl.exe -X POST `
  -H "Content-Type: application/octet-stream" `
  -H "X-DDJ-OTA: s3" `
  --data-binary "@releases\ddj-ffl4-<version>\control-board-s3.bin" `
  http://192.168.4.1/api/ota/s3
```

The S3 endpoint checks the image header/chip, image validity and
`control-board-s3` project name before changing the boot slot.

## Post-update acceptance

Record these values for both targets:

| Check | Expected result |
| --- | --- |
| Project | `main-deck-p4` or `control-board-s3` as appropriate |
| Version | Same release version on P4 and S3 |
| Running slot | The previously inactive `ota_0` or `ota_1` |
| State | `valid` after required subsystem startup |
| Last error | Empty/none |
| Controller | FLX4 reconnects and sends both-deck controls |
| Audio | PCM5102A MAIN and FLX4 headphone cue operate |
| UI/media | Touch UI and USB library operate |

The 2026-07-13 accepted baseline was `RC1-106-g717b6ab3`, with both targets on
`ota_0 / valid` after the final clean release.

## Failure and rollback behavior

- A wrong target header, wrong chip, wrong project or oversized/invalid image
  is rejected without making it the boot partition.
- An interrupted upload aborts the OTA handle; the current slot remains
  bootable and the partial inactive image is not selected.
- A newly selected image boots as pending verification. Firmware marks it
  valid only after mandatory startup succeeds.
- If the new image resets or fails before confirmation, the ESP-IDF bootloader
  rolls back to the previous valid slot.

After an apparent failure, first read `/api/firmware` and serial logs. Do not
immediately repeat uploads if the device is still confirming or rolling back.

## Wired recovery

Use wired flashing when the board no longer exposes its AP, neither OTA slot
boots, or the partition table/bootloader is missing or damaged. Flash the full
set produced by ESP-IDF (bootloader, partition table, initial OTA data and
application) using the offsets printed by `idf.py build`/`idf.py flash`.

```powershell
cd D:\Documents\DDJ-FFL4\firmware\main-deck-p4
idf.py -p <P4-COM-PORT> flash monitor

cd D:\Documents\DDJ-FFL4\firmware\control-board-s3
idf.py -p <S3-COM-PORT> flash monitor
```

Use the actually detected COM ports; historical port numbers in validation
notes are not guaranteed. Exit the monitor before another process accesses the
same port.

## Release record template

```text
Date/time:
Operator:
Release version:
Manifest SHA-256 checked: yes/no
P4 before -> after slot/version/state:
S3 before -> after slot/version/state:
MAIN audio: pass/fail
Headphone cue: pass/fail
FLX4 controls/LEDs: pass/fail
USB library/UI: pass/fail
Rollback observed: no/yes, details
Notes:
```
