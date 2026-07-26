// splash_screen.c
//
// Implementation of the splash screen for the ESP32 UI.  This code
// relies on LVGL for creating and animating the splash.  The splash
// shows the word "Pajoniiir" in the Musieer font and fades the text
// in and out using a repeating animation.  After a fixed delay the
// provided callback is invoked to load the main user interface.

#include "splash_screen.h"
#include "Musieer_80.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "splash";

// Keep a handle to the splash screen and container so we can clean up later
static lv_obj_t *splash_scr = NULL;

/**
 * Animation execution callback.  Updates the opacity of an individual
 * character label.  By staggering the start delay of animations on
 * each label we create a moving fade highlight across the text.
 */
static void anim_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
    lv_obj_invalidate((lv_obj_t *)var);
}

/**
 * Timer callback invoked when the splash screen duration has expired.
 * Deletes the splash screen and calls the user-supplied callback to
 * load the main UI.
 */
static void timer_cb(lv_timer_t *timer)
{
    // Retrieve the callback from the timer's user_data
    void (*loaded_cb)(void) = (void (*)(void))lv_timer_get_user_data(timer);
    
    ESP_LOGI(TAG, "Splash screen duration expired. Cleaning up...");

    // 1. First, load the main UI so it becomes the active screen
    if (loaded_cb) {
        loaded_cb();
    }

    // 2. Now that main UI is active, we can safely delete the splash screen
    if (splash_scr) {
        lv_obj_del(splash_scr);
        splash_scr = NULL;
    }
    
    // 3. Stop and delete the timer
    lv_timer_del(timer);
}

/**
 * @see splash_screen.h
 */
/*
 * Build the animated wordmark on a fresh screen. Shared by the boot splash and
 * the idle screensaver so the two can never drift apart visually; `caption` is
 * NULL at boot and carries the screensaver's prompt otherwise.
 */
static lv_obj_t *splash_build(const char *caption);

void splash_screen_show(void (*loaded_cb)(void))
{
    ESP_LOGI(TAG, "Displaying splash screen with text 'Pajoniiir'...");

    splash_scr = splash_build(NULL);

    // Create a one-shot timer to end the splash after 3 seconds.  The timer will call timer_cb.
    (void)lv_timer_create(timer_cb, 3000, (void *)loaded_cb);

    // Load the splash screen using LVGL 9 screen load
    lv_screen_load(splash_scr);
}

/* ── Idle screensaver ─────────────────────────────────────────────────────── */

static lv_obj_t *saver_scr = NULL;
static lv_obj_t *saver_prev_scr = NULL;

void splash_screen_screensaver_show(void)
{
    if (saver_scr) return;
    saver_prev_scr = lv_screen_active();
    saver_scr = splash_build("press any button or don't...");
    if (!saver_scr) return;
    lv_screen_load(saver_scr);
    ESP_LOGI(TAG, "screensaver on");
}

void splash_screen_screensaver_hide(void)
{
    if (!saver_scr) return;
    /* Load the previous screen first: deleting the screen LVGL is currently
     * showing leaves it drawing into freed objects. */
    if (saver_prev_scr) {
        lv_screen_load(saver_prev_scr);
    }
    lv_obj_del(saver_scr);   /* also cancels the per-label opacity animations */
    saver_scr = NULL;
    saver_prev_scr = NULL;
    ESP_LOGI(TAG, "screensaver off");
}

bool splash_screen_screensaver_active(void)
{
    return saver_scr != NULL;
}

static lv_obj_t *splash_build(const char *caption)
{
    // Create a new blank screen for the splash and store it globally so we can clean up later
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    
    // Set background color to black (COL_BG in ui_theme is dark, but let's make it explicitly black for splash)
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    // Create a container for each character.  Use the flex layout to arrange labels horizontally.
    lv_obj_t *container = lv_obj_create(scr);
    lv_obj_clear_flag(container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(container, 0, 0);
    lv_obj_set_style_border_width(container, 0, 0);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Use flex layout in LVGL 9
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char *splash_text = "Pajoniiir";
    const size_t len = strlen(splash_text);
    // Create a label for each character
    for (size_t i = 0; i < len; i++) {
        lv_obj_t *lbl = lv_label_create(container);
        char buf[2];
        buf[0] = splash_text[i];
        buf[1] = '\0';
        lv_label_set_text(lbl, buf);
        lv_obj_set_style_text_font(lbl, &Musieer_80, 0);
        // Set text color to white
        lv_obj_set_style_text_color(lbl, lv_color_hex(0xffffff), 0);
        // Set initial text opacity to dimmed
        lv_obj_set_style_text_opa(lbl, 60, 0);

        // Create animation to vary the opacity.  Each label gets a different delay
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, lbl);
        lv_anim_set_exec_cb(&a, anim_opa_cb);
        // Animate from dimmed (60) to fully opaque (255)
        lv_anim_set_values(&a, 60, 255);
        // Duration of a single light sweep in milliseconds (LVGL 9 duration API)
        lv_anim_set_duration(&a, 800);
        lv_anim_set_reverse_duration(&a, 800);
        lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
        // Stagger the start of each character's animation to create a moving highlight
        lv_anim_set_delay(&a, i * 120);
        lv_anim_set_path_cb(&a, lv_anim_path_linear);
        lv_anim_start(&a);
    }

    // Center the container on the screen
    lv_obj_center(container);

    if (caption) {
        /* Deliberately static: the wordmark already animates nine labels, and
         * the 2026-07-09 stability pass showed this panel is sensitive to the
         * invalidate budget. A second animation buys nothing. */
        lv_obj_t *cap = lv_label_create(scr);
        lv_label_set_text(cap, caption);
        lv_obj_set_style_text_font(cap, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(cap, lv_color_hex(0xB0B0B0), 0);
        lv_obj_align(cap, LV_ALIGN_BOTTOM_MID, 0, -24);
    }

    return scr;
}
