#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "ui_library.h"
#include "hot_cue_store.h"
#include "ui_artwork_thumb.h"
#include "artwork_fixture.h"
#include "splash_screen.h"

extern void ui_simulator_deck_set_playing(bool playing);
extern void deck_core_test_apply_event(const ctrl_event_t *event);
extern uint32_t audio_engine_stub_duration_ms[2];
extern uint32_t audio_engine_stub_session_generation[2];
extern bool audio_engine_stub_deck_loaded[2];

#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 800
#define DISPLAY_HEIGHT 480
#endif
#define TICK_STEP_MS 16u

static uint32_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static lv_display_t *s_display;
static int s_failures;
static void fail(const char *message);
static ui_artwork_thumb_work_t s_art_work;
static ui_artwork_thumb_t s_art_result;

static void check_artwork_decoder(void)
{
    if (!ui_artwork_thumb_decode(ART_FIXTURE_QUAD, sizeof ART_FIXTURE_QUAD,
                                 &s_art_work, &s_art_result)) {
        fail("baseline artwork JPEG did not decode");
        return;
    }
    const int n = UI_ARTWORK_ROW_PX;
    uint16_t red = s_art_result.row[(n / 4) * n + n / 4];
    uint16_t blue = s_art_result.row[(3 * n / 4) * n + n / 4];
    if ((red & 0xf800u) < 0xd000u || (blue & 0x001fu) < 26u)
        fail("artwork JPEG colors were not preserved");
    if (ui_artwork_thumb_decode(ART_FIXTURE_PROGRESSIVE,
                                sizeof ART_FIXTURE_PROGRESSIVE,
                                &s_art_work, &s_art_result) ||
        ui_artwork_thumb_decode(ART_FIXTURE_QUAD, 200,
                                &s_art_work, &s_art_result))
        fail("unsupported or truncated artwork JPEG was accepted");
}

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

#if CONFIG_PAJONIIIR_DJ_OVERVIEW
/* Secondary compact controls live below the viewport. Locate only within
 * unhidden pages, then exercise LVGL scrolling before sending the touch. */
static lv_obj_t *find_scroll_label(lv_obj_t *root, const char *text)
{
    if (!root || lv_obj_has_flag(root, LV_OBJ_FLAG_HIDDEN)) return NULL;
    if (lv_obj_check_type(root, &lv_label_class) &&
        !strcmp(lv_label_get_text(root), text)) return root;
    for (uint32_t i = 0; i < lv_obj_get_child_count(root); ++i) {
        lv_obj_t *found = find_scroll_label(lv_obj_get_child(root, (int32_t)i), text);
        if (found) return found;
    }
    return NULL;
}
#endif

static bool click_label(const char *text)
{
    lv_obj_t *label = find_visible_label(lv_screen_active(), text);
#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    if (!label) {
        lv_obj_t *offscreen = find_scroll_label(lv_screen_active(), text);
        if (offscreen) {
            lv_obj_scroll_to_view_recursive(offscreen, LV_ANIM_OFF);
            pump(64);
            label = find_visible_label(lv_screen_active(), text);
        }
    }
#endif
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

static bool click_deck(uint8_t deck)
{
    return click_label(deck ? "D2" : "D1");
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

    check_artwork_decoder();

    if (ui_init() != ESP_OK) {
        fail("ui_init failed");
        return 1;
    }

    pump(3200);
    char accepted_key[16];
    ui_get_deck_track_key(CTRL_DECK_1, accepted_key, sizeof(accepted_key));
    if (strcmp(accepted_key, "8A") != 0) fail("D1 accepted metadata lost musical key");
    ui_get_deck_track_key(CTRL_DECK_2, accepted_key, sizeof(accepted_key));
    if (strcmp(accepted_key, "9A") != 0) fail("D2 accepted key reused D1 metadata");
    char short_key[2] = {'?', '?'};
    ui_get_deck_track_key(CTRL_DECK_1, short_key, sizeof(short_key));
    if (short_key[0] != '8' || short_key[1] != '\0') fail("key copy exceeded caller buffer");
    ui_get_deck_track_key(CTRL_DECK_1, NULL, 0);
    if (!ui_is_overview_active()) {
        fail("overview is not active after boot splash");
    }
    save_ppm(argv[1], "overview_deck1");
    uint64_t deck1_hash = framebuffer_hash();

    if (!click_deck(CTRL_DECK_2)) {
        fail("could not select Deck 2");
    }
    pump(64);
    save_ppm(argv[1], "overview_deck2");
    if (framebuffer_hash() == deck1_hash) {
        fail("Deck 2 selection produced no visible change");
    }
#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    /* Exercise actual shared callbacks against deck_core, not callback counters. */
    lv_obj_t *badge_label = find_visible_label(lv_screen_active(), "D2");
    lv_obj_t *footer = badge_label ? lv_obj_get_parent(lv_obj_get_parent(badge_label)) : NULL;
    lv_obj_t *play = find_visible_label(footer, LV_SYMBOL_PLAY);
    lv_obj_t *cue = find_visible_label(footer, "CUE");
    if (!play || !cue) fail("Deck 2 transport controls unavailable");
    else {
        lv_obj_send_event(lv_obj_get_parent(play), LV_EVENT_CLICKED, NULL);
        pump(64);
        if (!deck_core_get_deck_state(CTRL_DECK_2).playing ||
            deck_core_get_deck_state(CTRL_DECK_1).playing)
            fail("touch PLAY changed wrong deck");
        lv_obj_send_event(lv_obj_get_parent(play), LV_EVENT_CLICKED, NULL);
        pump(64);
        lv_obj_send_event(lv_obj_get_parent(cue), LV_EVENT_PRESSED, NULL);
        if (!deck_core_get_deck_state(CTRL_DECK_2).cue_held ||
            deck_core_get_deck_state(CTRL_DECK_1).cue_held)
            fail("touch CUE press changed wrong deck");
        lv_obj_send_event(lv_obj_get_parent(cue), LV_EVENT_RELEASED, NULL);
        pump(64);
        if (deck_core_get_deck_state(CTRL_DECK_2).cue_held ||
            deck_core_get_deck_state(CTRL_DECK_2).playing)
            fail("touch CUE release retained paused preview");
    }
#endif

    if (!click_label("LIBRARY") || !ui_is_library_active()) {
        fail("library navigation failed");
    }
    save_ppm(argv[1], "library");

    if (!click_label("PLAYLISTS")) fail("playlist browser did not open");
#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    if (!find_visible_label(lv_screen_active(), "Sets"))
        fail("playlist transition retained scroll that hides its new rows");
#endif
    save_ppm(argv[1], "library_playlist_root");
    if (ui_library_load_selected_for_deck(CTRL_DECK_1) != ESP_OK)
        fail("folder row did not open");
    pump(64);
    save_ppm(argv[1], "library_playlist_folder");
    if (ui_library_load_selected_for_deck(CTRL_DECK_1) != ESP_OK)
        fail("playlist row did not open");
    pump(64);
    save_ppm(argv[1], "library_playlist_tracks");
    if (ui_library_load_selected_for_deck(CTRL_DECK_1) != ESP_OK)
        fail("first playlist track did not load");
    deck_loaded_track_summary_t playlist_track = {0};
    if (!deck_core_get_loaded_track(CTRL_DECK_1, &playlist_track) ||
        playlist_track.track_key != 1003u)
        fail("playlist order did not select export's first track");
    if (!click_label("OVERVIEW")) fail("could not inspect PWV4 overview");
    pump(64);
    save_ppm(argv[1], "overview_color_pwv4");
    if (!click_label("LIBRARY")) fail("could not return to playlist");
    if (!click_label("BACK") || !click_label("BACK") ||
        !click_label("BACK")) fail("playlist hierarchy did not return to all tracks");

    if (!click_label("HOT CUES")) {
        fail("Hot Cues navigation failed");
    }
    save_ppm(argv[1], "hot_cues");

    if (!click_label("D2 JOG: VINYL")) fail("Deck 2 jog selector missing");
    if (!deck_core_get_deck_state(CTRL_DECK_2).jog_cdj_mode ||
        deck_core_get_deck_state(CTRL_DECK_1).jog_cdj_mode)
        fail("touch jog selector changed the wrong deck");
    if (!click_deck(CTRL_DECK_1)) fail("could not inspect Deck 1 jog mode");
    if (!find_visible_label(lv_screen_active(), "D1 JOG: VINYL"))
        fail("Deck 1 inherited Deck 2 jog mode label");
    if (!click_deck(CTRL_DECK_2)) fail("could not return to Deck 2 jog mode");
    if (!find_visible_label(lv_screen_active(), "D2 JOG: CDJ"))
        fail("Deck 2 mode did not survive navigation");
    ctrl_event_t vinyl = {
        .type = CTRL_EV_BUTTON, .id = CTRL_ID_DECK2_EXT_ACTION,
        .deck = CTRL_DECK_2,
        .value = CTRL_DECK_EXT_VALUE(CTRL_DECK_EXT_ACTION_JOG_VINYL, true),
    };
    deck_core_test_apply_event(&vinyl);
    pump(64);
    if (!find_visible_label(lv_screen_active(), "D2 JOG: VINYL"))
        fail("controller semantic jog mode did not refresh touch label");

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

    /* The same Library action used by touch and deferred controller LOAD must
     * refuse the playing destination before it replaces its track snapshot. */
    deck_loaded_track_summary_t before = {0}, after = {0};
    const bool had_track = deck_core_get_loaded_track(CTRL_DECK_1, &before);
    char locked_key[16];
    ui_get_deck_track_key(CTRL_DECK_1, locked_key, sizeof(locked_key));
    ui_simulator_deck_set_playing(true);
    if (ui_library_load_selected_for_deck(CTRL_DECK_1) != ESP_ERR_INVALID_STATE) {
        fail("load lock accepted a playing destination deck");
    }
    if (deck_core_get_loaded_track(CTRL_DECK_1, &after) != had_track ||
        (had_track && after.track_key != before.track_key)) {
        fail("rejected load changed the current track");
    }
    ui_simulator_deck_set_playing(false);

    ui_get_deck_track_key(CTRL_DECK_1, accepted_key, sizeof(accepted_key));
    if (strcmp(accepted_key, locked_key) != 0)
        fail("rejected load replaced accepted musical key");

    if (!click_label("OVERVIEW") || !click_deck(CTRL_DECK_1))
        fail("could not inspect live duration");
    uint32_t analysis_ms = ui_library_deck_analysis_span_ms(CTRL_DECK_1, 0);
    uint64_t metadata_hash = framebuffer_hash();
    audio_engine_stub_duration_ms[CTRL_DECK_1] = analysis_ms + 30000u;
    pump(64);
    if (ui_library_deck_duration_ms(CTRL_DECK_1, 0) != analysis_ms + 30000u ||
        ui_library_deck_analysis_span_ms(CTRL_DECK_1, 0) != analysis_ms)
        fail("live duration changed waveform time base or was ignored");
    if (framebuffer_hash() == metadata_hash)
        fail("live duration did not update the visible overview");
    ++audio_engine_stub_session_generation[CTRL_DECK_1];
    if (ui_library_deck_duration_ms(CTRL_DECK_1, 0) != analysis_ms)
        fail("stale audio session leaked its duration into current track");
    --audio_engine_stub_session_generation[CTRL_DECK_1];
    audio_engine_stub_duration_ms[CTRL_DECK_1] = 100u;
    if (ui_library_deck_duration_ms(CTRL_DECK_1, 0) != 100u)
        fail("metadata duration overrode shorter decoded duration");
    audio_engine_stub_deck_loaded[CTRL_DECK_1] = false;
    if (ui_library_deck_duration_ms(CTRL_DECK_1, 0) != analysis_ms)
        fail("unloaded audio retained its duration");
    audio_engine_stub_deck_loaded[CTRL_DECK_1] = true;
    audio_engine_stub_duration_ms[CTRL_DECK_1] = 0u;

#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    /* Exercise the real row callback and touch LOAD, not a direct test API. */
    if (!click_label("LIBRARY") || !click_label("Static Bloom") ||
        !click_label("LOAD DECK 2")) fail("dj_ui row selection/touch LOAD failed");
    deck_loaded_track_summary_t d2_touch = {0}, d1_touch = {0};
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &d2_touch) ||
        d2_touch.track_key != 1003u) fail("touch LOAD ignored selected row/deck");
    if (!deck_core_get_loaded_track(CTRL_DECK_1, &d1_touch) ||
        d1_touch.track_key != before.track_key) fail("D2 touch LOAD changed D1");
    if (!click_label("SORT NAME") || !click_label("LOAD DECK 2"))
        fail("dj_ui sort/touch LOAD failed");
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &d2_touch) ||
        d2_touch.track_key != 1003u) fail("sort lost the selected track identity");
    if (!click_label("PLAYLISTS") || !click_label("LOAD DECK 1"))
        fail("touch LOAD did not open the selected folder");
    if (!click_label("BACK") || !click_label("BACK"))
        fail("touch playlist navigation did not restore all tracks");
    if (!click_label("HOT CUES") || !click_deck(CTRL_DECK_2) ||
        !click_label("DELETE: OFF")) fail("explicit cue delete mode missing");
    lv_obj_t *cue_a = find_visible_label(lv_screen_active(), "CUE A");
    uint32_t delete_position = deck_core_get_deck_state(CTRL_DECK_2).position_ms;
    if (!cue_a) fail("cue A delete target missing");
    else {
        lv_obj_t *card = lv_obj_get_parent(cue_a);
        lv_obj_send_event(card, LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(card, LV_EVENT_RELEASED, NULL);
        pump(64);
    }
    hot_cue_store_blob_t deleted = {0};
    if (hot_cue_store_load(&d2_touch.persistent_id, &deleted) != ESP_OK ||
        !(deleted.override_mask & 1u) || (deleted.valid_mask & 1u))
        fail("touch delete did not persist a source cue tombstone");
    if (deck_core_get_deck_state(CTRL_DECK_2).position_ms != delete_position)
        fail("delete mode triggered audible cue/seek before deleting");
    if (!click_deck(CTRL_DECK_1) ||
        !find_visible_label(lv_screen_active(), "DELETE: OFF"))
        fail("delete mode survived target change");
    if (!click_deck(CTRL_DECK_2)) fail("restore target selection failed");
    lv_obj_t *restore_label = find_visible_label(lv_screen_active(), "HOLD RESTORE");
    lv_obj_t *restore = restore_label ? lv_obj_get_parent(restore_label) : NULL;
    if (!restore) fail("restore control missing");
    else {
        lv_obj_send_event(restore, LV_EVENT_CLICKED, NULL);
        hot_cue_store_load(&d2_touch.persistent_id, &deleted);
        if (!(deleted.override_mask & 1u)) fail("ordinary click restored source cues");
        lv_obj_send_event(restore, LV_EVENT_PRESSED, NULL);
        click_label("SETTINGS");
        lv_obj_send_event(restore, LV_EVENT_LONG_PRESSED, NULL);
        hot_cue_store_load(&d2_touch.persistent_id, &deleted);
        if (!(deleted.override_mask & 1u)) fail("stale hold restored cues after tab change");
        click_label("HOT CUES");
        lv_obj_send_event(restore, LV_EVENT_PRESSED, NULL);
        lv_obj_send_event(restore, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_send_event(restore, LV_EVENT_RELEASED, NULL);
        pump(64);
        esp_err_t restored = hot_cue_store_load(&d2_touch.persistent_id, &deleted);
        if (restored != ESP_ERR_NOT_FOUND &&
            (restored != ESP_OK || deleted.override_mask))
            fail("explicit hold did not reset local cue overrides");
    }
#endif

    if (s_failures != 0) {
        fprintf(stderr, "UI simulator E2E failed: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("UI simulator E2E scenario passed.\n");
    return 0;
}
