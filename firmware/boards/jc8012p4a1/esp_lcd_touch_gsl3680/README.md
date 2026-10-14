# esp_lcd_touch_gsl3680 (vendor copy)

GSL3680 capacitive touch driver for the Guition JC8012P4A1 (I2C 0x40), copied from the vendor
demo package https://github.com/sukesh-ak/JC8012P4A1-GUITION-ESP32-P4_ESP32-C6
(`1-Demo/idf-examples/common_components/esp_lcd_touch_gsl3680/`, 2026-10-13). It contains the
controller's firmware table (`include/esp_lcd_touch_gsl3680.h`) and Silead's point-id code.

**No licence is attached** to this code or to the firmware table. It was taken on the project
owner's explicit decision (2026-10-13, option A) to make the touch panel of the owner's own unit
work. Before any release: get a licence from Guition, or replace it (for example with the
published `guition-jc/esp_lcd_touch_gsl3680` once the project moves to ESP-IDF 6.1). Do not
copy the `espcontrol` `gsl3680` component instead: it is GPLv3 (from `kvj/esphome`).
See `docs/validation/jc8012p4a1/BOARD_NOTES.md`.
