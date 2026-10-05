#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "ui.h"
#include "ui_library.h"
#include "library.h"
#include "hot_cue_store.h"
#include "ui_artwork_thumb.h"
#include "artwork_fixture.h"
#include "splash_screen.h"
#include "ui_status.h"
#include "ui_overview.h"

extern void ui_simulator_deck_set_playing(bool playing);
extern void deck_core_test_apply_event(const ctrl_event_t *event);
extern uint32_t audio_engine_stub_duration_ms[2];
extern uint32_t audio_engine_stub_session_generation[2];
extern bool audio_engine_stub_deck_loaded[2];
extern bool ui_simulator_audio_status_override[2];
extern audio_engine_deck_status_t ui_simulator_audio_status[2];

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

static unsigned count_objects(lv_obj_t *object, const lv_obj_class_t *type)
{
    unsigned count = lv_obj_check_type(object, type) ? 1u : 0u;
    for (uint32_t i = 0; i < lv_obj_get_child_count(object); ++i)
        count += count_objects(lv_obj_get_child(object, (int32_t)i), type);
    return count;
}

#ifdef UI_LINK_SIMULATOR
extern void ui_link_mock_available(bool available);
extern void ui_link_mock_empty(void);
extern void ui_link_mock_error(void);
extern void ui_link_mock_pending(bool pending);
extern void ui_link_mock_load(uint8_t deck);
extern uint32_t ui_link_mock_visible_first(void);
extern void ui_link_mock_audio(bool ready,bool cancel);
extern uint32_t ui_link_mock_download_calls(void);
static lv_obj_t *link_table(lv_obj_t *root)
{
    if (lv_obj_check_type(root,&lv_table_class) && lv_obj_is_visible(root) &&
        lv_table_get_row_count(root)>1) return root;
    for (uint32_t i=0;i<lv_obj_get_child_count(root);++i) {
        lv_obj_t *t=link_table(lv_obj_get_child(root,(int32_t)i)); if (t) return t;
    }
    return NULL;
}
static void link_row(const char *expected)
{
    lv_obj_t *table=link_table(lv_screen_active());
    if (!table || strcmp(lv_table_get_cell_value(table,0,0),expected)) fail("Link row/page did not match owned snapshot");
}
static int link_scenario(const char *output)
{
    click_label("LIBRARY"); pump(256);
    ui_library_select_delta(2);
    deck_loaded_track_summary_t before={0},after={0};
    deck_core_get_loaded_track(CTRL_DECK_1,&before);
    if (!click_label("LOCAL USB")) fail("Link source selector missing");
    save_ppm(output,"link_loading"); pump(256);
    link_row("Remote track 1"); save_ppm(output,"link_tracks");
    ui_library_page_delta(1); pump(256);
    if (ui_link_mock_visible_first()!=8) fail("visible page did not drive metadata window");
    link_row("Remote track 9");
    if (ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_ERR_NOT_SUPPORTED)
        fail("metadata-only Link track was accepted as loaded audio");
    deck_core_get_loaded_track(CTRL_DECK_1,&after);
    if (after.track_key!=before.track_key) fail("remote metadata load changed existing deck");
    ui_link_mock_load(CTRL_DECK_1); pump(256);
    if (!find_visible_label(lv_screen_active(),"AUDIO DOWNLOAD UNAVAILABLE")) fail("incoming load bypassed common audio admission");
    ctrl_event_t play={.type=CTRL_EV_BUTTON,.deck=CTRL_DECK_1,.id=BTN_PLAY,.value=1};
    deck_core_test_apply_event(&play);
    ui_link_mock_load(CTRL_DECK_1); pump(256);
    if (!find_visible_label(lv_screen_active(),"LOAD LOCK")) fail("incoming load bypassed playing-deck load lock");
    deck_core_test_apply_event(&play); ui_simulator_deck_set_playing(false);
    ui_link_mock_audio(true,false);
    if(ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_OK)fail("verified remote artifact did not use common controller load");
    deck_core_get_loaded_track(CTRL_DECK_1,&after);
    if(after.track_key!=0x80000009u)fail("remote completion published the wrong selected track");
    save_ppm(output,"link_loaded");
    uint32_t calls=ui_link_mock_download_calls();deck_core_test_apply_event(&play);
    if(ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_ERR_INVALID_STATE || ui_link_mock_download_calls()!=calls)
        fail("playing-deck lock performed download before admission");
    deck_core_test_apply_event(&play);ui_simulator_deck_set_playing(false);
    ui_link_mock_audio(true,true);
    if(ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_ERR_INVALID_STATE)fail("cancelled transfer reported success");
    deck_core_get_loaded_track(CTRL_DECK_1,&after);
    if(after.track_key!=0x80000009u)fail("cancelled transfer changed loaded deck");
    ui_link_mock_audio(true,false);ui_link_mock_load(CTRL_DECK_1);pump(256);
    deck_core_get_loaded_track(CTRL_DECK_1,&after);
    if(after.track_key!=0x80000011u)fail("incoming load used selected browse row instead of requested track");
    ui_link_mock_audio(false,false);
    ui_library_load_track_index_for_deck(0,CTRL_DECK_1);
    ui_link_mock_load(CTRL_DECK_1);pump(256);
    ui_link_mock_pending(true);
    click_label("PLAYLISTS"); pump(256);
    if (link_table(lv_screen_active())) fail("pending new menu displayed old completed rows");
    if (ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_ERR_NOT_FOUND)
        fail("pending new menu admitted a stale row");
    ui_link_mock_pending(false); pump(256); link_row("Subfolder");
    save_ppm(output,"link_folders");
    ui_library_select_delta(1);
    if (ui_library_load_selected_for_deck(CTRL_DECK_1)!=ESP_OK) fail("controller LOAD did not open remote playlist");
    pump(256); link_row("Remote track 1"); save_ppm(output,"link_playlist");
    click_label("SORT NAME"); pump(64); link_row("Remote track 1");
    click_label("BACK"); pump(256); link_row("Subfolder");
    uint32_t parent_row=0,parent_col=0;
    lv_obj_t *parent_table=link_table(lv_screen_active());
    if (parent_table) lv_table_get_selected_cell(parent_table,&parent_row,&parent_col);
    if (!parent_table || parent_row!=1) fail("remote playlist BACK lost parent selection");
    click_label("BACK"); pump(256); link_row("Remote track 1");
    ui_refresh_library(); pump(64); link_row("Remote track 1");
    ui_link_mock_empty(); pump(256);
    if (link_table(lv_screen_active())) fail("empty remote list kept old rows");
    save_ppm(output,"link_empty");
    ui_link_mock_error(); pump(256);
    if (link_table(lv_screen_active())) fail("failed remote list kept completed rows");
    save_ppm(output,"link_error");
    ui_link_mock_available(false); pump(256);
    if (link_table(lv_screen_active())) fail("lost source retained remote rows");
    save_ppm(output,"link_unavailable");
    click_label("LINK #1 RB"); pump(256);
    lv_obj_t *table=link_table(lv_screen_active());
    uint32_t selected=0,column=0;
    if (table) lv_table_get_selected_cell(table,&selected,&column);
    if (!table || selected!=2) fail("local source selection was not restored");
    save_ppm(output,"link_local_restored");
    if (s_failures) return 1;
    puts("PASS real Library Link source, page, playlist order, admission, loss and local restoration"); return 0;
}
#endif

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
#ifdef UI_LINK_SIMULATOR
    return link_scenario(argv[1]);
#endif
#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    if (count_objects(lv_screen_active(), &lv_table_class) != 0)
        fail("preview instantiated a hidden legacy Library table");
    if (count_objects(lv_screen_active(), &lv_canvas_class) != 0)
        fail("preview instantiated hidden legacy waveform canvases");
    if (count_objects(lv_screen_active(), &lv_slider_class) != 2)
        fail("preview allocated duplicate Settings controls");
#else
    if (count_objects(lv_screen_active(), &lv_table_class) != 2)
        fail("legacy Library did not retain header and data tables");
    if (count_objects(lv_screen_active(), &lv_canvas_class) != 4)
        fail("product did not retain the shared waveform surfaces");
#endif
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
    /* Exercise actual shared callbacks against deck_core, not callback counters. */
    lv_obj_t *badge_label = find_visible_label(lv_screen_active(), "D2");
#if CONFIG_PAJONIIIR_DJ_OVERVIEW
    lv_obj_t *footer = badge_label ? lv_obj_get_parent(lv_obj_get_parent(badge_label)) : NULL;
#else
    lv_obj_t *footer = badge_label ? lv_obj_get_parent(badge_label) : NULL;
#endif
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
        lv_obj_send_event(lv_obj_get_parent(cue), LV_EVENT_PRESSED, NULL);
        click_label("LIBRARY");
        pump(64);
        if (deck_core_get_deck_state(CTRL_DECK_2).cue_held)
            fail("navigation retained a touch CUE hold");
    }

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
#if !CONFIG_PAJONIIIR_DJ_OVERVIEW
    if (!click_label("MEMORY")) fail("memory cue list missing");
    if (!find_visible_label(lv_screen_active(), "D2 MEMORY CUES: 16 (TRUNCATED)"))
        fail("memory cue count/truncation missing");
    save_ppm(argv[1], "memory_cues");
    lv_obj_t *memory_title = find_visible_label(lv_screen_active(), "D2 MEMORY CUES: 16 (TRUNCATED)");
    if (memory_title) {
        lv_obj_scroll_to_y(lv_obj_get_parent(memory_title), 400, LV_ANIM_OFF);
        pump(64);
        save_ppm(argv[1], "memory_cues_scrolled");
    }
    if (!click_label("MEMORY")) fail("memory list close failed");
#endif

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
    if (ui_show_library() != ESP_OK)
        fail("controller Library navigation required a legacy screen");
    pump(64);
    if (!ui_is_library_active()) fail("controller Library navigation was not published");
    ui_library_select_visible_row(0);
    if (ui_library_select_delta(1) != ESP_OK ||
        ui_library_load_selected_for_deck(CTRL_DECK_2) != ESP_OK)
        fail("controller browse/load required a legacy table");
    deck_loaded_track_summary_t controller_load = {0};
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &controller_load) ||
        controller_load.track_key != 1002u)
        fail("controller browse/load lost selected row identity");
    if (ui_toggle_library_view() != ESP_OK)
        fail("controller Library toggle required a legacy screen");
    pump(64);
    if (!ui_is_overview_active()) fail("controller Library toggle was not published");
    if (!click_label("LIBRARY") || !click_label("Static Bloom") ||
        !click_label("LOAD DECK 2")) fail("dj_ui row selection/touch LOAD failed");
    deck_loaded_track_summary_t d2_touch = {0}, d1_touch = {0};
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &d2_touch) ||
        d2_touch.track_key != 1003u) fail("touch LOAD ignored selected row/deck");
    if (!deck_core_get_loaded_track(CTRL_DECK_1, &d1_touch) ||
        d1_touch.track_key != before.track_key) fail("D2 touch LOAD changed D1");
    if (!click_label("SORT NAME")) fail("dj_ui sort failed");
    lv_obj_t *sorted_static = find_visible_label(lv_screen_active(), "Static Bloom");
    lv_obj_t *sorted_midnight = find_visible_label(lv_screen_active(), "Midnight Circuit");
    lv_area_t static_area = {0}, midnight_area = {0};
    if (!sorted_static || !sorted_midnight) fail("sorted Library rows missing");
    else {
        lv_obj_get_coords(sorted_static, &static_area);
        lv_obj_get_coords(sorted_midnight, &midnight_area);
        if (static_area.y1 >= midnight_area.y1)
            fail("name sort depended on the absent legacy table");
    }
    if (!click_label("LOAD DECK 2")) fail("dj_ui sorted touch LOAD failed");
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &d2_touch) ||
        d2_touch.track_key != 1003u) fail("sort lost the selected track identity");
    if (!click_label("PLAYLISTS") || !click_label("LOAD DECK 1"))
        fail("touch LOAD did not open the selected folder");
    if (!click_label("BACK") || !click_label("BACK"))
        fail("touch playlist navigation did not restore all tracks");
#else
    deck_loaded_track_summary_t d2_touch = {0};
    if (!deck_core_get_loaded_track(CTRL_DECK_2, &d2_touch)) fail("Deck 2 identity missing");
#endif
    click_label("OVERVIEW");
    click_deck(CTRL_DECK_2);
    uint64_t source_cue_pixels = framebuffer_hash();
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
    click_label("OVERVIEW");
    pump(64);
    if (framebuffer_hash() == source_cue_pixels)
        fail("Overview kept the deleted source cue");
    click_label("HOT CUES");
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

    /* Actual product status rendering, not only the donor's standalone demo. */
    click_label("OVERVIEW");
#if !CONFIG_PAJONIIIR_DJ_OVERVIEW
    ui_simulator_audio_status_override[0] = true;
    ui_simulator_audio_status[0] = (audio_engine_deck_status_t){
        .state=AE_LOADING, .load_progress=37};
    pump(512);
    if (!find_visible_label(lv_screen_active(), "LOADING 37%"))
        fail("product loading status did not use real decoder progress");
    save_ppm(argv[1], "overview_loading");
    ui_simulator_audio_status[0].state = AE_ERROR;
    snprintf(ui_simulator_audio_status[0].last_error_text,
        sizeof ui_simulator_audio_status[0].last_error_text, "media removed");
    pump(512);
    if (!find_visible_label(lv_screen_active(), "ERROR: media removed"))
        fail("product error status did not display the decoder failure");
    save_ppm(argv[1], "overview_error");
    ui_simulator_audio_status_override[0] = false;
#endif
    for (uint8_t d = 0; d < 2; ++d) {
        deck_core_clear_loaded_track(d, library_generation());
        audio_engine_stub_deck_loaded[d] = false;
    }
    /* Reject publication of an unloaded audio session through the real owner;
     * clearing only deck_core would leave its separate UI metadata intact. */
    ui_library_load_initial_track();
    pump(512);
#if !CONFIG_PAJONIIIR_DJ_OVERVIEW
    if (!find_visible_label(lv_screen_active(), "NO TRACK") ||
        !find_visible_label(lv_screen_active(), "EMPTY"))
        fail("unload retained product track/status state");
    for (int y = 55; y < 130; ++y) for (int x = 90; x < 600; ++x) {
        uint32_t pixel = s_framebuffer[y * DISPLAY_WIDTH + x] & 0xffffffu;
        int r = (pixel >> 16) & 255, g = (pixel >> 8) & 255, b = pixel & 255;
        if (abs(r - g) > 8 || abs(g - b) > 8) {
            fail("unload retained colored main waveform pixels");
            y = 130;
            break;
        }
    }
#endif
    save_ppm(argv[1], "overview_empty");
    library_clear();
    ui_refresh_library();
    click_label("LIBRARY");
    pump(512);
    if (find_visible_label(lv_screen_active(), "Midnight Circuit") ||
        find_visible_label(lv_screen_active(), "Neon Harbor"))
        fail("unavailable source retained Library track rows");
    if (!find_visible_label(lv_screen_active(), "EMPTY"))
        fail("empty Library deck claimed ready");
    save_ppm(argv[1], "library_unavailable");
#if !CONFIG_PAJONIIIR_DJ_OVERVIEW
    click_label("OVERVIEW");
    ui_frame_context_t network_ctx={0};
    network_ctx.active_deck=0;network_ctx.active_state.playing=true;
    ui_status_hold("",lv_color_hex(0),0);
    const char *network_text[]={"WAIT #3","ALIGNING #3","LOCKED #3"};
    const char *network_capture[]={"network_wait","network_aligning","network_locked"};
    for (unsigned i=0;i<3;++i) {
        network_ctx.active_state.network_sync=(deck_net_sync_status_t)(i+1);
        network_ctx.active_state.network_player=3;
        network_ctx.deck_state[0]=network_ctx.active_state;
        ui_overview_update(&network_ctx);
        if (!find_visible_label(lv_screen_active(),network_text[i]))
            fail("actual product header did not expose network sync status");
        save_ppm(argv[1],network_capture[i]);
    }
#endif

    if (s_failures != 0) {
        fprintf(stderr, "UI simulator E2E failed: %d failure(s)\n", s_failures);
        return 1;
    }
    printf("UI simulator E2E scenario passed.\n");
    return 0;
}
