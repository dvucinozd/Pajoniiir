# Guition JC8012P4A1 V2 (10.1", 1280x800) - bring-up notes

Branch `jc8012p4a1-v2`, started from `jc1060-hw-bench` at `00041a0d` (shared P4 core with the JC1060 fixes). The board is not in hand yet (expected 2026-10-13 evening). Nothing here has been checked on hardware.

## Source of the facts below

EspControl (ESPHome firmware for Home Assistant), `devices/guition-esp32-p4-jc8012p4a1-v2/device/device.yaml` in https://github.com/jtenniswood/espcontrol (main, read 2026-10-13). Other variants exist in the same repo: `jc8012p4a1`, `jc8012p4a1-v3`, `jc8012p4a1-v3-test`. The V2 file says "V2/new-panel 800x1280 native MIPI DSI panel", so V1 and V2 do not share a display configuration. Check the label on the board before trusting any pin below.

## What EspControl tells us (V2)

| Item | Value |
| --- | --- |
| SoC | ESP32-P4, 16 MB flash, CPU 360 MHz, engineering sample flag set, PSRAM |
| Panel | MIPI DSI, native **800 x 1280 portrait**, used rotated as 1280 x 800 landscape; RGB, 16 bit; pclk 70 MHz; custom init sequence (long register table in the file, needed as is); display reset **GPIO27** |
| DSI PHY power | `esp_ldo` channel 3 at 2.5 V |
| Backlight | LEDC PWM on **GPIO23** |
| Touch | **GSL3680** over I2C bus A: SDA **GPIO7**, SCL **GPIO8**, 400 kHz, no internal pull-ups; reset **GPIO22**, interrupt **GPIO21**; `mirror_x: true`, `swap_xy: false`, `mirror_y: false` (EspControl rebuilt twice for touch scaling and wake-tap noise) |
| Wi-Fi/BLE co-processor | ESP32-C6 over ESP-Hosted SDIO: reset **GPIO54**, CMD **GPIO19**, CLK **GPIO18**, D0-D3 **GPIO14-17**, 40 MHz, active-high reset |
| Serial log | USB-C port is a UART0 bridge, not USB-Serial-JTAG (EspControl sets the logger to UART0) |
| sdkconfig quirks | `CONFIG_ESP_HOSTED_DFLT_TASK_FROM_SPIRAM`, `CONFIG_ESP_HOSTED_USE_MEMPOOL`, main task stack 8192, task WDT 60 s |

Notes: the SDIO pins used for ESP-Hosted here are the same family as the JC1060 (GPIO14-19, 54). EspControl does not use the SD card, so the SD slot wiring is not given.

## Second source: Sukesh's vendor-demo mirror (read 2026-10-13)

https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6 (last push 2025-05-07, a mirror of the Guition demo package: Arduino and ESP-IDF examples, flash tool). It is older than the EspControl V2 file and is for the first revision, so it confirms but does not replace it.

- Board name given by the README: **JC8012P4A1C-I-W-Y1**; ESP32-P4**NRW32** (dual core, up to 400 MHz, 32 MB PSRAM, 16 MB flash) with an **ESP32-C6-MINI-1U-N4** for Wi-Fi and Bluetooth; 10.1" 800 x 1280 capacitive panel; "TF card interface and IO port interface reserved". No codec, Ethernet or USB host layout is stated.
- `pins_config.h` (Arduino examples): `LCD_H_RES 800`, `LCD_V_RES 1280`, `LCD_RST 27`, `LCD_LED 23`, touch `SDA 7`, `SCL 8`, `RST 22`, `INT 21`. This matches EspControl V2 exactly.
- Display driver in the demo: **JD9365** (`esp_lcd_jd9365`), touch **GSL3680** (`esp_lcd_gsl3680`). EspControl V2 uses a custom DSI init, so the JD9365 sequence may differ between revisions; the V2 table is the one to start from.
- The ESP-IDF demo ships a BSP copied from the Espressif P4 Function EV Board (`esp32_p4_function_ev_board`) with the JC8012 pins patched in (`BSP_LCD_BACKLIGHT 23`, `BSP_LCD_RST 27`, touch `22` / `21`, I2C `7` / `8`). It still declares the EV board's **SD pins GPIO39-44** (D0-D3 = 39-42, CLK 43, CMD 44) and **I2S pins GPIO9-13 with a power-amp enable on GPIO20**. These are probably the EV board defaults left in the BSP, NOT proven for the JC8012P4A1: treat the SD and audio pins as unverified until checked on the unit or with a schematic.
- `usb_extend_screen`, `esp_video` and camera components exist in the package, so USB device and MIPI camera are probably wired; USB host layout is still unknown.
- The package contains a `8-Burn operation` flash tool, useful for a vendor-image backup before the first flash.

## What EspControl does NOT tell us (needed for this project)

- USB host layout: which connector is root 0 / root 1, whether the board has two USB-A host ports, a hub or a single port. The DDJ-400 needs the controller, a stick, and USB audio output.
- Ethernet: presence, PHY and RMII pins (JC1060 has Ethernet on GPIO50-52 area).
- Audio: any codec or DAC (PCM5102A / ES8311) and its pins.
- microSD slot wiring (SDMMC slot, pins), and whether it shares the SDMMC host with the C6 as on JC1060 (see E9 in the JC1060 result file).
- Flash partition plan and PSRAM size/speed for this board.
- Power: USB host VBUS control.

Sources to get these: Guition schematic or wiki for JC8012P4A1, the vendor demo above (SD and audio pins are only candidates), and `get_chip_info` / board silkscreen once the unit arrives. Before the first flash, back up the whole 16 MB flash of the new board (same procedure as `RECOVERY_IMAGE.md` for the JC1060).

## Plan

1. Read the board label (V1 / V2 / V3) and confirm the display, touch and SDIO pins above.
2. Add `firmware/boards/jc8012p4a1/bsp_jc8012p4a1` and `firmware/main-deck-jc8012p4a1`, following `firmware/boards/jc1060` and `docs/SHARED_P4_BOARDS.md` (new column for this board, capabilities, geometry 1280 x 800).
3. Bring up display and touch only, then storage, then USB host and audio, then Ethernet if present.
4. Test with the same plan as JC1060 (`docs/validation/jc1060/HARDWARE_TEST_PLAN.md`), keeping the JC1060 defects in mind (SD warm-RST failure, playlist open, loop clamp).
5. Nothing is built or flashed without an explicit GO.
