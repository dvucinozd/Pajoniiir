/* DJ deck display — LVGL v9, 1024x600, 4 onglets (Overview, Library, Hot Cues, Settings) */
#ifndef DJ_UI_H
#define DJ_UI_H

#include "lvgl.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DJ_DECKS     2
#define DJ_HOTCUES   8
#define DJ_LIB_ROWS  8
#define DJ_LINK_ROWS 4
#define DJ_VU_SEGS   15
#define DJ_ART_ROW_PX  40             /* library row thumbnail, RGB565 square */
#define DJ_ART_DECK_PX 34             /* deck header thumbnail, RGB565 square */

typedef enum { DJ_TAB_OVERVIEW, DJ_TAB_LIBRARY, DJ_TAB_HOTCUES, DJ_TAB_SETTINGS } dj_tab_t;
typedef enum { DJ_SORT_NONE, DJ_SORT_ARTIST, DJ_SORT_NAME, DJ_SORT_BPM, DJ_SORT_KEY } dj_sort_t;
typedef enum { DJ_TONE_NORMAL, DJ_TONE_MUTED, DJ_TONE_OK, DJ_TONE_WARN, DJ_TONE_INFO, DJ_TONE_ERROR } dj_tone_t;
/* Deck SYNC button (v304): local BEAT SYNC, or following the DJ Link master. */
typedef enum { DJ_SYNC_OFF, DJ_SYNC_LOCAL, DJ_SYNC_LINK_WAIT, DJ_SYNC_LINK_LOCKED } dj_sync_t;

/* Status/text fields on Settings and Hot Cues screens (dj_ui_set_field).
 * DJ_F_MASTER, DJ_F_OUT_MAIN, DJ_F_UI_RENDER, DJ_F_LOCAL, DJ_F_MIX_CUE, DJ_F_MIX_JOG,
 * DJ_F_MIX_TEMPO, DJ_F_LOAD_LOCK and DJ_F_LINK_SYNC are tappable (on_field). */
typedef enum {
    DJ_F_MASTER, DJ_F_MASTER_HINT,
    DJ_F_OUT_MAIN, DJ_F_OUT_CUE, DJ_F_UI_RENDER, DJ_F_LOCAL,
    DJ_F_SYS_CONTROLLER, DJ_F_SYS_SD, DJ_F_SYS_SD_LOG, DJ_F_SYS_FW, DJ_F_SYS_RESET,
    DJ_F_MIX_MIXER, DJ_F_MIX_FADERS, DJ_F_MIX_XFADER, DJ_F_MIX_PFL, DJ_F_MIX_CUE,
    DJ_F_MIX_JOG,                     /* "JOG: VINYL" / "JOG: CDJ" (v267) */
    DJ_F_MIX_TEMPO,                   /* "TEMPO: +/-10%", cycles 6/10/16 (v275) */
    DJ_F_LOAD_LOCK,                   /* "LOAD LOCK: OFF" / "LOAD LOCK: ON" (v293) */
    DJ_F_LINK_SYNC,                   /* "LINK SYNC: OFF" / "LINK SYNC: ON" (v304) */
    DJ_F_HC_CUES, DJ_F_HC_LOOPS, DJ_F_HC_ANLZ, DJ_F_HC_TARGET, DJ_F_HC_MEMORY,
    DJ_F_LINK_STATUS,                 /* "DJ LINK: ON P4 - CDJ-3000 #1 174.2 BPM ON AIR" */
    DJ_F_REC_STATUS, DJ_F_REC_DEST,   /* "REC 01:23  12 MB", "-> /sd/recordings" */
    DJ_F_COUNT
} dj_field_t;

typedef struct {
    const char *title, *artist, *key;
    float bpm;
    uint32_t len_ms;
    const char *badge;                /* row badge: "42%", "DB", "NET", "META"; NULL = none */
    dj_tone_t badge_tone;             /* DJ_TONE_NORMAL = info blue */
    const char *bpm_text, *time_text; /* "...", "--"; NULL = formatted from bpm / len_ms */
    const uint16_t *art;              /* DJ_ART_ROW_PX^2 RGB565, copied; NULL = none */
    bool has_progress;                /* v307: download bar along the row's bottom edge */
    uint8_t progress;                 /* 0..100 */
} dj_track_t;

/* One DJ Link player row on Settings. Text is copied. */
typedef struct {
    uint8_t number;                   /* device number, #N */
    const char *name;                 /* "CDJ-3000" */
    float bpm;                        /* <= 0 = unknown ("---") */
    bool master, on_air, playing;
} dj_ui_link_peer_t;

/* DJ Link tempo master shown in the top bar of every tab. */
typedef struct {
    uint8_t player;                   /* 0 = no master */
    float bpm;                        /* <= 0 = unknown */
    uint8_t beat;                     /* beat in bar 1..4, 0 = unknown */
    bool confirmed;                   /* false = shown with '?' */
} dj_ui_link_master_t;

/* ---- Waveform surfaces ----
 * Each deck has a zoomed waveform (DJ_WAVE_ZOOM, centred on the playhead) and a
 * full-track overview (DJ_WAVE_MINI). The pixels come from one of three sources:
 *  - PEAKS:    dj_ui draws dj_ui_set_waveform() data (demo / fallback).
 *  - IMAGE:    dj_ui draws a caller image (e.g. the RGB565 ANLZ wave-cache strip)
 *              and paints markers on top. Zoom: image column src_x + width/2 is
 *              the playhead. Mini: the image spans the whole track.
 *  - EXTERNAL: the caller paints the surface outside LVGL (direct PPA blit).
 *              dj_ui draws nothing inside it, never invalidates it, and the caller
 *              burns the markers itself (dj_ui_wave_ms_to_x gives the geometry).
 * Markers drawn by dj_ui (PEAKS/IMAGE): hot cues, red memory-cue triangles
 * (bottom edge, v309), yellow cue-point triangle,
 * loop band, armed-loop trail, playhead, played shade; dj_ui_wave_set_marks()
 * picks the layers per surface (a strip that burns some itself drops them). */
typedef enum { DJ_WAVE_ZOOM, DJ_WAVE_MINI } dj_wave_t;
typedef enum { DJ_WAVE_SRC_PEAKS, DJ_WAVE_SRC_IMAGE, DJ_WAVE_SRC_EXTERNAL } dj_wave_src_t;
enum {
    DJ_MARK_HOTCUES    = 1u << 0,
    DJ_MARK_LOOP       = 1u << 1,     /* active loop band + edges */
    DJ_MARK_CUE_POINT  = 1u << 2,
    DJ_MARK_PLAYHEAD   = 1u << 3,
    DJ_MARK_PLAYED     = 1u << 4,     /* mini shade, PEAKS zoom dimming */
    DJ_MARK_LOOP_ARMED = 1u << 5,     /* zoom: loop-in to playhead */
    DJ_MARK_MEMORY     = 1u << 6,     /* v309: memory cue triangles (bottom) */
    DJ_MARK_ALL        = 0x7fu,
};

/* Touch events -> your app. Any pointer may be NULL. */
typedef struct {
    void (*on_tab)(dj_tab_t tab);
    void (*on_hotcue)(uint8_t deck, uint8_t index, bool pressed);
    void (*on_hotcue_delete)(uint8_t deck, uint8_t index);
    void (*on_restore_cues)(uint8_t deck);             /* explicit long hold */
    void (*on_fx_beat)(uint8_t index);                  /* 0..3 = 1/4, 1/2, 1, 2 */
    void (*on_fx_toggle)(void);
    void (*on_fx_select)(void);                         /* effect name tapped: next effect */
    void (*on_fx_channel)(void);                        /* CH chip tapped: next target */
    void (*on_fx_level)(uint8_t pct);                   /* LEVEL slider moved, 0..100 */
    void (*on_lib_select)(uint8_t row);
    void (*on_lib_load)(uint8_t deck, uint8_t row);     /* not called while loads are disabled;
                                                           still called on a locked deck, the
                                                           owner refuses and says why */
    void (*on_lib_sort)(dj_sort_t sort);                /* tapped column; answer with dj_ui_library_set_sort */
    void (*on_lib_page)(int8_t dir);                    /* -1 PREV, +1 NEXT */
    void (*on_lib_source)(void);                        /* SOURCE / CANCEL button */
    void (*on_lib_playlists)(void);                     /* PLAYLISTS / ALL TRACKS / BACK button */
    void (*on_brightness)(uint8_t pct);
    void (*on_wireless)(bool on);
    void (*on_play)(uint8_t deck);
    void (*on_cue)(uint8_t deck, bool pressed);        /* CUE hold/release shares MIDI semantics */
    void (*on_master_tempo)(uint8_t deck);
    void (*on_sync)(uint8_t deck);                      /* SYNC: same as the controller button */
    void (*on_seek)(uint8_t deck, uint32_t pos_ms, dj_wave_t wave); /* tap on the zoom or mini waveform */
    void (*on_target)(uint8_t deck);                    /* deck badge / Hot Cues TARGET */
    void (*on_field)(dj_field_t field);                 /* tappable Settings field */
    void (*on_link)(bool on);                           /* DJ Link switch */
    void (*on_record)(void);                            /* RECORD / STOP REC */
    void (*on_wake)(void);                              /* screensaver dismissed by touch */
} dj_ui_callbacks_t;

void dj_ui_create(lv_obj_t *parent);                    /* e.g. lv_screen_active() */
void dj_ui_set_callbacks(const dj_ui_callbacks_t *cb);
void dj_ui_cancel_track_holds(uint8_t deck);             /* accepted load/source epoch changed */
void dj_ui_set_features(bool ethernet, bool recorder);
void dj_ui_show_tab(dj_tab_t tab);

/* ---- Decks (deck = 0 or 1). Call from the LVGL thread or inside lv_lock()/lv_unlock(). ---- */
/* v307: deck load bar under the footer title (a DJ Link download into the
 * deck). pct < 0 hides it; db = still reading the peer's export.pdb (dim). */
void dj_ui_set_load_progress(uint8_t deck, int16_t pct, bool db);
void dj_ui_set_track(uint8_t deck, const char *title, const char *artist, const char *source,
                     uint16_t track_no, uint16_t track_count, uint32_t len_ms);
void dj_ui_set_key(uint8_t deck, const char *key);
void dj_ui_set_bpm(uint8_t deck, float track_bpm, uint32_t first_beat_ms); /* original BPM of the file */
void dj_ui_set_tempo(uint8_t deck, float percent);                          /* shown BPM = bpm * (1 + tempo/100) */
/* Time labels + playhead. Invalidates the zoom (unless EXTERNAL or a ring
 * strip) and only the mini columns between the old and new playhead. */
void dj_ui_set_position(uint8_t deck, uint32_t pos_ms);
/* peaks/cores 0..255, points_per_sec points per second. cores = bright inner part (NULL = 55%).
 * Buffers are NOT copied: keep them alive while the track is loaded. */
void dj_ui_set_waveform(uint8_t deck, const uint8_t *peaks, const uint8_t *cores,
                        uint32_t count, uint16_t points_per_sec);
void dj_ui_set_hotcue(uint8_t deck, uint8_t index, bool set, uint32_t pos_ms, uint8_t color); /* color 0..7 */
void dj_ui_set_hotcue_loop(uint8_t deck, uint8_t index, bool loop);       /* "LOOP A" instead of "CUE A" */
void dj_ui_set_cue_point(uint8_t deck, bool set, uint32_t pos_ms);        /* yellow triangle (deck cue) */
/* v309: memory cue starts (copied, at most DJ_MEMORY_CUES), red triangles on
 * the bottom edge of both surfaces. count 0 clears them. */
#define DJ_MEMORY_CUES 16
void dj_ui_set_memory_cues(uint8_t deck, const uint32_t *pos_ms, uint8_t count);
void dj_ui_set_loop(uint8_t deck, bool active, uint32_t start_ms, uint32_t end_ms);
void dj_ui_set_loop_armed(uint8_t deck, bool armed, uint32_t start_ms); /* loop-in set, loop-out pending */
void dj_ui_set_artwork(uint8_t deck, const void *src);   /* lv_image_dsc_t* or "S:/path.png", 34x34. NULL = placeholder */
void dj_ui_set_artwork_pixels(uint8_t deck, const uint16_t *px); /* DJ_ART_DECK_PX^2 RGB565, copied. NULL = placeholder */
void dj_ui_set_beat_grid_visible(bool visible);
void dj_ui_set_transport(uint8_t deck, bool playing, bool cue_lit);
void dj_ui_set_master_tempo(uint8_t deck, bool on);
void dj_ui_set_sync(uint8_t deck, dj_sync_t sync);
void dj_ui_set_beat(uint8_t deck, bool valid, uint8_t phase, bool downbeat); /* phase 0..3 */
void dj_ui_set_vu(uint8_t deck, uint8_t level);          /* 0..255, caller owns peak hold / decay */
void dj_ui_set_target(uint8_t deck);                     /* performance target: badge + Hot Cues */

/* ---- Waveform surfaces (see above) ---- */
void dj_ui_wave_set_source(uint8_t deck, dj_wave_t wave, dj_wave_src_t src);
/* IMAGE source. The descriptor is NOT copied; only the pointer and src_x are stored. */
void dj_ui_wave_set_image(uint8_t deck, dj_wave_t wave, const lv_image_dsc_t *img, int32_t src_x);
/* Zoom IMAGE as a horizontal ring (the wave-cache strip): the image repeats past
 * its right edge, src_x is the ring column under the surface's left edge and
 * center_ms the time the image shows under the playhead. Markers follow
 * center_ms, and dj_ui_set_position no longer redraws the zoom: each call with
 * a new src_x/center_ms does. NULL leaves ring mode. The zoom then draws a
 * 3 px green playhead, matching the strip the PPA path burned. */
void dj_ui_wave_set_strip(uint8_t deck, const lv_image_dsc_t *img, int32_t src_x, uint32_t center_ms);
void dj_ui_wave_set_marks(uint8_t deck, dj_wave_t wave, uint8_t marks);  /* DJ_MARK_*, default ALL */
void dj_ui_wave_set_window_ms(uint8_t deck, uint32_t window_ms);         /* zoom span, default 8000 */
uint32_t dj_ui_wave_get_window_ms(uint8_t deck);
/* Absolute screen area of the surface content. Valid after the first layout. */
bool dj_ui_wave_get_area(uint8_t deck, dj_wave_t wave, lv_area_t *out);
/* x relative to the surface area for a track time; false when outside it. */
bool dj_ui_wave_ms_to_x(uint8_t deck, dj_wave_t wave, uint32_t ms, int32_t *x);
void dj_ui_wave_invalidate(uint8_t deck, dj_wave_t wave);
/* v287: true when the surface is on the active screen, its page shown and no
 * top-layer overlay (screensaver, blackout) over it: an EXTERNAL painter may
 * write there now. */
bool dj_ui_wave_exposed(uint8_t deck, dj_wave_t wave);

/* v287: the markers dj_ui draws over a zoom ring strip (set_strip), for an
 * EXTERNAL painter that burns them itself, in drawing order. x relative to the
 * surface area, already clipped to it; x1 > x2 = not shown. RGB565 colours. */
typedef struct {
    int32_t armed_x1, armed_x2;         /* armed-loop shade, armed_color at armed_opa */
    int32_t armed_line;                 /* loop-in line, 1 px, line_color; -1 = none */
    int32_t playhead_x1, playhead_x2;
    int32_t cue_x;                      /* cue-point triangle tip; INT32_MIN = none */
    int32_t cue_half_w, cue_h;          /* row r spans cue_x +- (cue_half_w - r) */
    uint16_t armed_color, line_color, playhead_color, cue_color;
    uint8_t armed_opa;
} dj_ui_zoom_marks_t;
bool dj_ui_wave_zoom_marks(uint8_t deck, dj_ui_zoom_marks_t *out);

/* ---- FX panel ----
 * channel 1 or 2, 0 = both decks ("1+2"). beat_index 0..3 = 1/4..2, any other
 * value lights no button. time_ms 0 = "- ms". The LEVEL slider keeps the
 * operator's value while it is held. */
void dj_ui_set_fx(const char *name, uint8_t channel, uint8_t beat_index,
                  uint16_t time_ms, uint8_t level, bool on);

/* ---- Top bar (all tabs) ---- */
void dj_ui_set_status(const char *text, dj_tone_t tone);                   /* "D1 LOAD 42%", "CLIP 3"; NULL = clear */
void dj_ui_set_link_master(const dj_ui_link_master_t *m);                  /* NULL = DJ Link off */

/* ---- Library ---- */
void dj_ui_library_set_rows(const dj_track_t *rows, uint8_t count);        /* max DJ_LIB_ROWS, text is copied */
void dj_ui_library_set_row_art(uint8_t row, const uint16_t *px);          /* late thumbnail, copied; NULL = none */
void dj_ui_library_set_loaded(uint8_t deck, int8_t row);                   /* -1 = not on this page */
void dj_ui_library_set_selected(int8_t row);                               /* controller browse; -1 = none */
void dj_ui_library_set_info(const char *source, uint16_t total, uint16_t page, uint16_t pages); /* dims PREV / NEXT at the ends */
void dj_ui_library_set_info_unit(const char *source, uint16_t total, const char *unit,
                                 uint16_t page, uint16_t pages);           /* unit NULL = "TRACKS" */
void dj_ui_library_set_sort(dj_sort_t sort, bool descending);              /* highlight + arrow; NONE = unsorted */
void dj_ui_library_set_deck_status(uint8_t deck, const char *status);      /* deck DJ_DECKS = "DECK 1+2" */
void dj_ui_library_set_status(const char *text, dj_tone_t tone);           /* "D2 LOADED FROM #3", "LOAD BUSY" */
void dj_ui_library_set_progress(int16_t pct);                              /* <0 hides the bar */
void dj_ui_library_set_load_enabled(bool enabled);                         /* load gate busy = false */
void dj_ui_library_set_load_locked(uint8_t deck, bool locked);             /* v293 LOAD LOCK: deck playing */
void dj_ui_library_set_source_label(const char *label);                    /* "SOURCE" / "CANCEL" */
void dj_ui_library_set_playlists_label(const char *label);                 /* "PLAYLISTS" / "ALL TRACKS" / "BACK" */

/* ---- Settings / status ---- */
void dj_ui_set_field(dj_field_t field, const char *text, dj_tone_t tone);  /* text NULL = keep text */
void dj_ui_set_brightness(uint8_t pct);                                     /* slider range 10..100 */
void dj_ui_set_wireless(bool on);
void dj_ui_set_link_enabled(bool on);
void dj_ui_set_link_peers(const dj_ui_link_peer_t *peers, uint8_t count);  /* max DJ_LINK_ROWS shown, text copied */
void dj_ui_set_recording(bool on);

/* ---- Full-screen overlays (on lv_layer_top) ---- */
void dj_ui_set_screensaver_font(const lv_font_t *font);                    /* wordmark font, default Montserrat 28 */
void dj_ui_set_screensaver(bool on);                                       /* a touch hides it and calls on_wake */
bool dj_ui_screensaver_active(void);
void dj_ui_set_blackout(bool on);                                          /* "UI OFF" test mode frame */

#ifdef __cplusplus
}
#endif
#endif
