# P4 ESP-IDF 6.0.2 boot and microSD smoke — 2026-08-02

## Scope

This record covers the first wired ESP-IDF 6.0.2 boot-chain flash on the P4
and the microSD regression found after the RC2 applications had been installed
through OTA on both boards. It is a focused boot/microSD smoke, not full RC2
hardware acceptance.

## Initial installed state

- The operator successfully installed `RC2` through OTA on both P4 and S3 and
  confirmed that both versions reported `RC2`.
- A captured P4 boot still reported `ESP-IDF v5.5-dirty 2nd stage bootloader`
  while the application reported `main-deck-p4 RC2`, `slot=ota_0`,
  `state=new`. This is expected because application OTA does not replace the
  bootloader or partition table.
- The P4 Settings page reported `Offline (/sd unavailable)`.
- The boot log repeated:

  ```text
  E SD_HOST: sd_host_create_sdmmc_controller(84): no available sd host controller
  E vfs_fat_sdmmc: host init failed (0x105)
  W bsp: SD mount skipped (ESP_ERR_NOT_FOUND)
  ```

## Card check and repair

The 58.27 GiB card was inspected read-only as `F:` on Windows. It was exFAT,
MBR, 512-byte sectors and readable. Before repair, all 24 original files
(108,857,575 bytes) were copied to:

```text
D:\Documents\Pajoniiir-SD-backup-20260802-020133
```

SHA-256 verification matched every copied source file. `chkdsk F:` found
directory and volume-bitmap inconsistencies but no bad sectors. After the
operator authorised repair, `chkdsk F: /F /X` corrected the filesystem and a
second read-only `chkdsk F:` exited 0 with no remaining problems. All 24
original files still matched the backup. The single recovered fragment
`FOUND.000\FILE0000.CHK` was also copied under the backup's
`recovered-by-chkdsk` directory.

The repaired card still remained offline on RC2, proving that filesystem
damage was not the firmware mount failure's root cause.

## Root cause and fix

On ESP-IDF 6.0.2, ESP-Hosted's SDIO transport initializes the P4's only SDMMC
controller before `app_main()` and uses slot 1. The P4 board-support code then
called the global `sdmmc_host_init()` again for the card in slot 0. The second
global initialization failed with `ESP_ERR_NOT_FOUND` because no second SDMMC
controller exists.

Commit `136aad75` makes the card mount reuse the controller already initialized
by ESP-Hosted while preserving the default slot-aware deinitializer. A static
regression gate in `tests/run_p4_host_tests.ps1` pins this ownership rule.

## Software verification

- `tests/run_p4_host_tests.ps1`: pass, including the new shared-SDMMC static
  gate.
- Isolated P4 build with ESP-IDF v6.0.2: pass.
- Application version: `RC2-3-g136aad7`.
- Application size: `0x250090`; `0x1aff70` bytes (42%) free in the smallest app
  partition.
- Bootloader size: `0x5af0`; `0x510` bytes (5%) free before the partition table.

SHA-256 values for the flashed artifacts:

| Artifact | SHA-256 |
| --- | --- |
| `bootloader.bin` | `ABA466260CAF3F53DE1CAD8F4DC1839E6C9B56718DB0978F68C62C9C5871B466` |
| `partition-table.bin` | `C4DF413D781F3259D2EBAD2D3EB3C12BE6F39D9174D60F9C417D86FE9CED1FB3` |
| `ota_data_initial.bin` | `7D2C7AC4888BFD75CD5F56E8D61F69595121183AFC81556C876732FD3782C62F` |
| `main-deck-p4.bin` | `5356DD224182ABB39B06DD9D73B66DAB738623D557F1B1D73FE3006376400B8E` |

## Wired P4 flash and boot evidence

`idf.py -B build_sdmmc_fix -p COM15 flash` completed successfully against an
ESP32-P4 revision v1.3. Esptool verified every written region: bootloader,
partition table, initial OTA data and factory application.

The post-flash reset log confirmed:

```text
I boot: ESP-IDF v6.0.2 2nd stage bootloader
I boot: Defaulting to factory image
W fw_health: running main-deck-p4 RC2-3-g136aad7 slot=factory
Name: SD
Type: SDHC
Speed: 20.00 MHz (limit: 20.00 MHz)
Size: 59688MB
SSR: bus_width=4
W ctrl_link: S3 firmware version=RC2 slot=1 state=3
```

The previous `no available sd host controller`, mount-retry and
`/sd unavailable` messages were absent. The card information is printed only
after the board-support layer logs a successful `/sd` mount.

## Result and remaining work

- **Pass:** P4 ESP-IDF 6.0.2 bootloader/application boot chain.
- **Pass:** P4 SDMMC controller sharing between ESP-Hosted slot 1 and microSD
  slot 0.
- **Pass:** repaired exFAT microSD mount and card identification.
- **Observed:** P4 receives the S3 heartbeat and reports the S3 application as
  `RC2`, slot 1, state 3.
- **Pending:** visually confirm that the live Settings value has refreshed to
  the online capacity display.
- **Pending:** connect the S3 service USB, perform a full wired ESP-IDF 6.0.2
  flash, and capture its bootloader/application log. Only COM15 and Bluetooth
  serial ports were present during this session, so the S3 bootloader version
  was not directly observable.
- **Pending:** every other row in the ESP-IDF 6.0.2 hardware acceptance matrix.
