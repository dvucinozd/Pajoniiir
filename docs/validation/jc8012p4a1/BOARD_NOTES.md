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

## Schematic findings (vendor schematic PNGs, `5-Schematic/` of the same mirror, read 2026-10-13)

Sheets read: `1_PWR`, `3_ESP32-P4`, `4_CONN`, `6_USB`, `7_CODEC`. This supersedes the "unverified" remarks above for SD and audio. The schematic is of the first revision (JC8012P4A1C-I-W-Y1); check that V2 did not change it.

**USB: three Type-C connectors.**
- `USB2` "Full Speed USB": P4 FS PHY on **GPIO24 / GPIO25** (`USB1P1_N` / `USB1P1_P`), with ESD diodes.
- `USB3` "High speed USB": the P4 HS PHY pins **DM / DP** (`ESP_USB_N` / `ESP_USB_P`).
- `USB1` "USB-TTL": a **CH340C** bridge to **UART0** (GPIO37 TX / GPIO38 RX), with RTS/DTR transistors on **BOOTMODE** and **CHIP_PU**: the serial port should be flashable with esptool auto-reset (to be confirmed; the JC1060 needed a manual BOOT + RST).
- All three connectors have 5.1 kOhm on CC and their **VBUS tied to `USB5V_IN`**, which is the board's 5 V input (it feeds the IP5306 charger / battery boost and then the 3.3 V buck). No VBUS source switch or overcurrent limiter appears on these sheets: the board does not power a downstream device. Expect the same wiring as on the JC1060: DDJ-400 and the stick on a **self-powered hub** (or a powered USB-C OTG adapter), the hub also feeding the board. Do not back-feed from two supplies at once.
- Mapping to the project's two USB roots to be checked on the unit: the HS port suits the DDJ-400 (high-speed UAC1/MIDI, like the JC1060 root 0), the FS port suits the stick but is limited to 12 Mbit/s (library reads and Link downloads will be slower than on the JC1060).

**No Ethernet** (confirmed by the operator and by the schematic: no PHY, no RMII). Network is Wi-Fi through the C6 only, or nothing. The project's web UI, Link and OTA paths all assume Ethernet on the JC1060, and the JC1060 notes say Wi-Fi enable/disable is out of scope there (panic family with the SD host); this board therefore needs its own network decision (Wi-Fi AP/STA, or no web service).

**Audio codec: ES8311** (DAC/ADC), with an **NS4150** class-D speaker amplifier (mono speaker connector `CN3`) and an analog MEMS mic (MSM381A). No headphone or line output is drawn; the DAC output goes to the speaker amp only. I2S0: **MCLK GPIO13, SCLK GPIO12, LRCK GPIO10, DSDIN (P4 output) GPIO9, codec output to P4 on GPIO11** (labelled `ES7210_SDOUT`); amp enable `PA_CTRL` **GPIO20**; codec control on the I2C bus **GPIO7 / GPIO8** (shared with the RTC and the touch FPC).

**microSD ("TF CARD", SDMMC slot 0):** D0-D3 **GPIO39-42**, CLK **GPIO43**, CMD **GPIO44**, 5.1 kOhm pull-ups to **`ESP_LDO_VO4`**, i.e. the card is supplied and signalled from the P4's internal LDO channel 4, not from a fixed rail. Note for E9: the JC1060 mount failure after a warm RST may be tied to that kind of on-chip LDO supply (the card keeps its state while the LDO is re-initialised); hypothesis only, to compare with the JC1060 schematic.

**C6 (ESP-Hosted):** SDIO on `SD2_D0-D3`, CLK, CMD = **GPIO14-19** (SDMMC slot 1), `C6_CHIP_PU` on **GPIO54**, `C6_IO2`.

**Other:** touch FPC `TOUCH_RST` GPIO22 / `TOUCH_INT` GPIO21 (matches the two other sources); backlight `LCD_PWM` GPIO23; display reset GPIO27; one **WS2812** LED on `WS2812_DAT` (GPIO26 by my reading of the P4 sheet); **RX8025T RTC** with a CR1220 cell on the shared I2C; CSI camera connector (MIPI, with its own I2C pair); expansion headers on GPIO2-5, 45-48 and 28-34; battery connector with an IP5306; UART0 header `CN2`; four test pads S1-S4.

Reading caveats: the PNGs were read visually, not from the CAD data, and the GPIO number of a few labels (WS2812, `C6_IO2`) is my reading of the pin table; re-check the ones you rely on.

## Network decision (operator, 2026-10-13): Wi-Fi through the ESP32-C6

No Ethernet, so the web service, Link and OTA go over Wi-Fi on the C6 via ESP-Hosted (SDIO slot 1, GPIO14-19, reset GPIO54).

- The shared core already has this path on two other boards: JC4880 (Wi-Fi available, default OFF) and M3 (available, default ON, WPA2 APSTA), see `docs/SHARED_P4_BOARDS.md`, with `CONFIG_ESP_HOSTED_CP_TARGET_ESP32C6`, `..._SDIO_HOST_INTERFACE`, `..._SDIO_SLOT_1`, `..._SDIO_4_BIT_BUS` in their `sdkconfig.defaults`. This board should follow the JC4880/M3 model, not the JC1060 one (JC1060: `wifi` capability false, Hosted linked but never started).
- Same SDMMC topology as the JC4880: Hosted on slot 1 owns the single SDMMC controller (registered before `app_main()` under IDF 6) and the microSD sits on slot 0, so the BSP must skip a second `sdmmc_host_init()` and keep the slot-aware deinit, exactly as `firmware/boards/jc4880/bsp_jc4880/bsp_jc4880.c` does (see its comment around the `bsp_sdmmc_host_already_initialized` hook, and its 3-attempt SD mount retry).
- Risk to carry from the JC1060 campaign: E9 (SD mount fails on about every other warm RST, with an `SD_HOST ... slot registered` error) happens on a board where Hosted is linked but Wi-Fi is never started. With Wi-Fi really running, the SD/Hosted interaction must be tested from the first bring-up, and the JC1060 note for the shared-core owner applies to this board too. The reference fork also panicked when Wi-Fi was disabled at run time on the JC1060 (`sd_host_del_sdmmc_controller`), so do not toggle Wi-Fi off and on without a recovery image.
- Decided (operator, 2026-10-13): Wi-Fi **default ON**, **AP+STA, WPA2**, i.e. the M3 policy (`wifi` capability true, `wifi_apsta` true, `wifi_wpa3_transition` false). In the capability table this board gets the same Wi-Fi column as the M3: "available, default ON, WPA2 APSTA".
- Still open: how credentials are entered for the STA side (the AP reaches the web UI, as on the M3), and OTA over Wi-Fi.

## What is still unknown (needed for this project)

- Which P4 USB root the project calls "controller" and "storage" on this layout, and whether the FS port can carry the library (speed).
- STA credential flow and OTA over Wi-Fi; the Wi-Fi defaults are decided (see "Network decision").
- Whether V2 differs from the first-revision schematic (display init, any pin).
- SD and C6 on separate SDMMC slots here (0 and 1), but still the same SD host driver: the E9 interaction is not ruled out.
- Flash partition plan and PSRAM size/speed for this board.
- Power: USB host VBUS control.

Sources to get these: Guition schematic or wiki for JC8012P4A1, the vendor demo above (SD and audio pins are only candidates), and `get_chip_info` / board silkscreen once the unit arrives. Before the first flash, back up the whole 16 MB flash of the new board (same procedure as `RECOVERY_IMAGE.md` for the JC1060).

## Scaffold status (2026-10-13, nothing built or run)

Committed on this branch, all of it **uncompiled for the target** (no `idf.py` was run, per the no-build-without-GO rule); only the host tests and a preprocessor check were run:

- Shared core: `BOARD_JC8012P4A1` in `board_capabilities` (row, name, `get()`), Kconfig choice `PAJONIIIR_BOARD_JC8012P4A1` (Wi-Fi remote default ON), `board_adapter.h` panel size 800 x 1280, `ui_overview.c` geometry 1280 x 800. `tests/board_adapter/test_board_capabilities.c` now covers the fourth board in all configurations (also added to `tests/run_p4_host_tests.ps1`); the four existing configurations still pass. `tools/check_board_build.py` accepts `main-deck-jc8012p4a1`.
- `firmware/boards/jc8012p4a1/bsp_jc8012p4a1`: display only (JD9365, component default table, GPIO27 reset, GPIO23 LEDC backlight, LDO channel 3). Everything else comes from `jc-common/board_peripherals.c`, where two gated changes were made: codec input GPIO11 and amplifier enable GPIO20 for this board, and a touch stub that logs a warning and returns OK (the caller uses `ESP_ERROR_CHECK`). Preprocessing the file with the other boards' flags gives output identical to before the change.
- `firmware/main-deck-jc8012p4a1`: CMake project (allow-list copied from the JC1060, no Ethernet component), `app_main.c`, `sdkconfig.defaults` (PCM5102A and ES8311 off), `partitions.csv` (same as the JC1060), README. No `dependencies.lock` yet.

Findings while scaffolding:

1. **Touch driver.** `guition-jc/esp_lcd_touch_gsl3680` 0.1.0 (published 2026-10-13, MIT code, I2C address 0x40, `esp_lcd_touch_new_i2c_gsl3680()`) is the only GSL3680 driver found. It requires **ESP-IDF >=6.1.0,<6.2.0** and `esp_lcd_touch ==1.2.1`; this project pins 6.0.2. Its `NOTICE` says the 100 kB controller firmware table has **no upstream licence**, so it should not be copied into this repository. Options: move this board to IDF 6.1 (a project-wide pin question), ask Guition to relax the IDF range, or fetch the component as a dependency at build time. The board runs display-only until then, which is enough to test audio, USB, storage and Wi-Fi.
2. **Licensing.** The EspControl repository is **PolyForm Noncommercial 1.0.0**. Pin numbers and the board description were used as facts; its display register table and timings were **not** copied. The display uses the Espressif component's Apache-2.0 defaults.
3. **UI at 1280 x 800.** `ui.c` takes the size from `board_capabilities`, but `ui_overview.c` and `dj_ui.c` have 800 and 1024 layouts; a 1280 x 800 layout (waveforms, deck panels, library) is untested and probably needs work. The UI simulator (`tests/ui_simulator`) can render at a given size without hardware once LVGL 9.5.0 is available; not run.
4. **Chip and memory.** The common defaults require chip revision >= v1.0 (the JC1060 reported v1.3) and 200 MHz PSRAM; the board has 32 MB PSRAM. Read the boot log of the first run.
5. **First-run risk.** The SD card is supplied from the same on-chip LDO channel 4 (`ESP_LDO_VO4`) as on the other boards and ESP-Hosted shares the SDMMC controller (Wi-Fi really running here): the E9 SD mount failure seen on the JC1060 may reproduce.

## Plan

1. Read the board label (V1 / V2 / V3) and confirm the display, touch and SDIO pins above.
2. Add `firmware/boards/jc8012p4a1/bsp_jc8012p4a1` and `firmware/main-deck-jc8012p4a1`, following `firmware/boards/jc1060` and `docs/SHARED_P4_BOARDS.md` (new column for this board, capabilities, geometry 1280 x 800).
3. Bring up display and touch only, then storage, then USB host and audio, then Ethernet if present.
4. Test with the same plan as JC1060 (`docs/validation/jc1060/HARDWARE_TEST_PLAN.md`), keeping the JC1060 defects in mind (SD warm-RST failure, playlist open, loop clamp).
5. Nothing is built or flashed without an explicit GO.
