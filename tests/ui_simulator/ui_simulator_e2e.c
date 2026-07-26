#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "splash_screen.h"

#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#define TICK_STEP_MS 16u

static uint32_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static lv_display_t *s_display;
static int s_failures;

static void flush_cb(lv_display_t *display, const lv_area_t *area,
                     uint8_t *pixels)
{
    (void)area;
    (void)pixels;
    lv_display_flush_ready(display);
}

static void fail(const char *message)
{
    fprintf(stderr, "FAIL: %s\n", message);
    s_failures++;
}

static void pump(uint32_t duration_ms)
{
    uint32_t elapsed = 0;
    while (elapsed < duration_ms) {
        lv_tick_inc(TICK_STEP_MS);
        (void)lv_timer_handler();
        elapsed += TICK_STEP_MS;
    }
    lv_refr_now(s_display);
}

static lv_obj_t *find_visible_label(lv_obj_t *root, const char *text)
{
    if (!root || !text) {
        return NULL;
    }
    if (lv_obj_check_type(root, &lv_label_class) && lv_obj_is_visible(root)) {
        const char *value = lv_label_get_text(root);
        if (value && strcmp(value, text) == 0) {
            return root;
        }
    }
    uint32_t count = lv_obj_get_child_count(root);
    for (uint32_t i = 0; i < count; i++) {
        lv_obj_t *found = find_visible_label(lv_obj_get_child(root, (int32_t)i), text);
        if (found) {
            return found;
        }
    }
    return NULL;
}

static bool click_label(const char *text)
{
    lv_obj_t *label = find_visible_label(lv_screen_active(), text);
    if (!label) {
        fprintf(stderr, "Missing visible label: %s\n", text);
        return false;
    }
    lv_obj_t *target = label;
    while (target && !lv_obj_has_flag(target, LV_OBJ_FLAG_CLICKABLE)) {
        target = lv_obj_get_parent(target);
    }
    if (!target) {
        fprintf(stderr, "Label has no clickable ancestor: %s\n", text);
        return false;
    }
    lv_result_t result = lv_obj_send_event(target, LV_EVENT_CLICKED, NULL);
    if (result != LV_RESULT_OK) {
        fprintf(stderr, "Click event failed for: %s\n", text);
        return false;
    }
    pump(64);
    return true;
}

static uint64_t framebuffer_hash(void)
{
    uint64_t hash = UINT64_C(14695981039346656037);
    const uint8_t *bytes = (const uint8_t *)s_framebuffer;
    for (size_t i = 0; i < sizeof(s_framebuffer); i++) {
        hash ^= bytes[i];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static bool save_ppm(const char *output_dir, const char *name)
{
    char path[1024];
    int n = snprintf(path, sizeof(path), "%s/%s.ppm", output_dir, name);
    if (n <= 0 || (size_t)n >= sizeof(path)) {
        fail("screenshot path is too long");
        return false;
    }

    lv_obj_invalidate(lv_screen_active());
    lv_refr_now(s_display);

    FILE *file = fopen(path, "wb");
    if (!file) {
        fprintf(stderr, "Cannot open screenshot: %s\n", path);
        s_failures++;
        return false;
    }
    fprintf(file, "P6\n%d %d\n255\n", DISPLAY_WIDTH, DISPLAY_HEIGHT);
    for (size_t i = 0; i < DISPLAY_WIDTH * DISPLAY_HEIGHT; i++) {
        uint32_t pixel = s_framebuffer[i];
        uint8_t rgb[3] = {
            (uint8_t)((pixel >> 16) & 0xffu),
            (uint8_t)((pixel >> 8) & 0xffu),
            (uint8_t)(pixel & 0xffu),
        };
        fwrite(rgb, 1, sizeof(rgb), file);
    }
    fclose(file);
    printf("CAPTURE %s\n", path);
    return true;
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: ui_simulator_e2e <screenshot-output-dir>\n");
        return 2;
    }

    lv_init();
    s_display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    if (!s_display) {
        fail("lv_display_create failed");
        return 1;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(s_display, s_framebuffer, NULL, sizeof(s_framebuffer),
                           LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_display, flush_cb);
    lv_display_set_default(s_display);

    if (ui_init() != ESP_OK) {
        fail("ui_init failed");
        return 1;
    }

    pump(3200);
    if (!ui_is_overview_active()) {
        fail("overview is not active after boot splash");
    }
    save_ppm(argv[1], "overview_deck1");
    uint64_t deck1_hash = framebuffer_hash();

    if (!click_label("D2")) {
        fail("could not select Deck 2");
    }
    save_ppm(argv[1], "overview_deck2");
    if (framebuffer_hash() == deck1_hash) {
        fail("Deck 2 selection produced no visible change");
    }

    if (!click_label("LIBRARY") || !ui_is_library_active()) {
        fail("library navigation failed");
    }
    save_ppm(argv[1], "library");

    if (!click_label("HOT CUES")) {
        fail("Hot Cues navigation failed");
    }
    save_ppm(argv[1], "hot_cues");

    if (!click_label("SETTINGS")) {
        fail("Settings navigation failed");
    }
    save_ppm(argv[1], "settings");
    uint64_t settings_hash = framebuffer_hash();

    splash_screen_screensaver_show();
    pump(512);
    if (!splash_screen_screensaver_active()) {
        fail("screensaver did not become active");
    }
    save_ppm(argv[1], "screensaver");

    splash_screen_screensaver_hide();
    pump(64);
    if (splash_screen_screensaver_active()) {
        fail("screensaver did not hide");
    }
    save_ppm(argv[1], "settings_restored");
    if (framebuffer_hash() != settings_hash) {
        fail("Settings screen was not restored exactly after screensaver");
    }

    if (s_failures != 0) {
        fprintf(stderr, "UI simulator E2E failed: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("UI simulator E2E scenario passed.\n");
    return 0;
}
