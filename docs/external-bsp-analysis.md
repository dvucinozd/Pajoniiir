# External BSP Repo Analysis

Repo reviewed locally: `../upstream/esp32_p4_jc4880p433c_bsp`

Source URL: `https://github.com/csvke/esp32_p4_jc4880p433c_bsp`

## Summary

This repository is a strong starting point for the ESP-IDF board support layer. It contains a modular BSP for a very close JC4880 ESP32-P4 board variant, including display, touch, shared I2C, camera control, Kconfig, and ESP Component Manager metadata.

Important naming caveat: the repository name and README say `JC4880P433C`, while several Kconfig symbols and the WiFi architecture document use `JC4880P443C`. Our board is `JC4880P443C_I_W`. Treat this BSP as a candidate base, not as verified compatible, until we build it and run it on the actual board.

## Useful Files

| File | Use for our project |
| --- | --- |
| `src/bsp_display.c` | Best available ESP-IDF starting point for ST7701S MIPI-DSI, LVGL port integration, DSI timing, backlight PWM |
| `src/bsp_touch.c` | GT911 touch init using shared I2C bus |
| `src/bsp_i2c.c` | Centralized I2C bus manager on GPIO 7/8 |
| `src/bsp_camera.c` | Camera I2C/control scaffold; not needed for MVP but useful reference |
| `include/bsp/esp-bsp.h` | Pin/capability definitions and public BSP API shape |
| `Kconfig` | Configurable panel size, DSI bitrate, DPI clock, backlight, I2C and LDO settings |
| `idf_component.yml` | Dependencies for ESP-IDF component manager |
| `WIFI_ARCHITECTURE.md` | ESP32-P4 + ESP32-C6 WiFi architecture through ESP-Hosted |
| `CAMERA_INTEGRATION.md` | Camera driver boundary and shared I2C notes |

## Confirmed BSP Details

Display and touch:

| Function | BSP value |
| --- | --- |
| LCD resolution | `480x800` |
| LCD driver | ST7701S over MIPI DSI |
| MIPI lanes | 2 |
| DSI lane bitrate | 500 Mbps default |
| DPI clock | 34 MHz default |
| Pixel format | RGB565 |
| Frame buffers | 2 default |
| Backlight GPIO | GPIO 23 default |
| Backlight PWM | LEDC, 20 kHz default |
| LCD reset GPIO | GPIO 5 default |
| MIPI DSI PHY power | LDO channel 3, 2500 mV |
| Touch controller | GT911 |
| Touch/shared I2C | SDA GPIO 7, SCL GPIO 8 |
| I2C speed | 400 kHz |
| Touch reset/int | `GPIO_NUM_NC` |

Dependencies from `idf_component.yml`:

| Dependency | Version constraint |
| --- | --- |
| ESP-IDF | `>=5.0.0` in component file; README says 5.5.1 or later |
| `espressif/esp_lcd_st7701` | `^1.1.0` |
| `espressif/esp_lcd_touch` | `^1.1.0` |
| `espressif/esp_lcd_touch_gt911` | `^1.1.0` |
| `espressif/esp_lvgl_port` | `^2.0.0` |

## What Is Already Useful

- We can avoid hand-porting the vendor Arduino display code first.
- The BSP already models display, touch, camera, and shared I2C as separate modules.
- The display code includes an ST7701 vendor init command table, explicit DSI timing, LVGL DSI registration, and backlight brightness API.
- The I2C code centralizes GPIO 7/8 and notes that the bus is shared by GT911 and camera/SCCB.
- The Kconfig file is a good template for our production firmware.

## What Is Not Done In The BSP

Despite the README calling the package production-ready, several important features for our project are not implemented in code:

- `bsp_sdcard_mount()` is currently a stub that logs "SD card mount not yet implemented".
- `bsp_extra_codec_init()` is currently a stub that logs "Audio codec init not yet implemented".
- Button support capability is `BSP_CAPS_BUTTONS 0`.
- SD card capability is `BSP_CAPS_SDCARD 0`.
- Audio capability is declared, but the codec init is not implemented in this repo.
- No CDJ panel I/O, encoders, pitch ADC, MIDI, or DJ playback engine exists.

Conclusion: use this repo for display/touch/I2C/BSP structure, but keep our own SD, audio, input, and deck layers.

## WiFi/ESP32-C6 Findings

`WIFI_ARCHITECTURE.md` documents that ESP32-P4 has no integrated WiFi/Bluetooth and uses the onboard ESP32-C6 through ESP-Hosted.

Documented SDIO mapping:

| ESP-Hosted signal | GPIO |
| --- | --- |
| CMD | GPIO 19 |
| CLK | GPIO 18 |
| D0 | GPIO 14 |
| D1 | GPIO 15 |
| D2 | GPIO 16 |
| D3 | GPIO 17 |
| C6 reset | GPIO 54 |

It also says the RMII pins exposed in the schematic are not used for WiFi:

`GPIO28`, `GPIO29`, `GPIO30`, `GPIO31`, `GPIO34`, `GPIO35`, `GPIO49`, `GPIO50`, `GPIO51`, `GPIO52`.

These are the same family of pins we identified on the JP1 expansion header. For our project this means:

- They may be available for CDJ controls if we do not need Ethernet/RMII.
- They must still be tested on real hardware before assignment.
- We should avoid consuming ESP32-C6 SDIO pins 14-19 and C6 reset 54.

## Recommendation

Use this repo as the starting BSP component for the ESP-IDF firmware skeleton, with these guardrails:

1. Clone or vendor it under `firmware/components/jc4880p4_bsp` after the first board smoke tests.
2. Rename references in our wrapper/docs to `JC4880P443C_I_W` while preserving upstream provenance.
3. Build an example app that calls `bsp_display_start()` and `bsp_display_backlight_on()`.
4. Verify touch and rotation.
5. Add our own SDMMC mount using the local vendor MP3 example pins.
6. Add our own ES8311/I2S codec init using the local vendor MP3 example pins.
7. Add separate `io_panel` and `deck_core` modules rather than adding CDJ behavior into this BSP.

## Impact On Existing Plan

This repo lowers risk for Phase 1 and Phase 2:

- Phase 1 project skeleton can depend on a BSP component instead of starting from empty code.
- Phase 2 display/touch work becomes verification and adaptation, not a full rewrite.

It does not remove the need for:

- SD/audio proof from the vendor examples.
- GPIO/JP1 probing.
- I/O expander selection.
- Native deck/audio/UI implementation.

