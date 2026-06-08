# Framework Decision: ESP-IDF With Arduino Smoke Tests

## Decision

Use ESP-IDF as the main production framework for the JC4880P443C_I_W port.

Use Arduino only as a short bring-up and smoke-test environment for the vendor examples that are already included with the board package.

## Rationale

The target project is not a small display demo. It needs coordinated control over:

- MIPI DSI display with ST7701S.
- GT911 capacitive touch.
- LVGL UI.
- SDMMC storage.
- ES8311 codec and I2S audio output.
- UART link to the ESP32-S3 control/MIDI board.
- USB host mode for future USB stick support.
- FreeRTOS task timing for UI, audio, storage, input scanning, and LED feedback.

ESP-IDF is the better fit because these low-level peripherals are first-class Espressif APIs there. Arduino is useful because the board vendor examples already prove the pins and basic peripherals, but Arduino should not become the long-term architecture for the standalone deck firmware.

## How We Will Use Arduino

Arduino is limited to early validation:

1. Flash the supplied `Wifi_scan` example to confirm board upload and serial output.
2. Flash the supplied LVGL example to confirm display and touch.
3. Flash the supplied `mp3_player` example to confirm SDMMC, ES8311, I2S, and speaker output.
4. Record working pins and any setup quirks into `docs/bench-notes.md`.

After these tests, new product firmware should move to ESP-IDF.

## How We Will Use ESP-IDF

ESP-IDF is used for the actual project:

- Board support package for JC4880P443C_I_W.
- Native LVGL UI in landscape `800x480`.
- SD card media browser.
- Audio playback pipeline.
- CDJ front-panel I/O and optional TinyUSB MIDI compatibility on the ESP32-S3 control board.
- Future USB mass-storage host support.

## Version Guidance

Start with the ESP-IDF version expected by the vendor package, which currently says ESP-IDF 5.4 or newer in `JC4880P443C_I_W/1-Demo/idf_examples/README.md`.

The external BSP repo `upstream/esp32_p4_jc4880p433c_bsp` says ESP-IDF 5.5.1 or later in its README, while its `idf_component.yml` allows `>=5.0.0`. Practical starting rule:

1. Try ESP-IDF 5.5.1 first for the ESP-IDF skeleton because it matches the external BSP documentation.
2. Fall back to ESP-IDF 5.4 if the local vendor examples require it.
3. Upgrade only after display, touch, SD, and audio are stable.

## Practical Rule

If the work is "prove this board feature works", Arduino is acceptable.

If the work is "build the DJ deck firmware", use ESP-IDF.
