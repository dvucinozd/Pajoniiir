#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lvgl.h"
#include "dj_ui.h"
#include "ui_dj_bridge.h"
#include "ui_artwork_thumb.h"
#include "artwork_fixture.h"


#ifndef DISPLAY_WIDTH
#define DISPLAY_WIDTH 800
#endif
#ifndef DISPLAY_HEIGHT
#define DISPLAY_HEIGHT 480
#endif
#define TICK_STEP_MS 16u

static uint32_t s_framebuffer[DISPLAY_WIDTH * DISPLAY_HEIGHT];
static lv_display_t *s_display;
static int s_failures;
static unsigned s_flushes;
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
    ++s_flushes;
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


static unsigned plays, cues_pressed, cues_released, pads_pressed, pads_released, wakes;
static bool loaded = true;
static unsigned track_generation = 1, restores;
static void on_restore(uint8_t deck) { (void)deck; ++restores; }
static void on_play(uint8_t deck) { plays |= 1u << deck; }
static void on_cue(uint8_t deck, bool pressed) { (void)deck; if (pressed) ++cues_pressed; else ++cues_released; }
static void on_pad(uint8_t deck, uint8_t pad, bool pressed)
{
    if (deck != 0 || pad != 0) fail("hot cue addressed wrong slot/deck");
    if (pressed) ++pads_pressed; else ++pads_released;
}
static void on_wake(void) { ++wakes; }
/* The presentation test supplies snapshots, not an emulated decoder or device. */
bool deck_core_get_loaded_track(uint8_t deck, deck_loaded_track_summary_t *out)
{
    *out = (deck_loaded_track_summary_t){.valid=loaded,.generation=track_generation,
                                       .track_key=deck ? 1003u : 1001u};
    return true;
}
bool deck_core_get_load_lock(void) { return true; }
uint32_t deck_core_hot_cue_revision(void) { return 0; }
bool deck_core_load_allowed(uint8_t deck) { return deck != 1; }
deck_core_loop_display_t deck_core_get_loop_display(uint8_t deck)
{
    return (deck_core_loop_display_t){.active=deck==0,.start_ms=44000,.end_ms=48000};
}

static void check_transport_events(void)
{
    lv_obj_t *label = find_visible_label(lv_screen_active(), "CUE");
    if (!label) { fail("CUE button missing"); return; }
    lv_obj_t *button = lv_obj_get_parent(label);
    lv_obj_send_event(button, LV_EVENT_PRESSED, NULL);
    lv_obj_send_event(button, LV_EVENT_RELEASED, NULL);
    lv_obj_send_event(button, LV_EVENT_PRESSED, NULL);
    lv_obj_send_event(button, LV_EVENT_PRESS_LOST, NULL);
    if (cues_pressed != 2 || cues_released != 2)
        fail("CUE hold/release/lost did not preserve transport semantics");
    if (!click_label(LV_SYMBOL_PLAY) || plays != 1u) fail("PLAY did not target first deck");
    label = find_visible_label(lv_screen_active(), "A");
    if (!label) { fail("hot cue pad missing"); return; }
    button = lv_obj_get_parent(label);
    lv_obj_send_event(button, LV_EVENT_PRESSED, NULL);
    lv_obj_send_event(button, LV_EVENT_RELEASED, NULL);
    lv_obj_send_event(button, LV_EVENT_PRESSED, NULL);
    lv_obj_send_event(button, LV_EVENT_PRESS_LOST, NULL);
    if (pads_pressed != 2 || pads_released != 2)
        fail("hot cue hold/release/lost semantics changed");
}
int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    lv_init();
    s_display = lv_display_create(DISPLAY_WIDTH, DISPLAY_HEIGHT);
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_XRGB8888);
    lv_display_set_buffers(s_display, s_framebuffer, NULL, sizeof s_framebuffer, LV_DISPLAY_RENDER_MODE_DIRECT);
    lv_display_set_flush_cb(s_display, flush_cb);
    dj_ui_callbacks_t cb = {.on_play=on_play,.on_cue=on_cue,
        .on_hotcue=on_pad,.on_restore_cues=on_restore,.on_wake=on_wake};
    ui_dj_bridge_init(lv_screen_active(), &cb, DISPLAY_WIDTH == 1024, false);
    ui_frame_context_t frame = {0};
    ui_deck_track_info_t info[2] = {
        {.valid=true,.title="First deck",.artist="Rekordbox export"},
        {.valid=true,.title="Second deck",.artist="Rekordbox export"}
    };
    anlz_metadata_t meta = {.has_waveform_low=true,.bpm=128,
        .cue_count=1,.cues={{.index=0,.type=ANLZ_CUE_SINGLE,.start_ms=30000}},
        .memory_cue_count=2,.memory_cues={{.start_ms=10000},{.start_ms=20000}}};
    for(unsigned i=0;i<400;++i) meta.waveform_low[i]=(uint8_t)(i%32);
    for(unsigned d=0;d<2;++d) {
        frame.deck_info[d]=&info[d]; frame.deck_bpm[d]=128+d;
        frame.deck_duration_ms[d]=frame.deck_analysis_span_ms[d]=240000;
        frame.deck_state[d].position_ms=45000;
        frame.deck_anlz[d]=anlz_snapshot_create(&meta,ANLZ_SNAPSHOT_FULL);
        frame.deck_meta[d]=anlz_snapshot_metadata(frame.deck_anlz[d]);
        dj_ui_set_hotcue(d,0,true,30000,0);
    }
    ui_dj_bridge_update(&frame);
    pump(64);
    unsigned flushes = s_flushes;
    ui_dj_bridge_stats_t before[2], after[2];
    for (uint8_t d=0;d<2;++d) ui_dj_bridge_get_stats(d,&before[d]);
    ui_dj_bridge_update(&frame);
    pump(64);
    if (s_flushes != flushes) fail("unchanged frame invalidated the LVGL presentation");
    for (uint8_t d=0;d<2;++d) {
        ui_dj_bridge_get_stats(d,&after[d]);
        if (!before[d].surface_bytes || !before[d].cache_full_updates ||
            after[d].columns_rendered != before[d].columns_rendered)
            fail("unchanged frame failed to reuse waveform cache");
        printf("DJ_BRIDGE deck=%u surface_bytes=%u columns=%u full=%u\n",
               d,(unsigned)after[d].surface_bytes,(unsigned)after[d].columns_rendered,
               (unsigned)after[d].cache_full_updates);
    }
    check_transport_events();
    /* Release the original snapshot before LVGL draws; the image descriptors
     * must contain owned pixels and copied cue positions only. */
    for(unsigned d=0;d<2;++d) {
        anlz_snapshot_release(frame.deck_anlz[d]);
        frame.deck_anlz[d]=NULL; frame.deck_meta[d]=NULL;
    }
    pump(64); save_ppm(argv[1],"dj_overview");
    if(!click_label("LIBRARY")) fail("library navigation");
    dj_track_t rows[8]={0};
    for(unsigned i=0;i<8;++i) { rows[i].title="Track title"; rows[i].artist="Artist";rows[i].bpm=128;rows[i].len_ms=240000; }
    dj_ui_library_set_rows(rows,8); dj_ui_library_set_selected(2);
    pump(64); save_ppm(argv[1],"dj_library");
    if(!click_label("HOT CUES")) fail("hotcue navigation");
    save_ppm(argv[1],"dj_hotcues");
    lv_obj_t *restore = find_visible_label(lv_screen_active(), "HOLD RESTORE");
    if (!restore) fail("native restore control missing");
    else {
        restore = lv_obj_get_parent(restore);
        lv_obj_send_event(restore, LV_EVENT_PRESSED, NULL);
        ++track_generation;
        ui_dj_bridge_update(&frame);
        lv_obj_send_event(restore, LV_EVENT_LONG_PRESSED, NULL);
        lv_obj_send_event(restore, LV_EVENT_RELEASED, NULL);
        if (restores) fail("old track hold restored newly loaded track cues");
    }
    lv_obj_t *hc = find_visible_label(lv_screen_active(), "CUE A");
    if (!hc) fail("Hot Cues surface missing");
    else {
        lv_obj_send_event(lv_obj_get_parent(hc), LV_EVENT_PRESSED, NULL);
        dj_ui_set_target(1);
        lv_obj_send_event(lv_obj_get_parent(hc), LV_EVENT_RELEASED, NULL);
        if (pads_pressed != 3 || pads_released != 3)
            fail("target change during hold lost the original release");
        dj_ui_set_target(0);
        lv_obj_send_event(lv_obj_get_parent(hc), LV_EVENT_PRESSED, NULL);
    }
    if(!click_label("SETTINGS")) fail("settings navigation");
    if (pads_pressed != 4 || pads_released != 4)
        fail("tab change retained a held hot cue");
    if (find_visible_label(lv_screen_active(), "RECORD")) fail("regular build exposes recorder");
    if (DISPLAY_WIDTH == 800 && find_visible_label(lv_screen_active(), "LINK OFF"))
        fail("board without Ethernet exposes Link master");
    save_ppm(argv[1],"dj_settings");
    uint64_t settings_hash = framebuffer_hash();
    dj_ui_set_screensaver(true); pump(64); save_ppm(argv[1],"dj_screensaver");
    lv_obj_t *wake_label = find_visible_label(lv_layer_top(), "touch me.....or don't ;)");
    if (!wake_label) fail("screensaver touch target missing");
    else lv_obj_send_event(lv_obj_get_parent(wake_label), LV_EVENT_CLICKED, NULL);
    pump(64); save_ppm(argv[1],"dj_settings_restored");
    if (wakes != 1 || dj_ui_screensaver_active() || framebuffer_hash() != settings_hash)
        fail("touch wake did not restore previous Settings exactly");
    dj_ui_show_tab((dj_tab_t)255); pump(64);
    if (framebuffer_hash() != settings_hash) fail("invalid tab changed screen");
    dj_ui_show_tab(DJ_TAB_OVERVIEW);
    loaded = false;
    for (unsigned d=0;d<2;++d) {
        frame.deck_duration_ms[d]=frame.deck_analysis_span_ms[d]=frame.deck_bpm[d]=0;
        info[d].valid=false;
    }
    ui_dj_bridge_update(&frame); pump(64);
    if (!find_visible_label(lv_screen_active(), "No Track")) fail("unload retained old track title");
    for (unsigned d = 0; d < 2; ++d) {
        lv_area_t area;
        if (!dj_ui_wave_get_area(d, DJ_WAVE_ZOOM, &area)) {
            fail("empty deck zoom surface missing");
            continue;
        }
        for (int y = area.y1 + 24; y < area.y2 - 24; ++y)
            for (int x = area.x1 + 24; x < area.x2 - 24; ++x) {
                uint32_t p = s_framebuffer[y * DISPLAY_WIDTH + x];
                unsigned r = (p >> 16) & 255, g = (p >> 8) & 255, b = p & 255;
                unsigned hi = r > g ? r : g; if (b > hi) hi = b;
                unsigned lo = r < g ? r : g; if (b < lo) lo = b;
                /* The empty surface uses a slightly tinted dark background;
                 * waveform/loop/cue colors have a much larger channel spread. */
                if (hi - lo > 16) {
                    fail("unload retained colored waveform/cue pixels");
                    y = area.y2; break;
                }
            }
    }
    save_ppm(argv[1],"dj_empty");
    dj_ui_library_set_rows(NULL,0); dj_ui_show_tab(DJ_TAB_LIBRARY);
    dj_ui_library_set_status("MEDIA REMOVED",DJ_TONE_ERROR);
    pump(64); save_ppm(argv[1],"dj_error");
    dj_ui_library_set_status("D1 LOADING",DJ_TONE_INFO);
    dj_ui_library_set_progress(42); dj_ui_library_set_load_enabled(false);
    pump(64); save_ppm(argv[1],"dj_loading");
    if(!framebuffer_hash()) fail("empty framebuffer");
    check_artwork_decoder();
    ui_dj_bridge_reset();
    return s_failures ? 1 : 0;
}
