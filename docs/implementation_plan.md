# Implementation Plan: Redesigning the Upper Static Overview Waveform Using Canvas

## Problem Description / Request
The user wants to redesign the upper waveform (the static overview waveform that shows the entire track) so that it matches the visual style of the lower zoomed waveform (smooth, high-resolution neon green waveform with a color palette, beatgrid lines in the background, and a playhead/markers on top), replacing the current 32 blocky blue rectangular bars.

---

## Proposed Solution

We will rebuild the upper static waveform using the same approach as the lower zoomed waveform, utilizing **`lv_canvas`** in **`LV_COLOR_FORMAT_I8`** (indexed 8-bit format):
1. **Perfect 400 px Width**: Rekordbox `waveform_low` (the `PWAV` section from the track analysis) contains exactly **400 bytes** of data! This means a canvas with a width of **400 px** and height of **76 px** perfectly maps the full low-res Rekordbox track analysis pixel-for-pixel without any downsampling!
2. **Fast and Safe SRAM Allocation**: The size of the image buffer (`400 * 76 = 30,400` bytes) plus 1024 bytes for the palette is only **~31 KB** (`LV_DRAW_BUF_SIZE(400, 76, LV_COLOR_FORMAT_I8)`). This can be safely allocated in the fast internal SRAM (`MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`), completely avoiding PSRAM.
3. **Excellent Performance (Zero Playback Overhead)**: The canvas is drawn **only once** when the track is loaded (`ui_load_waveform`). During playback, only the playhead `s_overview_playhead` (a 3 px wide red line, which is a separate LVGL object) moves across the canvas using `lv_obj_set_x()`. Thus, this change incurs **zero** memory bus traffic and zero CPU load during playback!
4. **Zipped Background Beatgrid**: We will render all track beats directly in the background of the static overview waveform on the canvas (regular beats in dimmed gray, downbeats at the start of a bar in dimmed red).

---

## Detailed Code Changes

### File: [ui.c](file:///c:/Users/klikn/Documents/AI/CDJXXX/firmware/main-deck-p4/components/ui/ui.c)

#### A. Defining Variables and Constants
We will remove the `s_overview_bars` array and the `WAVEFORM_BARS_COUNT` constant on the Overview screen, and introduce new definitions:
```c
#define OVERVIEW_CV_W 400
#define OVERVIEW_CV_H 76

static lv_obj_t *s_overview_canvas = NULL;
static uint8_t  *s_overview_cv_buf = NULL; // 8 bpp buffer in SRAM (~31 KB)
static int       s_overview_stride_px = OVERVIEW_CV_W;
```

#### B. Adapting Initialization (`create_screen_overview`)
1. Remove the `for (int i = 0; i < WAVEFORM_BARS_COUNT; i++)` loop that created the 32 bars in `wv_border`.
2. Create the `s_overview_canvas` inside `wv_border` with a size of `OVERVIEW_CV_W` and `OVERVIEW_CV_H`, centered at `x=10, y=2`:
   ```c
   size_t ov_sz = LV_DRAW_BUF_SIZE(OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
   #ifndef WIN32
       s_overview_cv_buf = heap_caps_malloc(ov_sz, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT); // SRAM
   #else
       s_overview_cv_buf = malloc(ov_sz);
   #endif
   ```
3. Initialize the canvas and its color palette with 5 colors (the same as the lower zoomed canvas):
   ```c
   s_overview_canvas = lv_canvas_create(wv_border);
   lv_canvas_set_buffer(s_overview_canvas, s_overview_cv_buf, OVERVIEW_CV_W, OVERVIEW_CV_H, LV_COLOR_FORMAT_I8);
   lv_obj_align(s_overview_canvas, LV_ALIGN_TOP_LEFT, 10, 2);
   lv_obj_remove_flag(s_overview_canvas, LV_OBJ_FLAG_CLICKABLE);

   lv_canvas_set_palette(s_overview_canvas, 0, lv_color32_make(0x00, 0x00, 0x00, 0xFF)); // Background (Black)
   lv_canvas_set_palette(s_overview_canvas, 1, lv_color32_make(0x00, 0xFF, 0x88, 0xFF)); // Waveform (Green 0x00FF88)
   lv_canvas_set_palette(s_overview_canvas, 3, lv_color32_make(0x2E, 0x36, 0x40, 0xFF)); // Beat grid (Gray 0x2E3640)
   lv_canvas_set_palette(s_overview_canvas, 4, lv_color32_make(0x6E, 0x20, 0x30, 0xFF)); // Downbeat grid (Red 0x6E2030)
   ```

#### C. Drawing the Full Static Waveform (`ui_load_waveform`)
We will draw the entire track on the canvas once upon loading:
1. Clear the pixel portion of the canvas (`memset` after the 1024-byte palette).
2. Render all beats from the database in the background of the canvas:
   `int x = (meta->beats[b].time_ms * OVERVIEW_CV_W) / track->duration_ms;`
   Draw a thin dimmed vertical line at position `x` with a height of `OVERVIEW_CV_H` for each beat.
3. Draw the 400 waveform columns from `track->waveform_low[x]` in green (`1`):
   Amplitude: `amp = track->waveform_low[x] & 0x1F` (0..31)
   Height: `h = (amp * (OVERVIEW_CV_H - 4)) / 31` (vertically centered).

#### D. Updating the Playhead (`ui_update`)
* In `ui_update()`, instead of calling `ui_update_overview_bars()`, we will calculate the playhead's x-position and apply it directly to `s_overview_playhead`:
   ```c
   if (track && track->duration_ms > 0) {
       int px_x = (int)(((uint64_t)state.position_ms * OVERVIEW_CV_W) / track->duration_ms);
       if (px_x < 0) px_x = 0;
       if (px_x > OVERVIEW_CV_W) px_x = OVERVIEW_CV_W;
       lv_obj_set_pos(s_overview_playhead, 10 + px_x, 2);
   }
   ```

---

## Verification Plan

### 1. Compilation and Execution on the Simulator (Windows)
* Build the simulator using `mingw32-make` in `lv_port_pc_vscode/build/`.
* Run `main.exe` and verify:
  * Does the upper static waveform display as a beautiful neon green high-fidelity canvas (400 px)?
  * Do the background beats render correctly and does the playhead move smoothly without crashes?

### 2. Compilation and Flashing on ESP32-P4 Hardware
* Build the firmware: `idf.py build`.
* Flash to the device: `idf.py -p COM15 flash monitor`.
* Audio and visual verification:
  * Does the audio play without crackling and PSRAM collisions?
  * Does the entire UI look aligned, premium, and beautiful in real-time on the JC4880P443C screen?
