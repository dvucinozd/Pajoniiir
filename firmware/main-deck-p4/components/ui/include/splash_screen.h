// splash_screen.h
//
// Provides a simple splash screen for the ESP32 UI using LVGL.  The splash
// displays the text "PajoNiiiR" in the Musieer font and fades the text in
// and out.  After a configurable delay the provided callback is invoked
// to load the main user interface.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "lvgl.h"

/**
 * @brief Show the splash screen.
 *
 * This function creates a new LVGL screen containing a label with the
 * text "PajoNiiiR".  The text is displayed using the Musieer_80 font
 * and animated to fade continuously.  After three seconds the screen
 * is automatically removed and the supplied callback is invoked.
 *
 * @param loaded_cb Function called when the splash has finished
 *        displaying.  Typically this should set up and load the main UI.
 */
void splash_screen_show(void (*loaded_cb)(void));

/**
 * @brief Show the same animation as an idle screensaver.
 *
 * Identical visual to the boot splash, with no timeout and a caption along
 * the bottom. The screen that was active is remembered and restored by
 * splash_screen_screensaver_hide(); it is not deleted, so tab state and any
 * cached rendering survive.
 *
 * Callers that draw outside LVGL - the Overview waveform uses a direct PPA
 * overlay - must re-arm their own redraw after hiding, exactly as they do
 * when switching tabs.
 */
void splash_screen_screensaver_show(void);

/** Restore the screen that was active when the screensaver appeared. */
void splash_screen_screensaver_hide(void);

bool splash_screen_screensaver_active(void);

#ifdef __cplusplus
} /* extern "C" */
#endif
