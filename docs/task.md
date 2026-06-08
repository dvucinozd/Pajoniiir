# Task Checklist — Visual Beatgrid Alignment

This checklist tracks progress in the implementation and verification of fine beatgrid alignment in the user interface on the high-resolution scrolling waveform.

- `[x]` 1. Binary search implementation for the beatgrid in `ui.c`
    - `[x]` Add helper function `find_closest_beat_idx(const anlz_metadata_t *meta, uint32_t target_ms)` in `ui.c`
- `[x]` 2. Update the scrolling waveform rendering loop in `ui.c`
    - `[x]` Integrate binary search in `ui_update()`
    - `[x]` Implement visual differentiation for downbeats (`beat_phase == 0`, neon red `0xFF1744`) and regular beats (`beat_phase != 0`, white `0xFFFFFF`)
    - `[x]` Maintain fallback logic for tracks without a beatgrid
- `[x]` 3. Compile, flash, and verify on hardware
    - `[x]` Stop active serial monitor task
    - `[x]` Compile and flash to board `COM15`
    - `[x]` Restart serial monitor and check stability/FPS
    - `[x]` Update `walkthrough.md` with results and observations

- `[x]` 4. S3 Beat LED Feedback & On-Screen Beat Indicator Restoration
    - `[x]` Add `control_link` dependency and optimize beat pre-calculation
    - `[x]` Implement UART transmission of Beat LED states only on changes (caching)
    - `[x]` Reconstruct and restore the on-screen beat indicators (4 dots)
    - `[x]` Fix dot invisibility by removing `lv_obj_remove_style_all()`
    - `[x]` Flash and verify correct rendering on hardware

- `[x]` 5. Library Visual Enhancements and Persistent Blue Highlight
    - `[x]` Remove the first column `#` (index) and redistribute 600px across 4 columns (Title, Artist, BPM, Time)
    - `[x]` Fix row heights to 36px with Montserrat 16 font (exactly 8 visible tracks at a time)
    - `[x]` Trim text intelligently in `ui_fill_library_row` to prevent text overlapping
    - `[x]` Resolve persistent blue highlight (remove `LV_OBJ_FLAG_CLICK_FOCUSABLE` + fix LVGL resets in callback and `ui_update`)
    - `[x]` Compile, flash to ESP32-P4 hardware (COM15), and verify stability with the 308-track database
    - `[x]` Update `walkthrough.md` and commit/push to origin/main repository

- `[x]` 6. Implement 3 Library Sorting Buttons (Artist, Title, BPM)
    - `[x]` Implement `library_sort` qsort function with toggle (ASC/DESC) and secondary criteria in `library.c`
    - `[x]` Create 3 buttons (SORT ARTIST, SORT NAME, SORT BPM) using Montserrat 12 and `s_style_btn_neon` style
    - `[x]` Remove `LV_OBJ_FLAG_CLICK_FOCUSABLE` flags from the new buttons to preserve the persistent highlight
    - `[x]` Preserve current track selection based on `track_id` after library sorting
    - `[x]` Compile, flash to ESP32-P4 hardware (COM15), and verify stability with the 308-track database
    - `[x]` Update `walkthrough.md` and commit/push to origin/main repository

- `[x]` 7. Zoom Waveform Optimization (PSRAM Bandwidth & SRAM I8)
    - `[x]` Initialize `LV_COLOR_FORMAT_I8` and allocate in SRAM in `ui.c`
    - `[x]` Update color palette in `create_screen_overview`
    - `[x]` Optimize `ui_draw_zoom_canvas` (write color index values instead of 16-bit colors)
    - `[x]` Compile and test on Windows simulator
    - `[x]` Compile and flash to ESP32-P4 hardware (COM15)
    - `[x]` Verify audio stability (elimination of static noise) and graphics fluidity

- `[x]` 8. Redesign Upper Static Overview Waveform (Static Waveform I8 Canvas)
    - `[x]` Introduce variables and allocate `s_overview_canvas` in SRAM in `ui.c`
    - `[x]` Initialize color palette and canvas in `create_screen_overview`
    - `[x]` Implement full track and beatgrid rendering in `ui_load_waveform`
    - `[x]` Adjust playhead updates in `ui_update`
    - `[x]` Compile and verify on Windows simulator
    - `[x]` Compile and flash to ESP32-P4 hardware (COM15)
