# S3 ESP-IDF 6.0.2 wired boot-chain flash — 2026-08-02

## Scope

This record covers the first full wired flash of the clean S3 RC2 boot chain.
It verifies the written bootloader, partition table, initial OTA data and
application, plus the running application's control-link report. It does not
close the FLX4 MIDI/UAC or broader RC2 hardware acceptance rows.

## Port and exact artifacts

The S3 appeared as `COM10`, `USB Serial Device`, with Espressif
`VID_303A&PID_1001`. Esptool identified an ESP32-S3 revision v0.2 with 8 MiB
embedded PSRAM.

The preserved `firmware/control-board-s3/build_signed` output from the clean
2026-07-30 RC2 release build was used directly. This avoided rebuilding from a
later documentation commit and changing the application version. The raw
application hash exactly matched `CLEAN_RELEASE_RC2_BUILD.md`.

| Address | Artifact | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `0x00000000` | `bootloader.bin` | 21,120 | `FEE252FC5047A9FC0A325D53A6F7FF70F2E79A97F60F02320E6765E26D954941` |
| `0x00008000` | `partition-table.bin` | 3,072 | `5EA93456723B6CF530F437177F6C8D595E22DD1AFE59856B88667ACBCB15ECDF` |
| `0x00010000` | `ota_data_initial.bin` | 8,192 | `7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F` |
| `0x00020000` | `control-board-s3.bin` | 964,288 | `EE9D205A9F7EF3FF4CC10677FB22A069A8C103C550A5048E23A7E90F6A71952B` |

`esptool image-info` reported:

```text
Bootloader Information
Bootloader version: 1
ESP-IDF: v6.0.2
Compile time: Jul 30 2026 10:41:51

Application Information
Project name: control-board-s3
App version: RC2
ESP-IDF: v6.0.2
```

Both image checksums and embedded validation hashes were valid.

## Flash and runtime evidence

The full direct esptool write completed on COM10. It wrote all four regions and
verified the flash hash after each region. The new partition table has no
factory app; `0x20000` is `ota_0`, so the expected post-flash control-link slot
code is 1 rather than the P4's factory slot code 3.

After normal boot, COM10 disappeared because the S3 application switches the
shared USB PHY from serial/JTAG service use to the product's USB-host role. A
P4 restart then received this fresh S3 firmware report:

```text
W ctrl_link: S3 firmware version=RC2 slot=1 state=3
```

The control-link protocol maps slot 1 to `ota_0` and state 3 to `VALID`. This
proves that the newly full-flashed S3 booted its RC2 application and resumed
communication with P4. The bootloader's IDF version is established by the
exact verified bootloader artifact written at `0x0`; its early serial banner
was not retained because the port disconnects when USB host mode starts.

## Result and remaining work

- **Pass:** exact clean S3 RC2 bootloader, partition table, initial OTA data and
  application written and flash-hash verified.
- **Pass:** both flashed executable images identify themselves as ESP-IDF
  v6.0.2; the application identifies itself as `RC2`.
- **Pass:** S3 boots `ota_0`, reports `VALID` and resumes the P4 control link.
- **Pending:** FLX4 enumeration, MIDI IN/OUT and UAC headphone-output smoke on
  the newly wired-flashed S3.
- **Pending:** the remaining ESP-IDF 6.0.2 hardware acceptance matrix.
