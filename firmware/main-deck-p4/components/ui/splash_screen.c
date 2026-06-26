// splash_screen.c
//
// Implementation of the splash screen for the ESP32 UI.  This code
// relies on LVGL for creating and animating the splash.  The splash
// shows the word "PajoNiiiR" in the Musieer font and fades the text
// in and out using a repeating animation.  After a fixed delay the
// provided callback is invoked to load the main user interface.

#include "splash_screen.h"
#include "Musieer_80.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "splash";

// Keep a handle to the splash screen and container so we can clean up later
static lv_obj_t *splash_scr = NULL;
static lv_obj_t *splash_container = NULL;

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
        splash_container = NULL;
    }
    
    // 3. Stop and delete the timer
    lv_timer_del(timer);
}

/**
 * @see splash_screen.h
 */
void splash_screen_show(void (*loaded_cb)(void))
{
    ESP_LOGI(TAG, "Displaying splash screen with text 'PajoNiiiR'...");

    // Create a new blank screen for the splash and store it globally so we can clean up later
    splash_scr = lv_obj_create(NULL);
    lv_obj_clear_flag(splash_scr, LV_OBJ_FLAG_SCROLLABLE);
    
    // Set background color to black (COL_BG in ui_theme is dark, but let's make it explicitly black for splash)
    lv_obj_set_style_bg_color(splash_scr, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(splash_scr, LV_OPA_COVER, 0);

    // Create a container for each character.  Use the flex layout to arrange labels horizontally.
    splash_container = lv_obj_create(splash_scr);
    lv_obj_clear_flag(splash_container, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_bg_opa(splash_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_pad_all(splash_container, 0, 0);
    lv_obj_set_style_border_width(splash_container, 0, 0);
    lv_obj_set_size(splash_container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);

    // Use flex layout in LVGL 9
    lv_obj_set_flex_flow(splash_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(splash_container, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    static const char *splash_text = "PajoNiiiR";
    const size_t len = strlen(splash_text);
    // Create a label for each character
    for (size_t i = 0; i < len; i++) {
        lv_obj_t *lbl = lv_label_create(splash_container);
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
    lv_obj_center(splash_container);

    // Create a one-shot timer to end the splash after 3 seconds.  The timer will call timer_cb.
    (void)lv_timer_create(timer_cb, 3000, (void *)loaded_cb);

    // Load the splash screen using LVGL 9 screen load
    lv_screen_load(splash_scr);
}
