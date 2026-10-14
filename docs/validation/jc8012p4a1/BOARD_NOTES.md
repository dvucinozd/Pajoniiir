# Guition JC8012P4A1 V3 (10.1", 1280x800) - bring-up notes

Branch `jc8012p4a1-v3` (renamed from `jc8012p4a1-v2` once the label showed V3), started from `jc1060-hw-bench` at `00041a0d` (shared P4 core with the JC1060 fixes). The board arrived on 2026-10-13; only the identity and factory-boot facts in "The unit in hand" come from the unit. Everything else here is still unchecked on hardware.

## Source of the facts below

EspControl (ESPHome firmware for Home Assistant), `devices/guition-esp32-p4-jc8012p4a1-v2/device/device.yaml` in https://github.com/jtenniswood/espcontrol (main, read 2026-10-13). Other variants exist in the same repo: `jc8012p4a1`, `jc8012p4a1-v3`, `jc8012p4a1-v3-test`. The V2 file says "V2/new-panel 800x1280 native MIPI DSI panel", so V1 and V2 do not share a display configuration. Check the label on the board before trusting any pin below.

## The unit in hand (2026-10-13)

Label: `SKU:10153001-V3 (2638)`, `P4-10.1 inch Capacitive touch`, `Models:JC8012P4A1C_I_W_Y`, `Resolution: 800*1280`. Everything below was read from the unit over the CH340 USB-C port; nothing was written to it.

| Item | Value |
| --- | --- |
| Chip | ESP32-P4 **revision v3.2** (ROM `esp32p4-eco7-20260109`), dual core + LP core, 400 MHz, eFuse block revision v1.2 |
| MAC | `80:f1:b2:d8:0c:b1` |
| Flash | 16 MB, manufacturer 0x68, device 0x4018, QIO 40 MHz |
| PSRAM | 32 MB, 200 MHz, hex (X16) |
| Factory image | `esp_brookesia_demo` 1.0.1, built with ESP-IDF v6.0.2 (same version as this project), Wi-Fi enabled by default |
| Factory partitions | `nvs` 0xb000/0x6000, `phy_init` 0x11000, `factory` 0x20000/0x700000, `storage` 0x720000/0x800000 |
| Factory flash backup | 16 777 216 bytes, SHA-256 `51e4ff9b4c924919ebb682f11908fcf8545cc434e4ffec5e8e4946e53950ec3b` (kept outside git) |

Factory boot log, relevant lines:
- Display: `jd9365: version: 2.0.2`, `LCD ID: 93 65 04`, `Display resolution 800x1280`. The user confirms the factory picture is shown. This is the same Espressif component this project uses, but whether the demo passes its own init table is not known from the log.
- Touch: the GSL3680 loads its firmware (`load fw success`) but then logs `startup_chip failed read 0xb0 = 5a,5a,5a,5a`. The user reports the tablet works under the factory image, so the log line is not proof that touch is broken.
- SD: `sdmmc_card_init failed (0x107)`; **no card was inserted**, so this is the expected empty-slot result, not the E9 defect.
- ES8311 codec opens; the C6 is reset through GPIO54, SDIO runs at 40 MHz on GPIO14-19 and the slave is identified as `esp32c6`. This confirms the C6 pins above.

EspControl publishes a separate `jc8012p4a1-v3` device file. Compared with its V2 file (both read 2026-10-13), pins, DSI timings (70 MHz, 2 lanes, 840 Mbps) and the touch setup are the same; only the display init sequence and the `engineering_sample` flag differ (V3 targets production silicon), and its default screen rotation is 180. The init table is not copied (licence, see below). Our rotation of 270 is still unverified.

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
- Whether the V3 differs from the first-revision schematic (display init, any pin).
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

1. (Done: V3, chip v3.2.) Confirm the display, touch and SDIO pins above.
2. Add `firmware/boards/jc8012p4a1/bsp_jc8012p4a1` and `firmware/main-deck-jc8012p4a1`, following `firmware/boards/jc1060` and `docs/SHARED_P4_BOARDS.md` (new column for this board, capabilities, geometry 1280 x 800).
3. Bring up display and touch only, then storage, then USB host and audio, then Ethernet if present.
4. Test with the same plan as JC1060 (`docs/validation/jc1060/HARDWARE_TEST_PLAN.md`), keeping the JC1060 defects in mind (SD warm-RST failure, playlist open, loop clamp).
5. Nothing is built or flashed without an explicit GO.

## UI at 1280 x 800 in the PC simulator (2026-10-13)

`tests/ui_simulator/CMakeLists.txt` has a new target `ui_product_jc8012_e2e` (1280 x 800, board policy JC8012P4A1). Built on this Linux host with `cmake` + `ninja` against the LVGL 9.5.0 checkout in `firmware/main-deck-jc1060/managed_components/lvgl__lvgl` (not the pinned commit the `run_ui_simulator_e2e.ps1` gate downloads, so this is a visual check, not the gate; no baseline manifest was added). The scenario passes and writes 21 captures; four are kept in `evidence/ui-preview-1280x800/`.

What the unchanged UI does at 1280 x 800:

| Page | Result |
| --- | --- |
| Overview | Fills the screen. Waveforms, deck headers, FX column and the bottom info strip stretch to the width; nothing cut off. |
| Hot cues | Fills the screen; the eight pads scale. |
| Library | **Not adapted.** Fixed 8-row viewport (`UI_LIBRARY_PAGE_ROWS` / `DJ_LIB_ROWS` = 8, 330 px), buttons at the top: the lower ~45 % of the screen is empty. |
| Settings | **Not adapted.** Panels are top-aligned at the 1024 x 600 positions; the lower ~40 % is empty. |
| Screensaver, network pages | Captured, not reviewed yet. |

Changing the library row count touches shared arrays and paging used by the other boards (`UI_LIBRARY_PAGE_ROWS`, `DJ_LIB_ROWS`, `s_page_track_keys`, the table cell model and the page keys), so it is a design decision, not a one-line fix: options are (a) accept the empty space for the first bring-up, (b) scale the viewport and row count at build time for tall panels only, (c) a dedicated 1280 x 800 layout. Not started; the baselines of the existing boards must stay unchanged.

## First flash results (2026-10-13)

First build of `main-deck-jc8012p4a1` (app 0x26e2b0 bytes in a 0x400000 partition) flashed over the native USB-JTAG port (`/dev/ttyACM0`, because the CH340 UART could not reset the board). Bootloader, partition table, otadata and app verified by hash.

- The common defaults (written for the JC1060's v1.3) selected the "< v3.0" family, which caps the supported chip revision at v1.99. This board's override file now requires v3.1 or later (`CONFIG_ESP32P4_REV_MIN_301`), the IDF default. v3.0 would also enable a MSPI workaround that only revision 3.0 needs.
- **LEDC fault.** With that fixed the bootloader sees chip v3.2 and starts the app, which then dies in `ledc_channel_config()` (called from `bsp_display_init`) with `Store access fault` (MCAUSE 7) at the LEDC gamma RAM, MTVAL `0x500d3400` on channel 0 with the automatic clock, `0x500d3440` on channel 1 with the XTAL clock. The vendor image drives the same pin (GPIO23) with the same API on channel 1 and does not fault. Cause not found: ESP-IDF 6.0.2 has the same gamma RAM address for every P4 revision and no separate gamma clock gate. The BSP now drives the backlight as a plain on/off GPIO; PWM dimming is open.
- After the GPIO change the app no longer crashes, but `/dev/ttyACM0` disappears right after boot: the native USB-JTAG pins (GPIO24/25) are the FS USB-C port, which the firmware takes over as USB host. Logs must be read on the CH340 UART.

## Display init table (2026-10-13)

Three tables were tried on the V3 unit with the same DPI timing family:
1. Espressif `esp_lcd_jd9365` 2.0.2 default: boots cleanly, screen black with the backlight on.
2. The older table in the Guition demo repository (`sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6`, 198 entries) with that demo's 60 MHz and porches 8/20: coloured vertical bands, and the same bands on a direct framebuffer test pattern (so not an LVGL or rotation problem).
3. **The table the unit's own factory firmware uses**: 204 `jd9365_lcd_init_cmd_t` entries found in the 16 MB backup (`esp_brookesia_demo` 1.0.1, array at flash `0x4b890` in the app image, data pointers resolved through the app's segment table; arguments the firmware keeps in zero-initialised storage are not in the image and are 0x00, the Espressif default table has 0x00 at the same positions). With it and the component's default timing and 1500 Mbps lanes, the Pajoniiir UI is displayed.

It lives in `firmware/boards/jc8012p4a1/bsp_jc8012p4a1/jd9365_guition_init.inc`. **Licence caveat:** no licence is attached to the vendor firmware or table; the project owner decided to use it for this unit. Before any release, get a licence or a documented sequence from Guition. The factory image has a second 171-entry table, identical to the Espressif default, used for another panel.

Not yet checked: orientation (rotation 270 assumed), colours, UI at 1280x800, touch (not integrated), backlight dimming (GPIO on/off only, LEDC faults on this chip).
