/* SPDX-License-Identifier: MIT
 * Presentation imported from kayrozen/Pajoniiir
 * 428b97dd4a175f03d3a172c8db9c4d5ed94195fb; see DJ_UI_NOTICE.md.
 * Adapted for the shared P4 state owner, LVGL 9.5 and native board layouts. */
#include "dj_ui.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#else
#include <stdlib.h>
#endif

#define ZOOM_WINDOW_MS 8000   /* default visible time in zoomed waveform */
#define ZOOM_HDR 22           /* zoom label band kept out of IMAGE/EXTERNAL surfaces */
#define BAR_W    3
#define BAR_STEP 4
#define OV_STEP  4
#define OV_MAX   256

#define F12 (&lv_font_montserrat_12)
#define F14 (&lv_font_montserrat_14)
#define F16 (&lv_font_montserrat_16)
#define F20 (&lv_font_montserrat_20)
#define F24 (&lv_font_montserrat_24)
#define F28 (&lv_font_montserrat_28)
/* Montserrat 32 is not in the JC1060 sdkconfig (nor the simulator lv_conf). */
#if LV_FONT_MONTSERRAT_32
#define F32 (&lv_font_montserrat_32)
#else
#define F32 F28
#endif

#define C_BG        lv_color_hex(0x0d0f11)
#define C_WAVEBG    lv_color_hex(0x08090b)
#define C_PANEL     lv_color_hex(0x181a1d)
#define C_LINE      lv_color_hex(0x2b2e32)
#define C_ROWLINE   lv_color_hex(0x1f2124)
#define C_BORDER2   lv_color_hex(0x4a4e54)
#define C_INK       lv_color_hex(0xeef0f2)
#define C_MUTED     lv_color_hex(0x9a9ea4)
#define C_DIM       lv_color_hex(0x6e7278)
#define C_DARK      lv_color_hex(0x0d0f11)
#define C_WHITE     lv_color_hex(0xf6f6f6)
#define C_GRID_BEAT lv_color_hex(0x1f2226)
#define C_GRID_BAR  lv_color_hex(0x4a4e55)
#define C_OK        lv_color_hex(0x6fd48a)
#define C_WARN      lv_color_hex(0xefb453)
#define C_INFO      lv_color_hex(0x6fb0f0)
#define C_ERR       lv_color_hex(0xf06a5f)
/* Colours carried over from the current Pajoniiir overview */
#define C_CUEPT     lv_color_hex(0xffff00)   /* deck cue point triangle */
#define C_MEMCUE    lv_color_hex(0xff1744)   /* memory cue triangle (CDJ red) */
#define C_PLAYED    lv_color_hex(0xffb05a)   /* mini played shade, loop edges */
#define C_LOOP      lv_color_hex(0x6b3f00)   /* active loop band */
#define C_BEAT_OFF  lv_color_hex(0x30343b)
#define C_RED       lv_color_hex(0xff1744)   /* downbeat, VU clip segment */
#define C_GREEN     lv_color_hex(0x6ee128)   /* VU, PLAY */
#define C_AMBER     lv_color_hex(0xeb870f)   /* VU high, CUE */
#define C_STRIP_PH  lv_color_hex(0x00ff00)   /* wave-cache strip playhead (legacy PPA look) */

#if LVGL_VERSION_MAJOR == 9 && LVGL_VERSION_MINOR >= 3
#define DJ_LONG_DOT LV_LABEL_LONG_MODE_DOTS
#else
#define DJ_LONG_DOT LV_LABEL_LONG_DOT
#endif

#define UD(i)  ((void *)(intptr_t)(i))
#define IDX(e) ((int)(intptr_t)lv_event_get_user_data(e))

static const uint32_t DECK_COL[2]  = { 0x48d4a8, 0xee8fd0 };
static const uint32_t DECK_CORE[2] = { 0xb8f2dd, 0xfcd0ee };
static const uint32_t CUE_COL[8]   = { 0xe8695f, 0xe58a3c, 0xb9a92c, 0x2fc596, 0x20b6d0, 0x6c9bf5, 0xb07fe8, 0xe06aa9 };

typedef struct {
    lv_color_t color, core;
    /* overview widgets */
    lv_obj_t *info_trk, *info_src, *info_key, *info_tempo, *info_bpm;
    lv_obj_t *zoom, *zoom_time;
    lv_obj_t *pad[DJ_HOTCUES];
    lv_image_dsc_t art_dsc;           /* over s_art_px, see art_buffers() */
    lv_obj_t *ft_load_bar;           /* v307: DJ Link download into the deck */
    int16_t load_pct; bool load_db;
    lv_obj_t *ft_title, *ft_artist, *ft_art_lbl, *ft_art, *ft_remain_cap, *ft_remain, *ft_tempo, *ft_bpm_cap, *ft_bpm, *ov;
    lv_obj_t *card, *badge, *num_lbl, *beat_seg[4], *vu_seg[DJ_VU_SEGS], *play, *cue, *mt, *sync_btn;
    /* data */
    const uint8_t *peaks, *cores;
    uint32_t count; uint16_t pps;
    uint32_t len_ms, pos_ms, first_beat_ms;
    float bpm, tempo;
    int32_t last_tenth;
    bool cue_set[DJ_HOTCUES]; uint32_t cue_ms[DJ_HOTCUES]; uint8_t cue_col[DJ_HOTCUES];
    uint8_t ov_peak[OV_MAX]; int32_t ov_n; bool ov_valid;
    bool cue_loop[DJ_HOTCUES];
    bool cue_pt_set; uint32_t cue_pt_ms;
    uint8_t mem_n; uint32_t mem_ms[DJ_MEMORY_CUES];   /* v309 */
    bool loop_on; uint32_t loop_start, loop_end;
    /* surfaces, indexed by dj_wave_t */
    dj_wave_src_t src[2]; const lv_image_dsc_t *img[2]; int32_t img_x[2];
    uint8_t marks[2];
    bool ring; uint32_t ring_ms;     /* zoom IMAGE is a wave-cache ring strip */
    lv_obj_t *zoom_img;              /* v286: ring strip as a tiled lv_image child */
    bool loop_armed; uint32_t loop_armed_ms;
    uint32_t window_ms;
    int32_t mini_px;                 /* last mini playhead column (absolute), -1 = none */
    int8_t beat_lit; bool beat_down; uint8_t vu_lit;
    bool playing, cue_lit, mt_on;
    dj_sync_t sync_mode;
    bool elapsed;                    /* footer time: elapsed instead of remaining (tap) */
} deck_t;

static struct {
    deck_t deck[DJ_DECKS];
    lv_obj_t *tab_btn[4], *page[4];
    bool grid;
    lv_obj_t *fx_name, *fx_ch, *fx_beat[4], *fx_time, *fx_level, *fx_level_lbl, *fx_on;
    lv_obj_t *row[DJ_LIB_ROWS], *row_lbl[DJ_LIB_ROWS][5];
    uint8_t rows; int8_t sel; int8_t loaded[DJ_DECKS];
    lv_obj_t *lib_info, *lib_strip, *lib_deck, *lib_status, *sort_btn[4], *sort_dir[4];
    dj_sort_t sort; bool sort_desc;
    lv_obj_t *tgt_btn[DJ_DECKS], *hc_card[DJ_HOTCUES], *hc_name[DJ_HOTCUES], *hc_val[DJ_HOTCUES];
    uint8_t target;
    bool hc_held[DJ_HOTCUES];
    uint8_t hc_held_deck[DJ_HOTCUES];
    bool pad_held[DJ_DECKS][DJ_HOTCUES], cue_held[DJ_DECKS];
    lv_obj_t *bright, *bright_lbl, *wl_sw, *wl_lbl;
    lv_obj_t *f_lbl[DJ_F_COUNT], *f_box[DJ_F_COUNT];
    /* top bar */
    lv_obj_t *status_box, *status_lbl, *master_box, *master_lbl, *master_seg[4];
    /* library extras */
    lv_obj_t *row_badge[DJ_LIB_ROWS], *load_btn[DJ_DECKS], *lib_src_btn, *lib_pl_btn, *lib_msg, *lib_bar;
    lv_obj_t *lib_prev, *lib_next;
    dj_tone_t badge_tone[DJ_LIB_ROWS];
    lv_obj_t *row_art[DJ_LIB_ROWS];
    lv_obj_t *row_bar[DJ_LIB_ROWS];  /* v307: download bar on the fetching row */
    lv_image_dsc_t row_art_dsc[DJ_LIB_ROWS];
    /* last colours pushed per row: a style write invalidates even when equal */
    lv_color_t row_bg[DJ_LIB_ROWS], row_fg[DJ_LIB_ROWS], row_bc[DJ_LIB_ROWS];
    bool row_styled[DJ_LIB_ROWS];
    bool load_enabled;
    bool load_locked[DJ_DECKS];     /* v293: LOAD LOCK on and the deck plays */
    /* settings extras */
    lv_obj_t *link_sw, *peer_lbl[DJ_LINK_ROWS], *rec_btn;
    bool recording;
    /* overlays */
    lv_obj_t *saver, *saver_row, *saver_ch[9], *blackout;
    lv_timer_t *saver_timer; uint32_t saver_step;
    dj_ui_callbacks_t cb;
} g;

/* Thumbnail pixels, 8 rows + 2 decks in one block. Kept outside g: allocated
 * once and reused across dj_ui_create (which clears g). */
#define ART_ROW_PIXELS  (DJ_ART_ROW_PX * DJ_ART_ROW_PX)
#define ART_DECK_PIXELS (DJ_ART_DECK_PX * DJ_ART_DECK_PX)
static uint16_t *s_art_px;

static void art_buffers(void)
{
    const size_t n = (size_t)DJ_LIB_ROWS * ART_ROW_PIXELS + (size_t)DJ_DECKS * ART_DECK_PIXELS;
    if (!s_art_px) {
#ifdef ESP_PLATFORM
        s_art_px = heap_caps_calloc(n, sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
        s_art_px = calloc(n, sizeof(uint16_t));
#endif
    }
    if (!s_art_px) return;   /* thumbnails stay hidden */
    for (int r = 0; r < DJ_LIB_ROWS; r++) {
        lv_image_dsc_t *i = &g.row_art_dsc[r];
        i->header.magic = LV_IMAGE_HEADER_MAGIC;
        i->header.cf = LV_COLOR_FORMAT_RGB565;
        i->header.w = i->header.h = DJ_ART_ROW_PX;
        i->header.stride = DJ_ART_ROW_PX * 2;
        i->data = (const uint8_t *)(s_art_px + r * ART_ROW_PIXELS);
        i->data_size = ART_ROW_PIXELS * 2;
    }
    for (int k = 0; k < DJ_DECKS; k++) {
        lv_image_dsc_t *i = &g.deck[k].art_dsc;
        i->header.magic = LV_IMAGE_HEADER_MAGIC;
        i->header.cf = LV_COLOR_FORMAT_RGB565;
        i->header.w = i->header.h = DJ_ART_DECK_PX;
        i->header.stride = DJ_ART_DECK_PX * 2;
        i->data = (const uint8_t *)(s_art_px + DJ_LIB_ROWS * ART_ROW_PIXELS + k * ART_DECK_PIXELS);
        i->data_size = ART_DECK_PIXELS * 2;
    }
}

/* Copy into the image's own buffer; same descriptor, so invalidate (an
 * uncompressed variable image is drawn straight from its data). NULL hides
 * the image. */
static bool art_show(lv_obj_t *img, lv_image_dsc_t *dsc, const uint16_t *px, uint32_t pixels)
{
    if (!img || !px || !dsc->data) {
        if (img) lv_obj_add_flag(img, LV_OBJ_FLAG_HIDDEN);
        return false;
    }
    if (!lv_obj_has_flag(img, LV_OBJ_FLAG_HIDDEN) &&
        memcmp(dsc->data, px, pixels * sizeof(uint16_t)) == 0) return true;
    memcpy((void *)dsc->data, px, pixels * sizeof(uint16_t));
    lv_image_set_src(img, dsc);
    lv_obj_invalidate(img);
    lv_obj_remove_flag(img, LV_OBJ_FLAG_HIDDEN);
    return true;
}

/* ---------------------------------------------------------------- helpers */

static lv_obj_t *box(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg, lv_color_t bd)
{
    lv_obj_t *o = lv_obj_create(p);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(o, bd, 0);
    lv_obj_set_style_border_width(o, 1, 0);
    lv_obj_set_style_radius(o, 4, 0);
    return o;
}

static lv_obj_t *plain(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = box(p, x, y, w, h, C_BG, C_BG);
    lv_obj_set_style_bg_opa(o, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

static lv_obj_t *strip(lv_obj_t *p, int32_t h, lv_color_t c)
{
    lv_obj_t *o = box(p, 0, 0, 3, h, c, c);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    return o;
}

static lv_obj_t *txt(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *s, int32_t x, int32_t y)
{
    lv_obj_t *l = lv_label_create(p);
    lv_obj_set_style_text_font(l, f, 0);
    lv_obj_set_style_text_color(l, c, 0);
    lv_label_set_text(l, s);
    lv_obj_set_pos(l, x, y);
    return l;
}

static lv_obj_t *txt_r(lv_obj_t *p, const lv_font_t *f, lv_color_t c, const char *s, int32_t x_ofs, int32_t y)
{
    lv_obj_t *l = txt(p, f, c, s, 0, 0);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, x_ofs, y);
    return l;
}

static lv_obj_t *cap(lv_obj_t *p, const char *s, int32_t x, int32_t y)
{
    lv_obj_t *l = txt(p, F12, C_MUTED, s, x, y);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    return l;
}

static lv_obj_t *btn(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h, const char *s, const lv_font_t *f,
                     lv_color_t bg, lv_color_t fg, lv_color_t bd, lv_event_cb_t cb, void *ud)
{
    lv_obj_t *o = box(p, x, y, w, h, bg, bd);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_border_color(o, C_INK, LV_STATE_PRESSED);
    lv_obj_t *l = txt(o, f, fg, s, 0, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_center(l);
    if (cb) lv_obj_add_event_cb(o, cb, LV_EVENT_CLICKED, ud);
    return o;
}

static void btn_style(lv_obj_t *o, lv_color_t bg, lv_color_t fg, lv_color_t bd)
{
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_border_color(o, bd, 0);
    lv_obj_set_style_text_color(lv_obj_get_child(o, 0), fg, 0);
}

static lv_color_t tone_col(dj_tone_t t)
{
    switch (t) {
    case DJ_TONE_MUTED: return C_MUTED;
    case DJ_TONE_OK:    return C_OK;
    case DJ_TONE_WARN:  return C_WARN;
    case DJ_TONE_INFO:  return C_INFO;
    case DJ_TONE_ERROR: return C_ERR;
    default:            return C_INK;
    }
}

static void apply_tone(dj_field_t f, dj_tone_t t)
{
    lv_color_t c = tone_col(t);
    lv_obj_set_style_text_color(g.f_lbl[f], c, 0);
    if (g.f_box[f]) lv_obj_set_style_border_color(g.f_box[f], t == DJ_TONE_NORMAL ? C_BORDER2 : c, 0);
}

static void field_txt(lv_obj_t *p, dj_field_t f, const lv_font_t *font, int32_t x, int32_t y, int32_t w,
                      const char *s, dj_tone_t t)
{
    lv_obj_t *l = txt(p, font, C_INK, s, x, y);
    lv_label_set_long_mode(l, DJ_LONG_DOT);
    lv_obj_set_width(l, w);
    g.f_lbl[f] = l; g.f_box[f] = NULL;
    apply_tone(f, t);
}

static void field_box(lv_obj_t *p, dj_field_t f, int32_t x, int32_t y, int32_t w, int32_t h, const char *s, dj_tone_t t)
{
    lv_obj_t *b = box(p, x, y, w, h, C_PANEL, C_BORDER2);
    lv_obj_t *l = txt(b, F14, C_INK, s, 0, 0);
    lv_obj_center(l);
    g.f_box[f] = b; g.f_lbl[f] = l;
    apply_tone(f, t);
}

static void chip(lv_obj_t *p, dj_field_t f, const char *s, dj_tone_t t)
{
    lv_obj_t *b = lv_obj_create(p);
    lv_obj_remove_style_all(b);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(b, 12, 0);
    lv_obj_set_style_pad_ver(b, 7, 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_radius(b, 3, 0);
    lv_obj_t *l = txt(b, F12, C_INK, s, 0, 0);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    g.f_box[f] = b; g.f_lbl[f] = l;
    apply_tone(f, t);
}

static void field_click(lv_event_t *e);

static void tappable(dj_field_t f)
{
    lv_obj_t *o = g.f_box[f] ? g.f_box[f] : g.f_lbl[f];
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_ext_click_area(o, 6);
    lv_obj_add_event_cb(o, field_click, LV_EVENT_CLICKED, UD(f));
    if (g.f_box[f]) lv_obj_set_style_border_color(o, C_INK, LV_STATE_PRESSED);
}

static lv_obj_t *flex_row(lv_obj_t *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *o = plain(p, x, y, w, h);
    lv_obj_set_flex_flow(o, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(o, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(o, 8, 0);
    return o;
}

static void fill(lv_layer_t *layer, int32_t x1, int32_t y1, int32_t x2, int32_t y2, lv_color_t c, lv_opa_t opa)
{
    lv_draw_rect_dsc_t r;
    lv_draw_rect_dsc_init(&r);
    r.bg_color = c; r.bg_opa = opa; r.radius = 0;
    r.border_width = 0; r.outline_width = 0; r.shadow_width = 0;
    lv_area_t a = { x1, y1, x2, y2 };
    lv_draw_rect(layer, &r, &a);
}

static void fmt_mmss(char *b, size_t n, uint32_t ms)
{
    snprintf(b, n, "%02lu:%02lu.%lu", (unsigned long)(ms / 60000), (unsigned long)(ms / 1000 % 60),
             (unsigned long)(ms / 100 % 10));
}

static void fmt_hms(char *b, size_t n, uint32_t ms)
{
    unsigned long s = ms / 1000;
    snprintf(b, n, "%02lu:%02lu:%02lu", s / 3600, s / 60 % 60, s % 60);
}

/* ---------------------------------------------------------------- waveforms */

static lv_obj_t *surf_obj(deck_t *d, dj_wave_t w) { return w == DJ_WAVE_ZOOM ? d->zoom : d->ov; }

/* Content area the waveform pixels occupy. IMAGE/EXTERNAL zooms leave the top
 * label band (deck number, elapsed time) to LVGL so a direct blit never covers
 * LVGL-owned pixels. */
static void surf_area(deck_t *d, dj_wave_t w, lv_area_t *a)
{
    lv_obj_get_content_coords(surf_obj(d, w), a);
    if (w == DJ_WAVE_ZOOM && d->src[w] != DJ_WAVE_SRC_PEAKS) a->y1 += ZOOM_HDR;
}

static void draw_img(lv_layer_t *layer, const lv_image_dsc_t *img, int32_t x, const lv_area_t *clip)
{
    /* manual intersection: lv_area_intersect() is in LVGL's private headers */
    lv_area_t old = layer->_clip_area;
    lv_area_t c = { LV_MAX(old.x1, clip->x1), LV_MAX(old.y1, clip->y1),
                    LV_MIN(old.x2, clip->x2), LV_MIN(old.y2, clip->y2) };
    if (c.x1 > c.x2 || c.y1 > c.y2) return;
    layer->_clip_area = c;
    lv_draw_image_dsc_t dsc;
    lv_draw_image_dsc_init(&dsc);
    dsc.src = img;
    lv_area_t ia = { x, clip->y1, x + (int32_t)img->header.w - 1, clip->y1 + (int32_t)img->header.h - 1 };
    lv_draw_image(layer, &dsc, &ia);
    layer->_clip_area = old;
}

/* v286: a ring strip shown by the zoom_img child. The zoom box has rounded
 * corners, so LVGL's cover check fails on the waveform area and every frame
 * repainted the page and the box background under the strip, three passes
 * per pixel. An unrounded lv_image with an opaque background covers the area:
 * a scrolled frame is one copy, TILE wraps the ring. The background is only
 * there for the cover check (LVGL's image check can only veto it); the strip
 * overwrites every pixel, so zoom_img_task drops its fill. The markers draw
 * on DRAW_POST_END. */
static void zoom_img_task(lv_event_t *e)
{
    lv_draw_task_t *t = lv_event_get_draw_task(e);
    if (lv_draw_task_get_type(t) != LV_DRAW_TASK_TYPE_FILL) return;
    lv_draw_fill_dsc_t *f = lv_draw_task_get_fill_dsc(t);
    if (f) f->opa = LV_OPA_TRANSP;
}

static bool zoom_img_on(const deck_t *d)
{
    return d->zoom_img && d->ring && d->src[DJ_WAVE_ZOOM] == DJ_WAVE_SRC_IMAGE && d->img[DJ_WAVE_ZOOM];
}

static void zoom_img_sync(deck_t *d)
{
    if (!d->zoom_img) return;
    if (!zoom_img_on(d)) {
        lv_obj_add_flag(d->zoom_img, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (lv_image_get_src(d->zoom_img) != d->img[DJ_WAVE_ZOOM]) lv_image_set_src(d->zoom_img, d->img[DJ_WAVE_ZOOM]);
    lv_image_set_offset_x(d->zoom_img, -d->img_x[DJ_WAVE_ZOOM]);
    if (lv_obj_has_flag(d->zoom_img, LV_OBJ_FLAG_HIDDEN)) {
        lv_area_t a, c;
        surf_area(d, DJ_WAVE_ZOOM, &a);
        lv_obj_get_content_coords(d->zoom, &c);
        lv_obj_set_pos(d->zoom_img, a.x1 - c.x1, a.y1 - c.y1);
        lv_obj_set_size(d->zoom_img, lv_area_get_width(&a), lv_area_get_height(&a));
        lv_obj_remove_flag(d->zoom_img, LV_OBJ_FLAG_HIDDEN);
    }
}

#define CUEPT_HALF_W 4          /* zoom cue-point triangle */
#define CUEPT_H      5
#define MEMCUE_HALF_W 5         /* zoom memory-cue triangle (v309), white outline */
#define MEMCUE_H      6

/* Solid down-pointing triangle hanging from y, one rect per row. */
static void tri_down(lv_layer_t *layer, int32_t x, int32_t y, int32_t half_w, int32_t h, lv_color_t c)
{
    for (int32_t r = 0; r < h && half_w - r >= 0; r++) fill(layer, x - half_w + r, y + r, x + half_w - r, y + r, c, LV_OPA_COVER);
}

/* Base on row y (the bottom edge), tip h - 1 rows above it. */
static void tri_up(lv_layer_t *layer, int32_t x, int32_t y, int32_t half_w, int32_t h, lv_color_t c)
{
    for (int32_t r = 0; r < h && half_w - r >= 0; r++) fill(layer, x - half_w + r, y - r, x + half_w - r, y - r, c, LV_OPA_COVER);
}

/* tri_up with a 1 px outline (base row and both edges), as the wave-cache
 * strip burns a zoom memory cue (ui_overview_renderer). */
static void tri_up_outlined(lv_layer_t *layer, int32_t x, int32_t y, int32_t half_w, int32_t h,
                            lv_color_t c, lv_color_t edge)
{
    tri_up(layer, x, y, half_w, h, edge);
    for (int32_t r = 1; r < h - 1 && half_w - r - 1 >= 0; r++)
        fill(layer, x - half_w + r + 1, y - r, x + half_w - r - 1, y - r, c, LV_OPA_COVER);
}

/* Time under the zoom playhead: a ring strip shows the centre it was rendered
 * for, which may trail the live position by the caller's redraw pacing. */
static uint32_t zoom_pos(const deck_t *d)
{
    return d->ring && d->src[DJ_WAVE_ZOOM] != DJ_WAVE_SRC_PEAKS ? d->ring_ms : d->pos_ms;
}

static int32_t zoom_x(const deck_t *d, int32_t cx, float mspp, uint32_t ms)
{
    return cx + (int32_t)floorf(((float)ms - (float)zoom_pos(d)) / mspp);
}

static void zoom_draw(lv_event_t *e)
{
    deck_t *d = lv_event_get_user_data(e);
    if (d->src[DJ_WAVE_ZOOM] == DJ_WAVE_SRC_EXTERNAL) return;
    /* a ring strip is the zoom_img child: the markers go on top of it */
    bool child = zoom_img_on(d);
    if ((lv_event_get_code(e) == LV_EVENT_DRAW_POST_END) != child) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    surf_area(d, DJ_WAVE_ZOOM, &a);
    int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    int32_t cx = a.x1 + w / 2, mid = a.y1 + h / 2, maxh = (h - 20) / 2;
    float mspp = (float)d->window_ms / (float)w;
    float pos = (float)d->pos_ms;
    bool peaks = d->src[DJ_WAVE_ZOOM] == DJ_WAVE_SRC_PEAKS;
    uint8_t mk = d->marks[DJ_WAVE_ZOOM];
    int32_t lx1 = 0, lx2 = -1, ax1 = 0, ax2 = -1;

    if ((mk & DJ_MARK_LOOP) && d->loop_on && d->loop_end > d->loop_start) {
        lx1 = LV_MAX(zoom_x(d, cx, mspp, d->loop_start), a.x1);
        lx2 = LV_MIN(zoom_x(d, cx, mspp, d->loop_end), a.x2);
        if (peaks && lx1 <= lx2) fill(layer, lx1, a.y1, lx2, a.y2, C_LOOP, LV_OPA_COVER);
    }
    if ((mk & DJ_MARK_LOOP_ARMED) && d->loop_armed) {
        ax1 = LV_MAX(zoom_x(d, cx, mspp, d->loop_armed_ms), a.x1);
        ax2 = LV_MIN(cx, a.x2);
    }

    if (!peaks) {
        const lv_image_dsc_t *img = child ? NULL : d->img[DJ_WAVE_ZOOM];
        if (img) {
            int32_t x0 = a.x1 - d->img_x[DJ_WAVE_ZOOM];
            draw_img(layer, img, x0, &a);
            /* ring strip: the columns past the image's right edge wrap to column 0 */
            if (d->ring && x0 + (int32_t)img->header.w <= a.x2) draw_img(layer, img, x0 + (int32_t)img->header.w, &a);
        }
        if (lx1 <= lx2) fill(layer, lx1, a.y1, lx2, a.y2, C_PLAYED, LV_OPA_20);
        if (ax1 <= ax2) fill(layer, ax1, a.y1, ax2, a.y2, C_PLAYED, LV_OPA_20);
    } else {
        if (g.grid && d->bpm > 1.f) {
            float beat = 60000.f / d->bpm;
            float t0 = pos - (w / 2) * mspp;
            long n = (long)ceilf((t0 - (float)d->first_beat_ms) / beat);
            for (;; n++) {
                float t = (float)d->first_beat_ms + n * beat;
                int32_t x = cx + (int32_t)((t - pos) / mspp);
                if (x > a.x2) break;
                if (x < a.x1) continue;
                fill(layer, x, a.y1, x, a.y2, (((n % 4) + 4) % 4 == 0) ? C_GRID_BAR : C_GRID_BEAT, LV_OPA_COVER);
            }
        }

        if (d->peaks && d->count && d->pps) {
            for (int32_t x = a.x1; x + BAR_W - 1 <= a.x2; x += BAR_STEP) {
                float t = pos + (x + BAR_W / 2 - cx) * mspp;
                if (t < 0) continue;
                uint32_t i = (uint32_t)(t * d->pps / 1000.f);
                if (i >= d->count) break;
                int32_t hh = d->peaks[i] * maxh / 255;
                if (hh < 1) hh = 1;
                lv_opa_t op = ((mk & DJ_MARK_PLAYED) && x + BAR_W / 2 < cx) ? LV_OPA_60 : LV_OPA_COVER;
                fill(layer, x, mid - hh, x + BAR_W - 1, mid + hh, d->color, op);
                int32_t ch = hh * (d->cores ? d->cores[i] : 140) / 255;
                if (ch > 0) fill(layer, x, mid - ch, x + BAR_W - 1, mid + ch, d->core, op);
            }
        }
    }

    /* markers */
    if (lx1 <= lx2) {
        fill(layer, lx1, a.y1, lx1, a.y2, C_PLAYED, LV_OPA_COVER);
        fill(layer, lx2, a.y1, lx2, a.y2, C_PLAYED, LV_OPA_COVER);
    }
    if (ax1 <= ax2) {
        if (peaks) fill(layer, ax1, a.y1, ax2, a.y2, C_PLAYED, LV_OPA_20);
        int32_t x = zoom_x(d, cx, mspp, d->loop_armed_ms);
        if (x >= a.x1 && x <= a.x2) fill(layer, x, a.y1, x, a.y2, C_WHITE, LV_OPA_COVER);
    }
    for (int k = 0; (mk & DJ_MARK_MEMORY) && k < d->mem_n; k++) {
        int32_t x = zoom_x(d, cx, mspp, d->mem_ms[k]);
        if (x >= a.x1 && x <= a.x2) tri_up_outlined(layer, x, a.y2, MEMCUE_HALF_W, MEMCUE_H, C_MEMCUE, C_WHITE);
    }
    for (int k = 0; k < DJ_HOTCUES; k++) {
        if (!(mk & DJ_MARK_HOTCUES) || !d->cue_set[k]) continue;
        int32_t x = zoom_x(d, cx, mspp, d->cue_ms[k]);
        if (x < a.x1 || x > a.x2) continue;
        lv_color_t c = lv_color_hex(CUE_COL[d->cue_col[k]]);
        fill(layer, x, a.y1, x, a.y2, c, LV_OPA_70);
        fill(layer, x, a.y2 - 9, LV_MIN(x + 7, a.x2), a.y2, c, LV_OPA_COVER);
    }
    bool strip = !peaks && d->ring;
    /* strip look: 3 px green playhead under the cue triangle, as the PPA path burned it */
    if ((mk & DJ_MARK_PLAYHEAD) && strip) fill(layer, cx - 1, a.y1, cx + 1, a.y2, C_STRIP_PH, LV_OPA_COVER);
    if ((mk & DJ_MARK_CUE_POINT) && d->cue_pt_set) {
        int32_t x = zoom_x(d, cx, mspp, d->cue_pt_ms);
        if (x >= a.x1 && x <= a.x2) tri_down(layer, x, a.y1, CUEPT_HALF_W, CUEPT_H, C_CUEPT);
    }
    if ((mk & DJ_MARK_PLAYHEAD) && !strip) fill(layer, cx - 1, a.y1, cx, a.y2, C_WHITE, LV_OPA_COVER);
}

static int32_t mini_x(const deck_t *d, const lv_area_t *a, uint32_t ms)
{
    if (ms > d->len_ms) ms = d->len_ms;
    return a->x1 + (int32_t)((uint64_t)ms * (uint32_t)lv_area_get_width(a) / d->len_ms);
}

static void overview_draw(lv_event_t *e)
{
    deck_t *d = lv_event_get_user_data(e);
    if (d->src[DJ_WAVE_MINI] == DJ_WAVE_SRC_EXTERNAL || !d->len_ms) return;
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    surf_area(d, DJ_WAVE_MINI, &a);
    int32_t w = lv_area_get_width(&a), h = lv_area_get_height(&a);
    int32_t px = mini_x(d, &a, d->pos_ms);
    uint8_t mk = d->marks[DJ_WAVE_MINI];

    if (d->src[DJ_WAVE_MINI] == DJ_WAVE_SRC_IMAGE) {
        const lv_image_dsc_t *img = d->img[DJ_WAVE_MINI];
        if (img) draw_img(layer, img, a.x1 - d->img_x[DJ_WAVE_MINI], &a);
        if ((mk & DJ_MARK_PLAYED) && px > a.x1) fill(layer, a.x1, a.y1, px - 1, a.y2, C_PLAYED, 80);
    } else if (d->peaks && d->count) {
        int32_t n = w / OV_STEP;
        if (n > OV_MAX) n = OV_MAX;
        if (!d->ov_valid || d->ov_n != n) {
            for (int32_t k = 0; k < n; k++) {
                uint32_t i0 = (uint32_t)((uint64_t)k * d->count / n), i1 = (uint32_t)((uint64_t)(k + 1) * d->count / n);
                if (i1 <= i0) i1 = i0 + 1;
                uint8_t m = 0;
                for (uint32_t i = i0; i < i1 && i < d->count; i++) if (d->peaks[i] > m) m = d->peaks[i];
                d->ov_peak[k] = m;
            }
            d->ov_n = n; d->ov_valid = true;
        }
        int32_t base = a.y2 - 3, maxh = h - 6;
        for (int32_t k = 0; k < n; k++) {
            int32_t x = a.x1 + k * OV_STEP;
            int32_t hh = d->ov_peak[k] * maxh / 255;
            if (hh < 1) hh = 1;
            fill(layer, x, base - hh + 1, x + OV_STEP - 2, base, d->color,
                 ((mk & DJ_MARK_PLAYED) && x < px) ? LV_OPA_40 : LV_OPA_COVER);
        }
    }

    if ((mk & DJ_MARK_LOOP) && d->loop_on && d->loop_end > d->loop_start)
        fill(layer, mini_x(d, &a, d->loop_start), a.y2 - 2, mini_x(d, &a, d->loop_end), a.y2, C_PLAYED, LV_OPA_COVER);
    for (int k = 0; (mk & DJ_MARK_MEMORY) && k < d->mem_n; k++)
        tri_up(layer, mini_x(d, &a, d->mem_ms[k]), a.y2, 3, 4, C_MEMCUE);
    for (int k = 0; k < DJ_HOTCUES; k++) {
        if (!(mk & DJ_MARK_HOTCUES) || !d->cue_set[k]) continue;
        int32_t x = mini_x(d, &a, d->cue_ms[k]);
        fill(layer, x - 2, a.y1, x + 1, a.y1 + 3, lv_color_hex(CUE_COL[d->cue_col[k]]), LV_OPA_COVER);
    }
    if ((mk & DJ_MARK_CUE_POINT) && d->cue_pt_set) tri_down(layer, mini_x(d, &a, d->cue_pt_ms), a.y1, 3, 5, C_CUEPT);
    if (mk & DJ_MARK_PLAYHEAD) fill(layer, px, a.y1, px + 1, a.y2, C_WHITE, LV_OPA_COVER);
}

/* Redraw only the mini columns the playhead (and the PEAKS played-opacity edge)
 * crossed since the last call. */
static void mini_advance(deck_t *d)
{
    if (d->src[DJ_WAVE_MINI] == DJ_WAVE_SRC_EXTERNAL || !d->len_ms) return;
    lv_area_t a;
    surf_area(d, DJ_WAVE_MINI, &a);
    if (lv_area_get_width(&a) <= 0) return;
    int32_t px = mini_x(d, &a, d->pos_ms);
    if (px == d->mini_px) return;
    if (d->mini_px < 0) {
        lv_obj_invalidate(d->ov);
    } else {
        lv_area_t r = a;
        r.x1 = LV_MIN(px, d->mini_px) - OV_STEP;
        r.x2 = LV_MAX(px, d->mini_px) + OV_STEP;
        lv_obj_invalidate_area(d->ov, &r);
    }
    d->mini_px = px;
}

/* IMAGE/EXTERNAL zooms keep the label band out of their area, so only the
 * waveform pixels are redrawn (the time label invalidates itself). */
static void surf_dirty(deck_t *d, dj_wave_t w)
{
    if (d->src[w] == DJ_WAVE_SRC_EXTERNAL) return;
    if (w == DJ_WAVE_ZOOM && d->src[w] == DJ_WAVE_SRC_IMAGE) {
        lv_area_t a;
        surf_area(d, w, &a);
        lv_obj_invalidate_area(d->zoom, &a);
    } else {
        lv_obj_invalidate(surf_obj(d, w));
    }
}

/* ---------------------------------------------------------------- refresh */

static void refresh_bpm(deck_t *d)
{
    char b[16];
    if (d->bpm > 0.f) snprintf(b, sizeof b, "%.1f", (double)(d->bpm * (1.f + d->tempo / 100.f)));
    else snprintf(b, sizeof b, "---.-");
    lv_label_set_text(d->info_bpm, b);
    lv_label_set_text(d->ft_bpm, b);
    snprintf(b, sizeof b, "%+.1f%%", (double)d->tempo);
    lv_label_set_text(d->info_tempo, b);
    lv_label_set_text(d->ft_tempo, b);
}

static void refresh_hc(void)
{
    deck_t *d = &g.deck[g.target];
    char b[16];
    for (int k = 0; k < DJ_HOTCUES; k++) {
        lv_label_set_text_fmt(g.hc_name[k], "%s %c", d->cue_loop[k] ? "LOOP" : "CUE", 'A' + k);
        if (d->cue_set[k]) {
            lv_color_t c = lv_color_hex(CUE_COL[d->cue_col[k]]);
            lv_obj_set_style_bg_color(g.hc_card[k], c, 0);
            lv_obj_set_style_border_color(g.hc_card[k], c, 0);
            lv_obj_set_style_text_color(g.hc_name[k], C_DARK, 0);
            lv_obj_set_style_text_color(g.hc_val[k], C_DARK, 0);
            fmt_hms(b, sizeof b, d->cue_ms[k]);
            lv_label_set_text(g.hc_val[k], b);
        } else {
            lv_obj_set_style_bg_color(g.hc_card[k], C_PANEL, 0);
            lv_obj_set_style_border_color(g.hc_card[k], C_LINE, 0);
            lv_obj_set_style_text_color(g.hc_name[k], C_INK, 0);
            lv_obj_set_style_text_color(g.hc_val[k], C_MUTED, 0);
            lv_label_set_text(g.hc_val[k], "EMPTY");
        }
    }
    for (int i = 0; i < DJ_DECKS; i++) {
        if (i == g.target) btn_style(g.tgt_btn[i], g.deck[i].color, C_DARK, g.deck[i].color);
        else btn_style(g.tgt_btn[i], C_PANEL, C_INK, C_LINE);
        lv_obj_set_style_border_color(g.deck[i].card, i == g.target ? g.deck[i].color : C_LINE, 0);
    }
}

static void refresh_cues(uint8_t deck)
{
    deck_t *d = &g.deck[deck];
    for (int k = 0; k < DJ_HOTCUES; k++) {
        if (d->cue_set[k]) {
            lv_color_t c = lv_color_hex(CUE_COL[d->cue_col[k]]);
            btn_style(d->pad[k], c, C_DARK, c);
        } else {
            btn_style(d->pad[k], C_PANEL, C_DIM, C_LINE);
        }
    }
    surf_dirty(d, DJ_WAVE_ZOOM);
    surf_dirty(d, DJ_WAVE_MINI);
    if (deck == g.target) refresh_hc();
}

/* Only rows whose colours change: restyling all eight on every encoder step
 * redrew the whole list (v284). */
static void refresh_row(int r)
{
    lv_color_t bg = C_WAVEBG, fg = C_INK;
    if (r == g.sel) bg = C_LINE;
    if (r == g.loaded[0]) { bg = g.deck[0].color; fg = C_DARK; }
    if (r == g.loaded[1]) { bg = g.deck[1].color; fg = C_DARK; }
    lv_color_t bc = g.badge_tone[r] == DJ_TONE_NORMAL ? C_INFO : tone_col(g.badge_tone[r]);
    if (lv_color_eq(fg, C_DARK)) bc = C_DARK;
    bool styled = g.row_styled[r];
    if (!styled || !lv_color_eq(g.row_bg[r], bg)) lv_obj_set_style_bg_color(g.row[r], bg, 0);
    if (!styled || !lv_color_eq(g.row_fg[r], fg)) {
        for (int c = 0; c < 5; c++) lv_obj_set_style_text_color(g.row_lbl[r][c], fg, 0);
    }
    if (!styled || !lv_color_eq(g.row_bc[r], bc)) lv_obj_set_style_text_color(g.row_badge[r], bc, 0);
    g.row_bg[r] = bg;
    g.row_fg[r] = fg;
    g.row_bc[r] = bc;
    g.row_styled[r] = true;
}

static void refresh_rows(void)
{
    for (int r = 0; r < DJ_LIB_ROWS; r++) refresh_row(r);
}

static void refresh_transport(deck_t *d)
{
    if (d->playing) btn_style(d->play, C_GREEN, C_DARK, C_GREEN);
    else btn_style(d->play, C_PANEL, C_INK, C_LINE);
    lv_label_set_text(lv_obj_get_child(d->play, 0), d->playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    if (d->cue_lit) btn_style(d->cue, C_AMBER, C_DARK, C_AMBER);
    else btn_style(d->cue, C_PANEL, C_INK, C_LINE);
    if (d->mt_on) btn_style(d->mt, C_INFO, C_DARK, C_INFO);
    else btn_style(d->mt, C_PANEL, C_DIM, C_LINE);
    switch (d->sync_mode) {
    case DJ_SYNC_LOCAL:       btn_style(d->sync_btn, C_INFO, C_DARK, C_INFO); break;
    case DJ_SYNC_LINK_WAIT:   btn_style(d->sync_btn, C_PANEL, C_AMBER, C_AMBER); break;
    case DJ_SYNC_LINK_LOCKED: btn_style(d->sync_btn, C_GREEN, C_DARK, C_GREEN); break;
    default:                  btn_style(d->sync_btn, C_PANEL, C_DIM, C_LINE); break;
    }
}

static void refresh_load_btns(void)
{
    static const char *const label[DJ_DECKS][2] = {
        {"LOAD DECK 1", "D1 LOCKED"}, {"LOAD DECK 2", "D2 LOCKED"},
    };
    for (int i = 0; i < DJ_DECKS; i++) {
        bool on = g.load_enabled && !g.load_locked[i];
        lv_obj_set_style_opa(g.load_btn[i], on ? LV_OPA_COVER : LV_OPA_40, 0);
        lv_label_set_text(lv_obj_get_child(g.load_btn[i], 0), label[i][g.load_locked[i]]);
    }
}

static void refresh_record(void)
{
    if (g.recording) btn_style(g.rec_btn, C_ERR, C_DARK, C_ERR);
    else btn_style(g.rec_btn, C_PANEL, C_ERR, C_LINE);
    lv_label_set_text(lv_obj_get_child(g.rec_btn, 0), g.recording ? "STOP REC" : "RECORD");
}

static void refresh_sorts(void)
{
    for (int k = 0; k < 4; k++) {
        bool on = g.sort == (dj_sort_t)(k + 1);
        if (on) btn_style(g.sort_btn[k], C_INK, C_DARK, C_INK);
        else btn_style(g.sort_btn[k], C_PANEL, C_INK, C_LINE);
        lv_label_set_text(g.sort_dir[k], g.sort_desc ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
        lv_obj_set_style_text_color(g.sort_dir[k], C_DARK, 0);
        if (on) lv_obj_remove_flag(g.sort_dir[k], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g.sort_dir[k], LV_OBJ_FLAG_HIDDEN);
    }
}

/* ---------------------------------------------------------------- events */

static void tab_click(lv_event_t *e)
{
    dj_ui_show_tab((dj_tab_t)IDX(e));
    if (g.cb.on_tab) g.cb.on_tab((dj_tab_t)IDX(e));
}

static void pad_click(lv_event_t *e)
{
    int v = IDX(e);
    uint8_t deck = (uint8_t)(v / DJ_HOTCUES), pad = (uint8_t)(v % DJ_HOTCUES);
    if (deck >= DJ_DECKS) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g.pad_held[deck][pad] = true;
        if (g.cb.on_hotcue) g.cb.on_hotcue(deck, pad, true);
    } else if (g.pad_held[deck][pad] &&
               (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)) {
        g.pad_held[deck][pad] = false;
        if (g.cb.on_hotcue) g.cb.on_hotcue(deck, pad, false);
    }
}

static void hc_click(lv_event_t *e)
{
    uint8_t pad = (uint8_t)IDX(e);
    if (pad >= DJ_HOTCUES) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g.hc_held[pad] = true;
        g.hc_held_deck[pad] = g.target;
        if (g.cb.on_hotcue) g.cb.on_hotcue(g.target, pad, true);
    } else if (g.hc_held[pad] &&
               (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)) {
        g.hc_held[pad] = false;
        if (g.cb.on_hotcue) g.cb.on_hotcue(g.hc_held_deck[pad], pad, false);
    }
}

static void release_touch_holds(void)
{
    for (uint8_t deck = 0; deck < DJ_DECKS; ++deck) {
        if (g.cue_held[deck]) {
            g.cue_held[deck] = false;
            if (g.cb.on_cue) g.cb.on_cue(deck, false);
        }
        for (uint8_t pad = 0; pad < DJ_HOTCUES; ++pad) {
            if (!g.pad_held[deck][pad]) continue;
            g.pad_held[deck][pad] = false;
            if (g.cb.on_hotcue) g.cb.on_hotcue(deck, pad, false);
        }
    }
    for (uint8_t pad = 0; pad < DJ_HOTCUES; ++pad) {
        if (!g.hc_held[pad]) continue;
        g.hc_held[pad] = false;
        if (g.cb.on_hotcue) g.cb.on_hotcue(g.hc_held_deck[pad], pad, false);
    }
}

static void tgt_click(lv_event_t *e)
{
    g.target = (uint8_t)IDX(e);
    refresh_hc();
    if (g.cb.on_target) g.cb.on_target(g.target);
}

static void play_click(lv_event_t *e) { if (g.cb.on_play) g.cb.on_play((uint8_t)IDX(e)); }
static void cue_click(lv_event_t *e) {
    uint8_t deck = (uint8_t)IDX(e);
    if (deck >= DJ_DECKS) return;
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        g.cue_held[deck] = true;
        if (g.cb.on_cue) g.cb.on_cue(deck, true);
    } else if (g.cue_held[deck] &&
               (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)) {
        g.cue_held[deck] = false;
        if (g.cb.on_cue) g.cb.on_cue(deck, false);
    }
}
static void mt_click(lv_event_t *e) { if (g.cb.on_master_tempo) g.cb.on_master_tempo((uint8_t)IDX(e)); }
static void sync_click(lv_event_t *e) { if (g.cb.on_sync) g.cb.on_sync((uint8_t)IDX(e)); }

static void refresh_time(deck_t *d)
{
    char b[16];
    fmt_mmss(b, sizeof b, d->elapsed ? d->pos_ms : (d->len_ms > d->pos_ms ? d->len_ms - d->pos_ms : 0));
    lv_label_set_text(d->ft_remain, b);
}

/* tap on the footer time: remaining <-> elapsed, a display choice kept here */
static void time_click(lv_event_t *e)
{
    deck_t *d = &g.deck[IDX(e)];
    d->elapsed = !d->elapsed;
    lv_label_set_text(d->ft_remain_cap, d->elapsed ? "ELAPSED" : "REMAIN");
    refresh_time(d);
}

/* user data = deck * 2 + dj_wave_t */
static void seek_click(lv_event_t *e)
{
    int v = IDX(e);
    deck_t *d = &g.deck[v / 2];
    lv_indev_t *indev = lv_indev_active();
    if (!g.cb.on_seek || !indev || !d->len_ms) return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    surf_area(d, (dj_wave_t)(v % 2), &a);
    int32_t w = lv_area_get_width(&a);
    if (w <= 0) return;
    int32_t x = LV_CLAMP(a.x1, p.x, a.x2);
    int64_t ms;
    if (v % 2 == DJ_WAVE_MINI) ms = (int64_t)(x - a.x1) * d->len_ms / w;
    else ms = (int64_t)zoom_pos(d) + (int64_t)(x - (a.x1 + w / 2)) * d->window_ms / w;
    g.cb.on_seek((uint8_t)(v / 2), (uint32_t)LV_CLAMP(0, ms, (int64_t)d->len_ms), (dj_wave_t)(v % 2));
}

static void field_click(lv_event_t *e) { if (g.cb.on_field) g.cb.on_field((dj_field_t)IDX(e)); }
static void src_click(lv_event_t *e) { (void)e; if (g.cb.on_lib_source) g.cb.on_lib_source(); }
static void pl_click(lv_event_t *e) { (void)e; if (g.cb.on_lib_playlists) g.cb.on_lib_playlists(); }
static void rec_click(lv_event_t *e) { (void)e; if (g.cb.on_record) g.cb.on_record(); }

static void link_cb(lv_event_t *e)
{
    (void)e;
    if (g.cb.on_link) g.cb.on_link(lv_obj_has_state(g.link_sw, LV_STATE_CHECKED));
}

static void saver_click(lv_event_t *e)
{
    (void)e;
    dj_ui_set_screensaver(false);
    if (g.cb.on_wake) g.cb.on_wake();
}

/* Letter-by-letter shimmer of the wordmark, driven by the LVGL tick only. */
static void saver_tick(lv_timer_t *t)
{
    (void)t;
    int32_t head = (int32_t)(g.saver_step++ % 14) - 2;
    for (int k = 0; k < 9; k++) {
        int32_t dist = LV_ABS(k - head);
        lv_obj_set_style_text_opa(g.saver_ch[k], (lv_opa_t)(dist >= 4 ? 70 : 255 - dist * 46), 0);
    }
}

static void fx_beat_click(lv_event_t *e) { if (g.cb.on_fx_beat) g.cb.on_fx_beat((uint8_t)IDX(e)); }
static void fx_on_click(lv_event_t *e) { (void)e; if (g.cb.on_fx_toggle) g.cb.on_fx_toggle(); }
static void fx_select_click(lv_event_t *e) { (void)e; if (g.cb.on_fx_select) g.cb.on_fx_select(); }
static void fx_ch_click(lv_event_t *e) { (void)e; if (g.cb.on_fx_channel) g.cb.on_fx_channel(); }

static void fx_level_cb(lv_event_t *e)
{
    (void)e;
    int32_t v = lv_slider_get_value(g.fx_level);
    lv_label_set_text_fmt(g.fx_level_lbl, "%d%%", (int)v);
    if (g.cb.on_fx_level) g.cb.on_fx_level((uint8_t)v);
}

static void row_click(lv_event_t *e)
{
    g.sel = (int8_t)IDX(e);
    refresh_rows();
    if (g.cb.on_lib_select) g.cb.on_lib_select((uint8_t)g.sel);
}

static void load_click(lv_event_t *e)
{
    if (g.load_enabled && g.sel >= 0 && g.cb.on_lib_load) g.cb.on_lib_load((uint8_t)IDX(e), (uint8_t)g.sel);
}

/* The owner decides direction and may refuse (peer list, load busy), so the
 * highlight only moves through dj_ui_library_set_sort. */
static void sort_click(lv_event_t *e)
{
    if (g.cb.on_lib_sort) g.cb.on_lib_sort((dj_sort_t)IDX(e));
}

static void page_click(lv_event_t *e) { if (g.cb.on_lib_page) g.cb.on_lib_page((int8_t)IDX(e)); }

static void bright_cb(lv_event_t *e)
{
    (void)e;
    int32_t v = lv_slider_get_value(g.bright);
    lv_label_set_text_fmt(g.bright_lbl, "%d%%", (int)v);
    if (g.cb.on_brightness) g.cb.on_brightness((uint8_t)v);
}

static void wl_cb(lv_event_t *e)
{
    (void)e;
    bool on = lv_obj_has_state(g.wl_sw, LV_STATE_CHECKED);
    lv_label_set_text(g.wl_lbl, on ? "P4 REMOTE: ON" : "P4 REMOTE: OFF");
    if (g.cb.on_wireless) g.cb.on_wireless(on);
}

/* ---------------------------------------------------------------- screens */

static void build_overview(lv_obj_t *pg)
{
    for (int i = 0; i < DJ_DECKS; i++) {
        deck_t *d = &g.deck[i];
        int32_t y = i * 149, x0 = i * 507;
        char num[4], deck_lbl[12];
        snprintf(num, sizeof num, "%d", i + 1);
        snprintf(deck_lbl, sizeof deck_lbl, "DECK %d", i + 1);

        /* info card */
        lv_obj_t *c = box(pg, 0, y, 136, 143, C_PANEL, C_LINE);
        d->card = c;
        strip(c, 141, d->color);
        cap(c, deck_lbl, 10, 8);
        d->info_trk = txt_r(c, F12, C_MUTED, "--/--", -8, 8);
        d->info_src = txt(c, F16, C_INK, "-", 10, 26);
        /* beat indicator: one lit segment per beat of the bar, downbeat red */
        for (int k = 0; k < 4; k++) {
            d->beat_seg[k] = box(c, 10 + k * 29, 49, 26, 5, C_BEAT_OFF, C_BEAT_OFF);
            lv_obj_set_style_radius(d->beat_seg[k], 1, 0);
            lv_obj_set_style_border_width(d->beat_seg[k], 0, 0);
            lv_obj_set_style_bg_opa(d->beat_seg[k], LV_OPA_40, 0);
        }
        cap(c, "KEY", 10, 60);
        d->info_key = txt(c, F20, d->color, "--", 10, 76);
        cap(c, "TEMPO", 60, 60);
        d->info_tempo = txt(c, F20, C_INK, "+0.0%", 60, 76);
        d->info_bpm = txt(c, F28, C_INK, "---.-", 10, 104);
        txt(c, F12, C_MUTED, "BPM", 96, 118);

        /* channel VU between the card and the zoom, segment 0 at the bottom */
        for (int k = 0; k < DJ_VU_SEGS; k++) {
            d->vu_seg[k] = box(pg, 141, y + 4 + (DJ_VU_SEGS - 1 - k) * 9, 8, 8, C_LINE, C_LINE);
            lv_obj_set_style_radius(d->vu_seg[k], 1, 0);
            lv_obj_set_style_border_width(d->vu_seg[k], 0, 0);
        }

        /* zoomed waveform */
        d->zoom = box(pg, 154, y, 698, 143, C_WAVEBG, C_LINE);
        lv_obj_add_event_cb(d->zoom, zoom_draw, LV_EVENT_DRAW_MAIN_END, d);
        lv_obj_add_event_cb(d->zoom, zoom_draw, LV_EVENT_DRAW_POST_END, d);
        lv_obj_add_flag(d->zoom, LV_OBJ_FLAG_CLICKABLE);
        /* not clickable: touches fall through to the zoom's seek */
        d->zoom_img = lv_image_create(d->zoom);
        lv_obj_remove_style_all(d->zoom_img);
        lv_obj_remove_flag(d->zoom_img, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
        lv_image_set_inner_align(d->zoom_img, LV_IMAGE_ALIGN_TILE);
        lv_obj_set_style_bg_color(d->zoom_img, C_WAVEBG, 0);
        lv_obj_set_style_bg_opa(d->zoom_img, LV_OPA_COVER, 0);
        lv_obj_add_flag(d->zoom_img, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
        lv_obj_add_event_cb(d->zoom_img, zoom_img_task, LV_EVENT_DRAW_TASK_ADDED, NULL);
        lv_obj_add_flag(d->zoom_img, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(d->zoom, seek_click, LV_EVENT_CLICKED, UD(i * 2 + DJ_WAVE_ZOOM));
        d->num_lbl = txt(d->zoom, F12, d->color, num, 8, 6);
        lv_obj_set_style_text_letter_space(d->num_lbl, 1, 0);
        d->zoom_time = txt_r(d->zoom, F12, C_MUTED, "00:00.0", -8, 6);

        /* hot cue pads */
        for (int k = 0; k < DJ_HOTCUES; k++) {
            char l[2] = { (char)('A' + k), 0 };
            d->pad[k] = btn(pg, x0 + k * 63, 298, 59, 34, l, F14, C_PANEL, C_DIM, C_LINE, pad_click, UD(i * 8 + k));
            lv_obj_remove_event_cb(d->pad[k], pad_click);
            lv_obj_add_event_cb(d->pad[k], pad_click, LV_EVENT_PRESSED, UD(i * 8 + k));
            lv_obj_add_event_cb(d->pad[k], pad_click, LV_EVENT_RELEASED, UD(i * 8 + k));
            lv_obj_add_event_cb(d->pad[k], pad_click, LV_EVENT_PRESS_LOST, UD(i * 8 + k));
        }

        /* footer */
        lv_obj_t *f = box(pg, x0, 338, 501, 208, C_PANEL, C_LINE);
        lv_obj_t *badge = box(f, 76, 8, 22, 22, d->color, d->color);
        lv_obj_set_style_radius(badge, 3, 0);
        lv_obj_center(txt(badge, F12, C_DARK, i ? "D2" : "D1", 0, 0));
        lv_obj_add_flag(badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_ext_click_area(badge, 11);
        lv_obj_add_event_cb(badge, tgt_click, LV_EVENT_CLICKED, UD(i));
        d->badge = badge;
        d->ft_title = txt(f, F16, C_INK, "-", 106, 6);
        lv_label_set_long_mode(d->ft_title, DJ_LONG_DOT);
        lv_obj_set_width(d->ft_title, 336);
        d->ft_artist = txt(f, F12, C_MUTED, "", 106, 26);
        lv_label_set_long_mode(d->ft_artist, DJ_LONG_DOT);
        lv_obj_set_width(d->ft_artist, 336);
        /* v307: thin load bar under title/artist, hidden unless downloading */
        d->ft_load_bar = lv_bar_create(f);
        lv_obj_set_pos(d->ft_load_bar, 106, 43);
        lv_obj_set_size(d->ft_load_bar, 336, 4);
        lv_bar_set_range(d->ft_load_bar, 0, 100);
        lv_obj_set_style_radius(d->ft_load_bar, 2, LV_PART_MAIN);
        lv_obj_set_style_radius(d->ft_load_bar, 2, LV_PART_INDICATOR);
        lv_obj_set_style_bg_color(d->ft_load_bar, C_LINE, LV_PART_MAIN);
        lv_obj_set_style_bg_opa(d->ft_load_bar, LV_OPA_COVER, LV_PART_MAIN);
        lv_obj_set_style_bg_color(d->ft_load_bar, d->color, LV_PART_INDICATOR);
        lv_obj_add_flag(d->ft_load_bar, LV_OBJ_FLAG_HIDDEN);
        d->load_pct = -1;
        lv_obj_t *art = box(f, 453, 6, 36, 36, C_LINE, C_BORDER2);
        lv_obj_set_style_radius(art, 3, 0);
        d->ft_art_lbl = txt(art, F12, C_MUTED, "ART", 0, 0);
        lv_obj_center(d->ft_art_lbl);
        d->ft_art = lv_image_create(art);
        lv_obj_set_pos(d->ft_art, 0, 0);
        lv_obj_set_size(d->ft_art, 34, 34);
        lv_obj_add_flag(d->ft_art, LV_OBJ_FLAG_HIDDEN);

        d->ft_remain_cap = cap(f, "REMAIN", 76, 50);
        d->ft_remain = txt(f, F32, C_INK, "00:00.0", 76, 64);
        for (int k = 0; k < 2; k++) {
            lv_obj_t *t = k ? d->ft_remain : d->ft_remain_cap;
            lv_obj_add_flag(t, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_set_ext_click_area(t, 6);
            lv_obj_add_event_cb(t, time_click, LV_EVENT_CLICKED, UD(i));
        }
        cap(f, "TEMPO", 192, 50);
        d->ft_tempo = txt(f, F16, C_INK, "+0.0%", 192, 72);
        d->mt = btn(f, 264, 52, 54, 44, "MT", F14, C_PANEL, C_DIM, C_LINE, mt_click, UD(i));
        d->sync_btn = btn(f, 324, 52, 62, 44, "SYNC", F14, C_PANEL, C_DIM, C_LINE, sync_click, UD(i));
        /* v275: transport column on the left, CUE over PLAY as on a CDJ */
        d->cue = btn(f, 10, 6, 56, 44, "CUE", F14, C_PANEL, C_INK, C_LINE, cue_click, UD(i));
        lv_obj_remove_event_cb(d->cue, cue_click);
        lv_obj_add_event_cb(d->cue, cue_click, LV_EVENT_PRESSED, UD(i));
        lv_obj_add_event_cb(d->cue, cue_click, LV_EVENT_RELEASED, UD(i));
        lv_obj_add_event_cb(d->cue, cue_click, LV_EVENT_PRESS_LOST, UD(i));
        d->play = btn(f, 10, 54, 56, 44, LV_SYMBOL_PLAY, F20, C_PANEL, C_INK, C_LINE, play_click, UD(i));
        d->ft_bpm_cap = txt_r(f, F12, C_MUTED, "BPM", -10, 50);
        lv_obj_set_style_text_letter_space(d->ft_bpm_cap, 1, 0);
        d->ft_bpm = txt_r(f, F28, d->color, "---.-", -10, 64);

        d->ov = plain(f, 10, 108, 481, 90);
        lv_obj_set_style_bg_color(d->ov, C_WAVEBG, 0);
        lv_obj_set_style_bg_opa(d->ov, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(d->ov, 3, 0);
        lv_obj_add_event_cb(d->ov, overview_draw, LV_EVENT_DRAW_MAIN_END, d);
        lv_obj_add_flag(d->ov, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(d->ov, seek_click, LV_EVENT_CLICKED, UD(i * 2 + DJ_WAVE_MINI));

    }

    /* FX panel */
    lv_obj_t *fx = box(pg, 858, 0, 150, 292, C_PANEL, C_LINE);
    cap(fx, "FX", 10, 10);
    g.fx_ch = btn(fx, 94, 8, 44, 20, "CH 1", F12, C_LINE, C_INK, C_LINE, fx_ch_click, NULL);
    lv_obj_set_style_radius(g.fx_ch, 3, 0);
    lv_obj_set_ext_click_area(g.fx_ch, 6);
    /* tap the effect name for the next one, like BEAT FX SELECT */
    lv_obj_t *sel = btn(fx, 10, 32, 128, 34, "-", F24, C_PANEL, C_INK, C_LINE, fx_select_click, NULL);
    g.fx_name = lv_obj_get_child(sel, 0);
    cap(fx, "BEAT", 10, 72);
    static const char *beats[4] = { "1/4", "1/2", "1", "2" };
    for (int k = 0; k < 4; k++)
        g.fx_beat[k] = btn(fx, 10 + k * 33, 88, 29, 26, beats[k], F12, C_LINE, C_INK, C_LINE, fx_beat_click, UD(k));
    cap(fx, "TIME", 10, 126);
    g.fx_time = txt(fx, F20, C_INK, "- ms", 10, 140);
    cap(fx, "LEVEL", 10, 176);
    g.fx_level_lbl = txt_r(fx, F12, C_INK, "0%", -10, 176);
    g.fx_level = lv_slider_create(fx);
    lv_obj_set_pos(g.fx_level, 14, 200);
    lv_obj_set_size(g.fx_level, 120, 8);
    lv_slider_set_range(g.fx_level, 0, 100);
    lv_obj_set_style_bg_color(g.fx_level, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g.fx_level, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.fx_level, C_INK, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g.fx_level, C_INK, LV_PART_KNOB);
    lv_obj_set_style_pad_all(g.fx_level, 6, LV_PART_KNOB);
    lv_obj_set_ext_click_area(g.fx_level, 16);
    lv_obj_add_event_cb(g.fx_level, fx_level_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g.fx_on = btn(fx, 10, 244, 128, 38, "ON", F16, C_PANEL, C_INK, C_LINE, fx_on_click, NULL);
}

static void build_library(lv_obj_t *pg)
{
    static const char *hdr[5] = { "TITLE", "ARTIST", "KEY", "BPM", "TIME" };
    /* TITLE starts after the DJ_ART_ROW_PX thumbnail at x 10 */
    static const int32_t cx[5] = { 60, 446, 648, 712, 776 }, cw[5] = { 374, 190, 52, 52, 60 };

    lv_obj_t *t = box(pg, 0, 0, 852, 500, C_WAVEBG, C_LINE);
    for (int c = 0; c < 5; c++) {
        lv_obj_t *l = cap(t, hdr[c], cx[c], 9);
        if (c >= 2) { lv_obj_set_width(l, cw[c]); lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0); }
    }
    lv_obj_t *sep = strip(t, 1, C_LINE);
    lv_obj_set_pos(sep, 0, 30);
    lv_obj_set_width(sep, 850);

    for (int r = 0; r < DJ_LIB_ROWS; r++) {
        lv_obj_t *row = box(t, 0, 31 + r * 52, 850, 52, C_WAVEBG, C_ROWLINE);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_event_cb(row, row_click, LV_EVENT_CLICKED, UD(r));
        for (int c = 0; c < 5; c++) {
            lv_obj_t *l = txt(row, c < 2 ? F16 : F14, C_INK, "", 0, 0);
            lv_label_set_long_mode(l, DJ_LONG_DOT);
            lv_obj_set_width(l, c == 0 ? cw[0] - 56 : cw[c]);
            if (c >= 2) lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_RIGHT, 0);
            lv_obj_align(l, LV_ALIGN_LEFT_MID, cx[c], 0);
            g.row_lbl[r][c] = l;
        }
        g.row_art[r] = lv_image_create(row);
        lv_obj_set_size(g.row_art[r], DJ_ART_ROW_PX, DJ_ART_ROW_PX);
        lv_obj_align(g.row_art[r], LV_ALIGN_LEFT_MID, 10, 0);
        lv_obj_add_flag(g.row_art[r], LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(g.row_art[r], LV_OBJ_FLAG_CLICKABLE);
        /* v307: download progress along the bottom edge, as the deck bar */
        g.row_bar[r] = lv_bar_create(row);
        lv_obj_set_size(g.row_bar[r], 850, 3);
        lv_obj_align(g.row_bar[r], LV_ALIGN_BOTTOM_LEFT, 0, 0);
        lv_bar_set_range(g.row_bar[r], 0, 100);
        lv_obj_set_style_radius(g.row_bar[r], 0, LV_PART_MAIN);
        lv_obj_set_style_radius(g.row_bar[r], 0, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(g.row_bar[r], LV_OPA_TRANSP, LV_PART_MAIN);
        lv_obj_set_style_bg_color(g.row_bar[r], C_INFO, LV_PART_INDICATOR);
        lv_obj_remove_flag(g.row_bar[r], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(g.row_bar[r], LV_OBJ_FLAG_HIDDEN);
        g.row_badge[r] = txt(row, F12, C_INFO, "", 0, 0);
        lv_obj_set_width(g.row_badge[r], 48);
        lv_obj_set_style_text_align(g.row_badge[r], LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(g.row_badge[r], LV_ALIGN_LEFT_MID, cx[0] + cw[0] - 48, 0);
        g.row[r] = row;
    }

    g.lib_prev = btn(pg, 0, 506, 100, 40, "PREV", F14, C_PANEL, C_INK, C_LINE, page_click, UD(-1));
    g.lib_info = txt(pg, F12, C_MUTED, "", 106, 519);
    lv_obj_set_width(g.lib_info, 640);
    lv_obj_set_style_text_align(g.lib_info, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_letter_space(g.lib_info, 1, 0);
    g.lib_next = btn(pg, 752, 506, 100, 40, "NEXT", F14, C_PANEL, C_INK, C_LINE, page_click, UD(1));

    lv_obj_t *dc = box(pg, 858, 0, 150, 70, C_PANEL, C_LINE);
    g.lib_strip = strip(dc, 68, g.deck[0].color);
    g.lib_deck = txt(dc, F24, C_INK, "DECK 1", 0, 0);
    lv_obj_align(g.lib_deck, LV_ALIGN_TOP_MID, 0, 10);
    g.lib_status = txt(dc, F12, g.deck[0].color, "READY", 0, 0);
    lv_obj_set_style_text_letter_space(g.lib_status, 1, 0);
    lv_obj_align(g.lib_status, LV_ALIGN_TOP_MID, 0, 44);

    g.load_btn[0] = btn(pg, 858, 76, 150, 44, "LOAD DECK 1", F14, g.deck[0].color, C_DARK, g.deck[0].color, load_click, UD(0));
    g.load_btn[1] = btn(pg, 858, 126, 150, 44, "LOAD DECK 2", F14, g.deck[1].color, C_DARK, g.deck[1].color, load_click, UD(1));
    static const char *sorts[4] = { "SORT ARTIST", "SORT NAME", "SORT BPM", "SORT KEY" };
    for (int k = 0; k < 4; k++) {
        g.sort_btn[k] = btn(pg, 858, 184 + k * 50, 150, 44, sorts[k], F12, C_PANEL, C_INK, C_LINE, sort_click, UD(k + 1));
        g.sort_dir[k] = txt(g.sort_btn[k], F12, C_DARK, LV_SYMBOL_UP, 0, 0);
        lv_obj_align(g.sort_dir[k], LV_ALIGN_RIGHT_MID, -8, 0);
        lv_obj_add_flag(g.sort_dir[k], LV_OBJ_FLAG_HIDDEN);
    }

    g.lib_src_btn = btn(pg, 858, 384, 150, 40, "SOURCE", F14, C_PANEL, C_INK, C_LINE, src_click, NULL);
    g.lib_pl_btn = btn(pg, 858, 430, 150, 40, "PLAYLISTS", F14, C_PANEL, C_INK, C_LINE, pl_click, NULL);
    lv_obj_t *sb = box(pg, 858, 476, 150, 70, C_PANEL, C_LINE);
    g.lib_msg = txt(sb, F14, C_MUTED, "", 10, 8);
    lv_label_set_long_mode(g.lib_msg, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(g.lib_msg, 130);
    g.lib_bar = lv_bar_create(sb);
    lv_obj_set_pos(g.lib_bar, 10, 54);
    lv_obj_set_size(g.lib_bar, 128, 8);
    lv_bar_set_range(g.lib_bar, 0, 100);
    lv_obj_set_style_bg_color(g.lib_bar, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g.lib_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.lib_bar, C_INFO, LV_PART_INDICATOR);
    lv_obj_add_flag(g.lib_bar, LV_OBJ_FLAG_HIDDEN);
}

static void build_hotcues(lv_obj_t *pg)
{
    cap(pg, "TARGET", 414, 13);
    g.tgt_btn[0] = btn(pg, 478, 2, 64, 36, "D1", F14, C_PANEL, C_INK, C_LINE, tgt_click, UD(0));
    g.tgt_btn[1] = btn(pg, 546, 2, 64, 36, "D2", F14, C_PANEL, C_INK, C_LINE, tgt_click, UD(1));

    for (int k = 0; k < DJ_HOTCUES; k++) {
        char name[8];
        snprintf(name, sizeof name, "CUE %c", 'A' + k);
        lv_obj_t *c = box(pg, (k % 4) * 254, 48 + (k / 4) * 222, 244, 212, C_PANEL, C_LINE);
        lv_obj_set_style_radius(c, 6, 0);
        lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_style_border_color(c, C_INK, LV_STATE_PRESSED);
        lv_obj_add_event_cb(c, hc_click, LV_EVENT_PRESSED, UD(k));
        lv_obj_add_event_cb(c, hc_click, LV_EVENT_RELEASED, UD(k));
        lv_obj_add_event_cb(c, hc_click, LV_EVENT_PRESS_LOST, UD(k));
        g.hc_name[k] = txt(c, F20, C_INK, name, 14, 12);
        g.hc_val[k] = txt(c, F14, C_MUTED, "EMPTY", 0, 0);
        lv_obj_align(g.hc_val[k], LV_ALIGN_BOTTOM_RIGHT, -14, -12);
        g.hc_card[k] = c;
    }

    lv_obj_t *sb = box(pg, 0, 490, 1008, 56, C_PANEL, C_LINE);
    cap(sb, "HOT CUE STATUS", 12, 20);
    lv_obj_t *row = flex_row(sb, 150, 0, 840, 54);
    chip(row, DJ_F_HC_CUES, "CUE A-H", DJ_TONE_OK);
    chip(row, DJ_F_HC_LOOPS, "LOOP CUES", DJ_TONE_WARN);
    chip(row, DJ_F_HC_ANLZ, "ANLZ DATA", DJ_TONE_INFO);
    chip(row, DJ_F_HC_TARGET, "D1/D2 TARGET", DJ_TONE_NORMAL);
}

static void build_settings(lv_obj_t *pg)
{
    lv_color_t acc = g.deck[0].color;
    lv_obj_t *b;

    b = box(pg, 0, 0, 501, 86, C_PANEL, C_LINE);
    cap(b, "DISPLAY", 14, 12);
    g.bright = lv_slider_create(b);
    lv_obj_set_pos(g.bright, 24, 50);
    lv_obj_set_size(g.bright, 380, 8);
    lv_slider_set_range(g.bright, 10, 100);   /* backlight floor, as app_settings */
    lv_slider_set_value(g.bright, 78, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(g.bright, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(g.bright, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.bright, acc, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(g.bright, C_INK, LV_PART_KNOB);
    lv_obj_set_style_pad_all(g.bright, 7, LV_PART_KNOB);
    lv_obj_set_ext_click_area(g.bright, 16);
    lv_obj_add_event_cb(g.bright, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g.bright_lbl = txt_r(b, F16, C_INK, "78%", -14, 43);

    b = box(pg, 0, 92, 501, 90, C_PANEL, C_LINE);
    cap(b, "MASTER OUTPUT", 14, 12);
    field_box(b, DJ_F_MASTER, 14, 36, 160, 40, "MASTER: 0 dB", DJ_TONE_NORMAL);
    tappable(DJ_F_MASTER);
    field_txt(b, DJ_F_MASTER_HINT, F14, 188, 47, 296, "Lower if limiter stays active", DJ_TONE_MUTED);

    b = box(pg, 0, 188, 501, 100, C_PANEL, C_LINE);
    cap(b, "OUTPUT", 14, 12);
    field_box(b, DJ_F_OUT_MAIN, 14, 34, 230, 36, "MAIN: UNKNOWN", DJ_TONE_MUTED);
    field_box(b, DJ_F_UI_RENDER, 256, 34, 230, 36, "UI render: normal", DJ_TONE_NORMAL);
    field_txt(b, DJ_F_OUT_CUE, F12, 14, 76, 230, "CUE: UNKNOWN", DJ_TONE_MUTED);
    field_txt(b, DJ_F_LOCAL, F12, 256, 76, 230, "LOCAL: disabled", DJ_TONE_MUTED);
    tappable(DJ_F_OUT_MAIN);
    tappable(DJ_F_UI_RENDER);
    tappable(DJ_F_LOCAL);

    b = box(pg, 507, 0, 501, 182, C_PANEL, C_LINE);
    cap(b, "SYSTEM STATUS", 14, 12);
    field_txt(b, DJ_F_SYS_CONTROLLER, F14, 14, 34, 470, "Controller: UNKNOWN", DJ_TONE_MUTED);
    txt(b, F14, C_INK, "SD Card", 14, 62);
    field_txt(b, DJ_F_SYS_SD, F12, 14, 82, 470, "SD: UNKNOWN", DJ_TONE_MUTED);
    field_txt(b, DJ_F_SYS_SD_LOG, F12, 14, 100, 470, "SD Log: UNKNOWN", DJ_TONE_MUTED);
    field_txt(b, DJ_F_SYS_FW, F12, 14, 126, 470, "Firmware: UNKNOWN", DJ_TONE_MUTED);
    field_txt(b, DJ_F_SYS_RESET, F14, 14, 150, 470, "Last reset: UNKNOWN", DJ_TONE_MUTED);

    /* v294: the network box (DJ LINK + WIRELESS) takes the right column
     * under SYSTEM STATUS, DECK LOAD (LOAD LOCK + deck setup chips) the left
     * column under OUTPUT. MIXER STATUS becomes a 56 px bottom strip, the
     * HOT CUE STATUS idiom, to give the network box room for P4 REMOTE. */
    b = box(pg, 507, 188, 501, 190, C_PANEL, C_LINE);
    cap(b, "DJ LINK (ETHERNET)", 14, 12);
    g.link_sw = lv_switch_create(b);
    lv_obj_set_pos(g.link_sw, 14, 36);
    lv_obj_set_size(g.link_sw, 56, 30);
    lv_obj_set_style_bg_color(g.link_sw, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.link_sw, acc, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g.link_sw, C_INK, LV_PART_KNOB);
    lv_obj_add_event_cb(g.link_sw, link_cb, LV_EVENT_VALUE_CHANGED, NULL);
    field_txt(b, DJ_F_LINK_STATUS, F14, 84, 42, 400, "DJ LINK: OFF", DJ_TONE_MUTED);
    for (int r = 0; r < DJ_LINK_ROWS; r++) {
        g.peer_lbl[r] = txt(b, F12, C_INK, "", 14, 78 + r * 19);
        lv_label_set_long_mode(g.peer_lbl[r], DJ_LONG_DOT);
        lv_obj_set_width(g.peer_lbl[r], 470);
    }
    lv_obj_t *sep = box(b, 14, 138, 472, 1, C_LINE, C_LINE);
    lv_obj_set_style_border_width(sep, 0, 0);
    cap(b, "WIRELESS", 14, 157);
    g.wl_sw = lv_switch_create(b);
    lv_obj_set_pos(g.wl_sw, 110, 149);
    lv_obj_set_size(g.wl_sw, 56, 30);
    lv_obj_set_style_bg_color(g.wl_sw, C_LINE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(g.wl_sw, acc, LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(g.wl_sw, C_INK, LV_PART_KNOB);
    lv_obj_add_event_cb(g.wl_sw, wl_cb, LV_EVENT_VALUE_CHANGED, NULL);
    g.wl_lbl = txt(b, F14, C_INK, "P4 REMOTE: OFF", 180, 155);

    b = box(pg, 0, 294, 501, 190, C_PANEL, C_LINE);
    cap(b, "DECK LOAD & SYNC", 14, 12);
    field_box(b, DJ_F_LOAD_LOCK, 14, 36, 230, 40, "LOAD LOCK: OFF", DJ_TONE_NORMAL);
    tappable(DJ_F_LOAD_LOCK);
    field_box(b, DJ_F_LINK_SYNC, 256, 36, 230, 40, "LINK SYNC: OFF", DJ_TONE_NORMAL);
    tappable(DJ_F_LINK_SYNC);
    cap(b, "DECK SETUP", 14, 100);
    lv_obj_t *row = flex_row(b, 14, 124, 472, 40);
    chip(row, DJ_F_MIX_TEMPO, "TEMPO: +/-10%", DJ_TONE_NORMAL);
    tappable(DJ_F_MIX_TEMPO);
    chip(row, DJ_F_MIX_JOG, "JOG: VINYL", DJ_TONE_NORMAL);
    tappable(DJ_F_MIX_JOG);
    chip(row, DJ_F_MIX_CUE, "CUE: STEREO", DJ_TONE_NORMAL);
    tappable(DJ_F_MIX_CUE);

    b = box(pg, 507, 384, 501, 100, C_PANEL, C_LINE);
    cap(b, "RECORD", 14, 12);
    g.rec_btn = btn(b, 14, 36, 140, 44, "RECORD", F14, C_PANEL, C_ERR, C_LINE, rec_click, NULL);
    field_txt(b, DJ_F_REC_STATUS, F16, 170, 40, 316, "REC --:--", DJ_TONE_MUTED);
    field_txt(b, DJ_F_REC_DEST, F12, 170, 64, 316, "-> /sd/recordings", DJ_TONE_MUTED);

    b = box(pg, 0, 490, 1008, 56, C_PANEL, C_LINE);
    cap(b, "MIXER STATUS", 12, 20);
    row = flex_row(b, 150, 0, 840, 54);
    chip(row, DJ_F_MIX_MIXER, "MIXER: UNKNOWN", DJ_TONE_MUTED);
    chip(row, DJ_F_MIX_FADERS, "CH FADERS", DJ_TONE_INFO);
    chip(row, DJ_F_MIX_XFADER, "CROSSFADER", DJ_TONE_NORMAL);
    chip(row, DJ_F_MIX_PFL, "PFL D1/D2", DJ_TONE_WARN);
    lv_obj_t *sp = plain(row, 0, 0, 1, 1);
    lv_obj_set_flex_grow(sp, 1);
}

static void build_topbar(lv_obj_t *parent)
{
    g.status_box = box(parent, 786, 8, 112, 32, C_PANEL, C_LINE);
    g.status_lbl = txt(g.status_box, F12, C_MUTED, "", 0, 0);
    lv_label_set_long_mode(g.status_lbl, DJ_LONG_DOT);
    lv_obj_set_width(g.status_lbl, 104);
    lv_obj_set_style_text_align(g.status_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(g.status_lbl);

    g.master_box = box(parent, 904, 8, 112, 32, C_PANEL, C_LINE);
    g.master_lbl = txt(g.master_box, F12, C_MUTED, "", 0, 0);
    lv_obj_set_width(g.master_lbl, 104);
    lv_obj_set_style_text_align(g.master_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(g.master_lbl, LV_ALIGN_TOP_MID, 0, 2);
    for (int k = 0; k < 4; k++) {
        g.master_seg[k] = box(g.master_box, 7 + k * 25, 22, 21, 4, C_BEAT_OFF, C_BEAT_OFF);
        lv_obj_set_style_radius(g.master_seg[k], 1, 0);
        lv_obj_set_style_border_width(g.master_seg[k], 0, 0);
    }
}

static lv_obj_t *fullscreen(lv_obj_t *layer)
{
    lv_display_t *disp = lv_obj_get_display(layer);
    lv_obj_t *o = box(layer, 0, 0, lv_display_get_horizontal_resolution(disp),
                      lv_display_get_vertical_resolution(disp), lv_color_black(), lv_color_black());
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, 0, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    return o;
}

static void build_overlays(lv_obj_t *parent)
{
    lv_obj_t *top = lv_display_get_layer_top(lv_obj_get_display(parent));
    static const char word[] = "Pajoniiir";

    g.saver = fullscreen(top);
    lv_obj_add_flag(g.saver, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(g.saver, saver_click, LV_EVENT_CLICKED, NULL);
    g.saver_row = plain(g.saver, 0, 0, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(g.saver_row, LV_FLEX_FLOW_ROW);
    lv_obj_center(g.saver_row);
    for (int k = 0; k < 9; k++) {
        char ch[2] = { word[k], 0 };
        g.saver_ch[k] = txt(g.saver_row, F28, C_WHITE, ch, 0, 0);
    }
    lv_obj_t *capt = txt(g.saver, F14, C_DIM, "touch me.....or don't ;)", 0, 0);
    lv_obj_align(capt, LV_ALIGN_BOTTOM_MID, 0, -40);
    g.saver_timer = lv_timer_create(saver_tick, 120, NULL);
    lv_timer_pause(g.saver_timer);

    g.blackout = fullscreen(top);
    lv_obj_center(txt(g.blackout, F16, C_WARN, "UI OFF - PLAYING", 0, 0));
}

/* ---------------------------------------------------------------- public API */

/* Compact layout uses native LVGL objects and fonts. Secondary groups scroll;
 * no framebuffer scaling or second presentation renderer is involved. */
static void compact_layout(void)
{
    for (int p = 0; p < 4; ++p) {
        lv_obj_set_pos(g.page[p], 8, 58);
        lv_obj_set_size(g.page[p], 784, 414);
        lv_obj_add_flag(g.page[p], LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(g.page[p], LV_DIR_VER);
    }
    for (int i = 0; i < 4; ++i) {
        lv_obj_set_pos(g.tab_btn[i], 8 + i * 148, 6);
        lv_obj_set_size(g.tab_btn[i], 142, 44);
    }
    lv_obj_set_pos(g.status_box, 604, 6); lv_obj_set_size(g.status_box, 90, 44);
    lv_obj_set_width(g.status_lbl, 82);
    lv_obj_set_pos(g.master_box, 700, 6); lv_obj_set_size(g.master_box, 92, 44);
    lv_obj_set_width(g.master_lbl, 84);
    for (int k = 0; k < 4; ++k) {
        lv_obj_set_pos(g.master_seg[k], 5 + k * 21, 30);
        lv_obj_set_width(g.master_seg[k], 17);
    }
    for (int i = 0; i < DJ_DECKS; ++i) {
        deck_t *d = &g.deck[i]; int y = i * 106, x = i * 395;
        lv_obj_set_pos(d->card, 0, y); lv_obj_set_height(d->card, 102);
        lv_obj_set_pos(d->info_bpm, 10, 72); lv_obj_set_style_text_font(d->info_bpm, F20, 0);
        lv_obj_set_pos(d->info_key, 106, 76); lv_obj_set_style_text_font(d->info_key, F14, 0);
        lv_obj_add_flag(d->info_tempo, LV_OBJ_FLAG_HIDDEN);
        for (uint32_t k = 0; k < lv_obj_get_child_count(d->card); ++k) {
            lv_obj_t *o = lv_obj_get_child(d->card, (int32_t)k);
            if (lv_obj_check_type(o, &lv_label_class)) {
                const char *text = lv_label_get_text(o);
                if (!strcmp(text, "KEY") || !strcmp(text, "TEMPO") || !strcmp(text, "BPM"))
                    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
            }
        }
        lv_obj_set_pos(d->zoom, 154, y); lv_obj_set_size(d->zoom, 630, 102);
        for (int k = 0; k < DJ_VU_SEGS; ++k) {
            lv_obj_set_pos(d->vu_seg[k], 141, y + 2 + (DJ_VU_SEGS - 1 - k) * 6);
            lv_obj_set_height(d->vu_seg[k], 5);
        }
        for (int k = 0; k < DJ_HOTCUES; ++k) {
            lv_obj_set_pos(d->pad[k], x + k * 48, 214);
            lv_obj_set_size(d->pad[k], 44, 44);
        }
        lv_obj_t *f = lv_obj_get_parent(d->badge);
        lv_obj_set_pos(f, x, 264); lv_obj_set_size(f, 389, 154);
        lv_obj_set_x(d->ft_title, 102); lv_obj_set_width(d->ft_title, 236);
        lv_obj_set_x(d->ft_artist, 102); lv_obj_set_width(d->ft_artist, 236);
        lv_obj_set_x(d->ft_load_bar, 102); lv_obj_set_width(d->ft_load_bar, 236);
        lv_obj_set_x(lv_obj_get_parent(d->ft_art), 343);
        lv_obj_set_style_text_font(d->ft_remain, F24, 0);
        lv_obj_set_pos(d->ft_tempo, 182, 78); lv_obj_set_style_text_font(d->ft_tempo, F12, 0);
        lv_obj_set_pos(d->mt, 222, 52); lv_obj_set_size(d->mt, 54, 44);
        lv_obj_set_pos(d->sync_btn, 282, 52); lv_obj_set_size(d->sync_btn, 94, 44);
        lv_obj_add_flag(d->ft_bpm, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(d->ft_bpm_cap, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_size(d->ov, 369, 40);
    }
    lv_obj_t *fx = lv_obj_get_parent(g.fx_on);
    lv_obj_set_pos(fx, 0, 426); lv_obj_set_width(fx, 784);
    lv_obj_set_size(g.fx_ch, 80, 44);
    for (int k = 0; k < 4; ++k) {
        lv_obj_set_size(g.fx_beat[k], 44, 44);
        lv_obj_set_x(g.fx_beat[k], 10 + k * 50);
    }
    lv_obj_set_size(g.fx_on, 128, 44);
    lv_obj_t *table = lv_obj_get_parent(g.row[0]);
    lv_obj_set_width(table, 628);
    for (uint32_t k = 0; k < lv_obj_get_child_count(table); ++k) {
        lv_obj_t *o = lv_obj_get_child(table, (int32_t)k);
        if (!lv_obj_check_type(o, &lv_label_class)) continue;
        const char *text = lv_label_get_text(o);
        if (!strcmp(text, "ARTIST")) lv_obj_set_x(o, 318);
        else if (!strcmp(text, "BPM")) { lv_obj_set_x(o, 552); lv_obj_set_width(o, 60); }
        else if (!strcmp(text, "KEY") || !strcmp(text, "TIME"))
            lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
    }
    for (int r = 0; r < DJ_LIB_ROWS; ++r) {
        lv_obj_set_width(g.row[r], 626); lv_obj_set_width(g.row_bar[r], 626);
        lv_obj_set_width(g.row_lbl[r][0], 245);
        lv_obj_set_x(g.row_lbl[r][1], 318); lv_obj_set_width(g.row_lbl[r][1], 214);
        lv_obj_set_x(g.row_lbl[r][3], 552); lv_obj_set_width(g.row_lbl[r][3], 60);
        lv_obj_add_flag(g.row_lbl[r][2], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g.row_lbl[r][4], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_x(g.row_badge[r], 260);
    }
    for (uint32_t k = 0; k < lv_obj_get_child_count(g.page[1]); ++k) {
        lv_obj_t *o = lv_obj_get_child(g.page[1], (int32_t)k);
        if (lv_obj_get_x(o) >= 858) { lv_obj_set_x(o, 634); lv_obj_set_width(o, 150); }
    }
    /* Loading/errors remain visible beside the primary LOAD controls;
     * playlist navigation is primary; sort/source are secondary scroll content. */
    lv_obj_set_y(lv_obj_get_parent(g.lib_msg), 174);
    lv_obj_set_y(g.lib_pl_btn, 252);
    for (int k = 0; k < 4; ++k) lv_obj_set_y(g.sort_btn[k], 302 + k * 50);
    lv_obj_set_y(g.lib_src_btn, 502);
    lv_obj_set_x(g.lib_next, 528); lv_obj_set_width(g.lib_info, 410);
    int y = 0;
    for (uint32_t k = 0; k < lv_obj_get_child_count(g.page[3]); ++k) {
        lv_obj_t *o = lv_obj_get_child(g.page[3], (int32_t)k);
        lv_obj_set_pos(o, 0, y); lv_obj_set_width(o, 784);
        y += lv_obj_get_height(o) + 8;
    }
    lv_obj_set_x(g.tgt_btn[0], 316); lv_obj_set_x(g.tgt_btn[1], 388);
    for (uint32_t k = 0; k < lv_obj_get_child_count(g.page[2]); ++k) {
        lv_obj_t *o = lv_obj_get_child(g.page[2], (int32_t)k);
        if (lv_obj_check_type(o, &lv_label_class) &&
            !strcmp(lv_label_get_text(o), "TARGET")) lv_obj_set_x(o, 242);
    }
    for (int k = 0; k < DJ_HOTCUES; ++k) {
        lv_obj_set_pos(g.hc_card[k], (k % 4) * 198, 52 + (k / 4) * 156);
        lv_obj_set_size(g.hc_card[k], 190, 148);
    }
    lv_obj_t *hc_status = lv_obj_get_parent(g.f_lbl[DJ_F_HC_CUES]);
    lv_obj_set_width(hc_status, 630);
    lv_obj_set_pos(lv_obj_get_parent(hc_status), 0, 370);
    lv_obj_set_width(lv_obj_get_parent(hc_status), 784);
}

void dj_ui_set_features(bool ethernet, bool recorder)
{
    if (!ethernet) {
        lv_obj_add_flag(g.master_box, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_width(g.status_box,
            lv_display_get_horizontal_resolution(lv_obj_get_display(g.status_box)) == 800 ? 188 : 230);
        lv_obj_set_width(g.status_lbl, lv_obj_get_width(g.status_box) - 8);
        lv_obj_add_flag(g.link_sw, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g.f_lbl[DJ_F_LINK_STATUS], LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(g.f_box[DJ_F_LINK_SYNC], LV_OBJ_FLAG_HIDDEN);
        for (int k = 0; k < DJ_LINK_ROWS; ++k) lv_obj_add_flag(g.peer_lbl[k], LV_OBJ_FLAG_HIDDEN);
        lv_obj_t *network = lv_obj_get_parent(g.link_sw);
        lv_label_set_text(lv_obj_get_child(network, 0), "NETWORK / REMOTE");
    }
    if (!recorder) lv_obj_add_flag(lv_obj_get_parent(g.rec_btn), LV_OBJ_FLAG_HIDDEN);
    if (lv_display_get_horizontal_resolution(lv_obj_get_display(g.page[3])) == 800) {
        int y = 0;
        for (uint32_t k = 0; k < lv_obj_get_child_count(g.page[3]); ++k) {
            lv_obj_t *o = lv_obj_get_child(g.page[3], (int32_t)k);
            if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) continue;
            lv_obj_set_y(o, y);
            y += lv_obj_get_height(o) + 8;
        }
    }
}

void dj_ui_create(lv_obj_t *parent)
{
    release_touch_holds();
    /* the only LVGL resource not owned by the parent tree (re-create safe) */
    if (g.saver_timer) lv_timer_delete(g.saver_timer);
    memset(&g, 0, sizeof g);
    g.grid = true; g.sel = -1; g.loaded[0] = g.loaded[1] = -1;
    g.load_enabled = true;
    for (int i = 0; i < DJ_DECKS; i++) {
        g.deck[i].color = lv_color_hex(DECK_COL[i]);
        g.deck[i].core = lv_color_hex(DECK_CORE[i]);
        g.deck[i].last_tenth = -1;
        g.deck[i].window_ms = ZOOM_WINDOW_MS;
        g.deck[i].mini_px = -1;
        g.deck[i].beat_lit = -1;
        g.deck[i].marks[DJ_WAVE_ZOOM] = g.deck[i].marks[DJ_WAVE_MINI] = DJ_MARK_ALL;
    }

    lv_obj_set_style_bg_color(parent, C_BG, 0);
    lv_obj_set_style_bg_opa(parent, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(parent, 0, 0);
    lv_obj_remove_flag(parent, LV_OBJ_FLAG_SCROLLABLE);

    for (int i = 0; i < 4; i++) g.page[i] = plain(parent, 8, 46, 1008, 546);
    art_buffers();
    build_overview(g.page[0]);
    build_library(g.page[1]);
    build_hotcues(g.page[2]);
    build_settings(g.page[3]);

    static const char *names[4] = { "OVERVIEW", "LIBRARY", "HOT CUES", "SETTINGS" };
    for (int i = 0; i < 4; i++)
        g.tab_btn[i] = btn(parent, 8 + i * 194, 8, 190, 32, names[i], F14, C_PANEL, C_INK, C_LINE, tab_click, UD(i));
    build_topbar(parent);
    build_overlays(parent);
    lv_obj_update_layout(parent);
    if (lv_display_get_horizontal_resolution(lv_obj_get_display(parent)) == 800)
        compact_layout();

    for (int i = 0; i < DJ_DECKS; i++) {
        refresh_cues((uint8_t)i);
        refresh_transport(&g.deck[i]);
    }
    refresh_hc();
    refresh_sorts();
    refresh_load_btns();
    refresh_record();
    dj_ui_set_status(NULL, DJ_TONE_MUTED);
    dj_ui_set_link_master(NULL);
    dj_ui_set_fx("-", 1, 1, 0, 0, false);
    dj_ui_show_tab(DJ_TAB_OVERVIEW);
}

void dj_ui_set_callbacks(const dj_ui_callbacks_t *cb)
{
    release_touch_holds();
    if (cb) g.cb = *cb;
    else memset(&g.cb, 0, sizeof g.cb);
}

void dj_ui_show_tab(dj_tab_t tab)
{
    if ((unsigned)tab > DJ_TAB_SETTINGS) return;
    release_touch_holds();
    for (int i = 0; i < 4; i++) {
        if (i == (int)tab) {
            lv_obj_remove_flag(g.page[i], LV_OBJ_FLAG_HIDDEN);
            btn_style(g.tab_btn[i], C_INK, C_DARK, C_INK);
        } else {
            lv_obj_add_flag(g.page[i], LV_OBJ_FLAG_HIDDEN);
            btn_style(g.tab_btn[i], C_PANEL, C_INK, C_LINE);
        }
    }
}

void dj_ui_set_load_progress(uint8_t deck, int16_t pct, bool db)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (pct > 100) pct = 100;
    if (pct < 0) pct = -1;
    if (pct == d->load_pct && db == d->load_db) return;
    if (pct < 0) {
        lv_obj_add_flag(d->ft_load_bar, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (db != d->load_db || d->load_pct < 0) {
            lv_obj_set_style_bg_color(d->ft_load_bar, db ? C_DIM : d->color, LV_PART_INDICATOR);
        }
        lv_bar_set_value(d->ft_load_bar, pct, LV_ANIM_OFF);
        lv_obj_remove_flag(d->ft_load_bar, LV_OBJ_FLAG_HIDDEN);
    }
    d->load_pct = pct;
    d->load_db = db;
}

void dj_ui_set_track(uint8_t deck, const char *title, const char *artist, const char *source,
                     uint16_t track_no, uint16_t track_count, uint32_t len_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    lv_label_set_text(d->ft_title, title ? title : "");
    lv_label_set_text(d->ft_artist, artist ? artist : "");
    lv_label_set_text(d->info_src, source ? source : "");
    lv_label_set_text_fmt(d->info_trk, "%02u/%u", (unsigned)track_no, (unsigned)track_count);
    d->len_ms = len_ms;
    d->ov_valid = false;
    d->last_tenth = -1;
    d->mini_px = -1;
    dj_ui_set_position(deck, d->pos_ms);
}

void dj_ui_set_key(uint8_t deck, const char *key)
{
    if (deck >= DJ_DECKS) return;
    lv_label_set_text(g.deck[deck].info_key, key);
    lv_label_set_text_fmt(g.deck[deck].ft_bpm_cap, "BPM / %s", key);
}

void dj_ui_set_bpm(uint8_t deck, float track_bpm, uint32_t first_beat_ms)
{
    if (deck >= DJ_DECKS) return;
    g.deck[deck].bpm = track_bpm;
    g.deck[deck].first_beat_ms = first_beat_ms;
    refresh_bpm(&g.deck[deck]);
    surf_dirty(&g.deck[deck], DJ_WAVE_ZOOM);
}

void dj_ui_set_tempo(uint8_t deck, float percent)
{
    if (deck >= DJ_DECKS) return;
    if (g.deck[deck].tempo == percent) return;
    g.deck[deck].tempo = percent;
    refresh_bpm(&g.deck[deck]);
}

void dj_ui_set_position(uint8_t deck, uint32_t pos_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    d->pos_ms = pos_ms;
    int32_t tenth = (int32_t)(pos_ms / 100);
    if (tenth != d->last_tenth) {
        char b[16];
        d->last_tenth = tenth;
        fmt_mmss(b, sizeof b, pos_ms);
        lv_label_set_text(d->zoom_time, b);
        refresh_time(d);
    }
    /* a ring strip is redrawn by the caller's dj_ui_wave_set_strip() pacing */
    if (!(d->ring && d->src[DJ_WAVE_ZOOM] == DJ_WAVE_SRC_IMAGE)) surf_dirty(d, DJ_WAVE_ZOOM);
    mini_advance(d);
}

void dj_ui_set_waveform(uint8_t deck, const uint8_t *peaks, const uint8_t *cores, uint32_t count, uint16_t pps)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    d->peaks = peaks; d->cores = cores; d->count = count; d->pps = pps;
    d->ov_valid = false;
    surf_dirty(d, DJ_WAVE_ZOOM);
    surf_dirty(d, DJ_WAVE_MINI);
}

void dj_ui_set_hotcue(uint8_t deck, uint8_t index, bool set, uint32_t pos_ms, uint8_t color)
{
    if (deck >= DJ_DECKS || index >= DJ_HOTCUES) return;
    deck_t *d = &g.deck[deck];
    d->cue_set[index] = set;
    d->cue_ms[index] = pos_ms;
    d->cue_col[index] = color & 7;
    refresh_cues(deck);
}

void dj_ui_set_hotcue_loop(uint8_t deck, uint8_t index, bool loop)
{
    if (deck >= DJ_DECKS || index >= DJ_HOTCUES) return;
    g.deck[deck].cue_loop[index] = loop;
    if (deck == g.target) refresh_hc();
}

void dj_ui_set_cue_point(uint8_t deck, bool set, uint32_t pos_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (d->cue_pt_set == set && d->cue_pt_ms == pos_ms) return;
    d->cue_pt_set = set;
    d->cue_pt_ms = pos_ms;
    surf_dirty(d, DJ_WAVE_ZOOM);
    surf_dirty(d, DJ_WAVE_MINI);
}

void dj_ui_set_memory_cues(uint8_t deck, const uint32_t *pos_ms, uint8_t count)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (!pos_ms) count = 0;
    if (count > DJ_MEMORY_CUES) count = DJ_MEMORY_CUES;
    if (d->mem_n == count && (!count || !memcmp(d->mem_ms, pos_ms, count * sizeof(uint32_t)))) return;
    d->mem_n = count;
    if (count) memcpy(d->mem_ms, pos_ms, count * sizeof(uint32_t));
    surf_dirty(d, DJ_WAVE_ZOOM);
    surf_dirty(d, DJ_WAVE_MINI);
}

void dj_ui_set_loop(uint8_t deck, bool active, uint32_t start_ms, uint32_t end_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (d->loop_on == active && d->loop_start == start_ms && d->loop_end == end_ms) return;
    d->loop_on = active;
    d->loop_start = start_ms;
    d->loop_end = end_ms;
    surf_dirty(d, DJ_WAVE_ZOOM);
    surf_dirty(d, DJ_WAVE_MINI);
}

void dj_ui_set_loop_armed(uint8_t deck, bool armed, uint32_t start_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (d->loop_armed == armed && (!armed || d->loop_armed_ms == start_ms)) return;
    d->loop_armed = armed;
    d->loop_armed_ms = start_ms;
    surf_dirty(d, DJ_WAVE_ZOOM);
}

void dj_ui_set_artwork(uint8_t deck, const void *src)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (src) {
        lv_image_set_src(d->ft_art, src);
        lv_obj_remove_flag(d->ft_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(d->ft_art_lbl, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(d->ft_art, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(d->ft_art_lbl, LV_OBJ_FLAG_HIDDEN);
    }
}

void dj_ui_set_artwork_pixels(uint8_t deck, const uint16_t *px)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (art_show(d->ft_art, &d->art_dsc, px, ART_DECK_PIXELS)) lv_obj_add_flag(d->ft_art_lbl, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(d->ft_art_lbl, LV_OBJ_FLAG_HIDDEN);
}

void dj_ui_set_beat_grid_visible(bool visible)
{
    g.grid = visible;
    for (int i = 0; i < DJ_DECKS; i++) surf_dirty(&g.deck[i], DJ_WAVE_ZOOM);
}

void dj_ui_set_transport(uint8_t deck, bool playing, bool cue_lit)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    if (d->playing == playing && d->cue_lit == cue_lit) return;
    d->playing = playing;
    d->cue_lit = cue_lit;
    refresh_transport(d);
}

void dj_ui_set_master_tempo(uint8_t deck, bool on)
{
    if (deck >= DJ_DECKS || g.deck[deck].mt_on == on) return;
    g.deck[deck].mt_on = on;
    refresh_transport(&g.deck[deck]);
}

void dj_ui_set_sync(uint8_t deck, dj_sync_t sync)
{
    if (deck >= DJ_DECKS || g.deck[deck].sync_mode == sync) return;
    g.deck[deck].sync_mode = sync;
    refresh_transport(&g.deck[deck]);
}

void dj_ui_set_beat(uint8_t deck, bool valid, uint8_t phase, bool downbeat)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    int8_t lit = (valid && phase < 4) ? (int8_t)phase : -1;
    if (lit == d->beat_lit && downbeat == d->beat_down) return;
    for (int k = 0; k < 4; k++) {
        bool on = k == lit, was = k == d->beat_lit;
        if (!on && !was) continue;
        lv_obj_set_style_bg_color(d->beat_seg[k], on ? (downbeat ? C_RED : d->color) : C_BEAT_OFF, 0);
        lv_obj_set_style_bg_opa(d->beat_seg[k], on ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    d->beat_lit = lit;
    d->beat_down = downbeat;
}

void dj_ui_set_vu(uint8_t deck, uint8_t level)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    uint8_t lit = (uint8_t)((level * DJ_VU_SEGS + 254) / 255);
    if (lit == d->vu_lit) return;
    /* only the segments whose state flips are restyled */
    for (int k = LV_MIN(lit, d->vu_lit); k < LV_MAX(lit, d->vu_lit); k++) {
        lv_color_t c = C_LINE;
        if (k < lit) c = k >= DJ_VU_SEGS - 1 ? C_RED : (k >= DJ_VU_SEGS - 3 ? C_AMBER : C_GREEN);
        lv_obj_set_style_bg_color(d->vu_seg[k], c, 0);
    }
    d->vu_lit = lit;
}

void dj_ui_set_target(uint8_t deck)
{
    if (deck >= DJ_DECKS || g.target == deck) return;
    g.target = deck;
    refresh_hc();
}

void dj_ui_wave_set_source(uint8_t deck, dj_wave_t wave, dj_wave_src_t src)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI) return;
    deck_t *d = &g.deck[deck];
    d->src[wave] = src;
    d->mini_px = -1;
    if (wave == DJ_WAVE_ZOOM) zoom_img_sync(d);
    /* one full redraw so LVGL paints (or stops painting) the whole surface */
    lv_obj_invalidate(surf_obj(d, wave));
}

void dj_ui_wave_set_image(uint8_t deck, dj_wave_t wave, const lv_image_dsc_t *img, int32_t src_x)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI) return;
    deck_t *d = &g.deck[deck];
    if (d->img[wave] == img && d->img_x[wave] == src_x && !(wave == DJ_WAVE_ZOOM && d->ring)) return;
    if (wave == DJ_WAVE_ZOOM) d->ring = false;
    d->img[wave] = img;
    d->img_x[wave] = src_x;
    if (wave == DJ_WAVE_ZOOM) zoom_img_sync(d);
    if (d->src[wave] == DJ_WAVE_SRC_IMAGE) lv_obj_invalidate(surf_obj(d, wave));
}

void dj_ui_wave_set_strip(uint8_t deck, const lv_image_dsc_t *img, int32_t src_x, uint32_t center_ms)
{
    if (deck >= DJ_DECKS) return;
    deck_t *d = &g.deck[deck];
    bool ring = img != NULL;
    if (d->ring == ring && d->img[DJ_WAVE_ZOOM] == img && d->img_x[DJ_WAVE_ZOOM] == src_x && d->ring_ms == center_ms) return;
    d->ring = ring;
    d->ring_ms = center_ms;
    d->img[DJ_WAVE_ZOOM] = img;
    d->img_x[DJ_WAVE_ZOOM] = src_x;
    zoom_img_sync(d);
    surf_dirty(d, DJ_WAVE_ZOOM);
}

void dj_ui_wave_set_marks(uint8_t deck, dj_wave_t wave, uint8_t marks)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI || g.deck[deck].marks[wave] == marks) return;
    g.deck[deck].marks[wave] = marks;
    surf_dirty(&g.deck[deck], wave);
}

void dj_ui_wave_set_window_ms(uint8_t deck, uint32_t window_ms)
{
    if (deck >= DJ_DECKS || window_ms == 0) return;
    g.deck[deck].window_ms = window_ms;
    surf_dirty(&g.deck[deck], DJ_WAVE_ZOOM);
}

uint32_t dj_ui_wave_get_window_ms(uint8_t deck)
{
    return deck < DJ_DECKS ? g.deck[deck].window_ms : 0;
}

bool dj_ui_wave_get_area(uint8_t deck, dj_wave_t wave, lv_area_t *out)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI || !out) return false;
    surf_area(&g.deck[deck], wave, out);
    return lv_area_get_width(out) > 0 && lv_area_get_height(out) > 0;
}

bool dj_ui_wave_ms_to_x(uint8_t deck, dj_wave_t wave, uint32_t ms, int32_t *x)
{
    lv_area_t a;
    if (!x || !dj_ui_wave_get_area(deck, wave, &a)) return false;
    deck_t *d = &g.deck[deck];
    int32_t w = lv_area_get_width(&a), ax;
    if (wave == DJ_WAVE_MINI) {
        if (!d->len_ms || ms > d->len_ms) return false;
        ax = mini_x(d, &a, ms);
    } else {
        ax = zoom_x(d, a.x1 + w / 2, (float)d->window_ms / (float)w, ms);
    }
    if (ax < a.x1 || ax > a.x2) return false;
    *x = ax - a.x1;
    return true;
}

void dj_ui_wave_invalidate(uint8_t deck, dj_wave_t wave)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI) return;
    surf_dirty(&g.deck[deck], wave);
}

bool dj_ui_wave_exposed(uint8_t deck, dj_wave_t wave)
{
    if (deck >= DJ_DECKS || wave > DJ_WAVE_MINI) return false;
    deck_t *d = &g.deck[deck];
    lv_obj_t *o = surf_obj(d, wave);
    if (!o) return false;
    lv_display_t *disp = lv_obj_get_display(o);
    if (lv_obj_get_screen(o) != lv_display_get_screen_active(disp) || !lv_obj_is_visible(o)) return false;
    lv_area_t a;
    surf_area(d, wave, &a);
    lv_obj_t *top = lv_display_get_layer_top(disp);
    uint32_t n = lv_obj_get_child_count(top);
    for (uint32_t i = 0; i < n; i++) {
        lv_obj_t *c = lv_obj_get_child(top, (int32_t)i);
        if (lv_obj_has_flag(c, LV_OBJ_FLAG_HIDDEN)) continue;
        lv_area_t cc;
        lv_obj_get_coords(c, &cc);
        if (cc.x1 <= a.x2 && cc.x2 >= a.x1 && cc.y1 <= a.y2 && cc.y2 >= a.y1) return false;
    }
    return true;
}

/* Same geometry and colours as zoom_draw's ring-strip branch (marks as set). */
bool dj_ui_wave_zoom_marks(uint8_t deck, dj_ui_zoom_marks_t *out)
{
    if (deck >= DJ_DECKS || !out) return false;
    deck_t *d = &g.deck[deck];
    lv_area_t a;
    surf_area(d, DJ_WAVE_ZOOM, &a);
    int32_t w = lv_area_get_width(&a);
    if (w <= 0 || lv_area_get_height(&a) <= 0 || !d->window_ms) return false;
    int32_t cx = w / 2;
    float mspp = (float)d->window_ms / (float)w;
    uint8_t mk = d->marks[DJ_WAVE_ZOOM];
    memset(out, 0, sizeof *out);
    out->armed_x1 = out->playhead_x1 = 0;
    out->armed_x2 = out->playhead_x2 = -1;
    out->armed_line = -1;
    out->cue_x = INT32_MIN;
    out->cue_half_w = CUEPT_HALF_W;
    out->cue_h = CUEPT_H;
    out->armed_color = lv_color_to_u16(C_PLAYED);
    out->armed_opa = LV_OPA_20;
    out->line_color = lv_color_to_u16(C_WHITE);
    out->playhead_color = lv_color_to_u16(C_STRIP_PH);
    out->cue_color = lv_color_to_u16(C_CUEPT);
    if ((mk & DJ_MARK_LOOP_ARMED) && d->loop_armed) {
        int32_t x = zoom_x(d, cx, mspp, d->loop_armed_ms);
        out->armed_x1 = LV_MAX(x, 0);
        out->armed_x2 = LV_MIN(cx, w - 1);
        if (out->armed_x1 <= out->armed_x2 && x >= 0 && x < w) out->armed_line = x;
    }
    if (mk & DJ_MARK_PLAYHEAD) {
        out->playhead_x1 = cx - 1;
        out->playhead_x2 = LV_MIN(cx + 1, w - 1);
    }
    if ((mk & DJ_MARK_CUE_POINT) && d->cue_pt_set) {
        int32_t x = zoom_x(d, cx, mspp, d->cue_pt_ms);
        if (x >= 0 && x < w) out->cue_x = x;
    }
    return true;
}

void dj_ui_set_fx(const char *name, uint8_t channel, uint8_t beat_index, uint16_t time_ms, uint8_t level, bool on)
{
    lv_label_set_text(g.fx_name, name ? name : "-");
    if (channel == 1 || channel == 2) lv_label_set_text_fmt(lv_obj_get_child(g.fx_ch, 0), "CH %u", (unsigned)channel);
    else lv_label_set_text(lv_obj_get_child(g.fx_ch, 0), "1+2");
    for (int k = 0; k < 4; k++) {
        if (k == beat_index) btn_style(g.fx_beat[k], C_INK, C_DARK, C_INK);
        else btn_style(g.fx_beat[k], C_LINE, C_INK, C_LINE);
    }
    if (time_ms) lv_label_set_text_fmt(g.fx_time, "%u ms", (unsigned)time_ms);
    else lv_label_set_text(g.fx_time, "- ms");
    if (level > 100) level = 100;
    /* A held slider is the operator's: skip the state echo of its own value. */
    if (!lv_obj_has_state(g.fx_level, LV_STATE_PRESSED)) {
        lv_slider_set_value(g.fx_level, level, LV_ANIM_OFF);
        lv_label_set_text_fmt(g.fx_level_lbl, "%u%%", (unsigned)level);
    }
    if (on) btn_style(g.fx_on, C_INK, C_DARK, C_INK);
    else btn_style(g.fx_on, C_PANEL, C_MUTED, C_LINE);
    lv_label_set_text(lv_obj_get_child(g.fx_on, 0), on ? "ON" : "OFF");
}

void dj_ui_library_set_rows(const dj_track_t *rows, uint8_t count)
{
    /* A page/folder transition must reveal its new first rows, even when the
     * compact operator scrolled down to a secondary navigation action. */
    lv_obj_scroll_to(g.page[DJ_TAB_LIBRARY], 0, 0, LV_ANIM_OFF);
    if (count > DJ_LIB_ROWS) count = DJ_LIB_ROWS;
    g.rows = count;
    if (g.sel >= count) g.sel = -1;
    for (int r = 0; r < DJ_LIB_ROWS; r++) {
        /* Filled hidden: one area per row instead of one per label, which
         * overflowed LVGL's 32 invalidation slots into a full-screen redraw. */
        lv_obj_add_flag(g.row[r], LV_OBJ_FLAG_HIDDEN);
        if (r >= count) continue;
        const dj_track_t *t = &rows[r];
        lv_label_set_text(g.row_lbl[r][0], t->title ? t->title : "");
        lv_label_set_text(g.row_lbl[r][1], t->artist ? t->artist : "");
        lv_label_set_text(g.row_lbl[r][2], t->key ? t->key : "");
        if (t->bpm_text) lv_label_set_text(g.row_lbl[r][3], t->bpm_text);
        else lv_label_set_text_fmt(g.row_lbl[r][3], "%d", (int)(t->bpm + 0.5f));
        if (t->time_text) lv_label_set_text(g.row_lbl[r][4], t->time_text);
        else lv_label_set_text_fmt(g.row_lbl[r][4], "%lu:%02lu", (unsigned long)(t->len_ms / 60000),
                                   (unsigned long)(t->len_ms / 1000 % 60));
        lv_label_set_text(g.row_badge[r], t->badge ? t->badge : "");
        g.badge_tone[r] = t->badge_tone;
        if (t->has_progress) {
            lv_bar_set_value(g.row_bar[r], t->progress > 100 ? 100 : t->progress, LV_ANIM_OFF);
            lv_obj_remove_flag(g.row_bar[r], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(g.row_bar[r], LV_OBJ_FLAG_HIDDEN);
        }
        art_show(g.row_art[r], &g.row_art_dsc[r], t->art, ART_ROW_PIXELS);
        refresh_row(r);
        lv_obj_remove_flag(g.row[r], LV_OBJ_FLAG_HIDDEN);
    }
}

void dj_ui_library_set_row_art(uint8_t row, const uint16_t *px)
{
    if (row >= g.rows) return;
    art_show(g.row_art[row], &g.row_art_dsc[row], px, ART_ROW_PIXELS);
}

void dj_ui_library_set_loaded(uint8_t deck, int8_t row)
{
    if (deck >= DJ_DECKS) return;
    if (g.loaded[deck] == row) return;
    g.loaded[deck] = row;
    refresh_rows();
}

void dj_ui_library_set_selected(int8_t row)
{
    if (row >= (int8_t)g.rows) row = -1;
    if (row < -1) row = -1;
    if (g.sel == row) return;
    g.sel = row;
    refresh_rows();
}

void dj_ui_library_set_info(const char *source, uint16_t total, uint16_t page, uint16_t pages)
{
    dj_ui_library_set_info_unit(source, total, NULL, page, pages);
}

void dj_ui_library_set_info_unit(const char *source, uint16_t total, const char *unit,
                                 uint16_t page, uint16_t pages)
{
    lv_label_set_text_fmt(g.lib_info, "%s     %u %s     PAGE %u/%u", source, (unsigned)total,
                          unit ? unit : "TRACKS", (unsigned)page, (unsigned)pages);
    lv_obj_set_style_opa(g.lib_prev, page > 1 ? LV_OPA_COVER : LV_OPA_40, 0);
    lv_obj_set_style_opa(g.lib_next, page < pages ? LV_OPA_COVER : LV_OPA_40, 0);
}

void dj_ui_library_set_sort(dj_sort_t sort, bool descending)
{
    if (sort > DJ_SORT_KEY) sort = DJ_SORT_NONE;
    if (g.sort == sort && g.sort_desc == descending) return;
    g.sort = sort;
    g.sort_desc = descending;
    refresh_sorts();
}

void dj_ui_library_set_deck_status(uint8_t deck, const char *status)
{
    if (deck > DJ_DECKS) return;
    char title[16];
    if (deck == DJ_DECKS) snprintf(title, sizeof title, "DECK 1+2");
    else snprintf(title, sizeof title, "DECK %u", (unsigned)(deck + 1));
    if (!strcmp(lv_label_get_text(g.lib_deck), title) &&
        !strcmp(lv_label_get_text(g.lib_status), status ? status : "")) return;
    lv_color_t c = deck == DJ_DECKS ? C_INK : g.deck[deck].color;
    if (deck == DJ_DECKS) lv_label_set_text(g.lib_deck, "DECK 1+2");
    else lv_label_set_text_fmt(g.lib_deck, "DECK %u", (unsigned)(deck + 1));
    lv_label_set_text(g.lib_status, status ? status : "");
    lv_obj_set_style_text_color(g.lib_status, c, 0);
    lv_obj_set_style_bg_color(g.lib_strip, c, 0);
}

void dj_ui_library_set_status(const char *text, dj_tone_t tone)
{
    lv_label_set_text(g.lib_msg, text ? text : "");
    lv_obj_set_style_text_color(g.lib_msg, tone_col(tone), 0);
}

void dj_ui_library_set_progress(int16_t pct)
{
    if (pct < 0) {
        lv_obj_add_flag(g.lib_bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    lv_bar_set_value(g.lib_bar, pct > 100 ? 100 : pct, LV_ANIM_OFF);
    lv_obj_remove_flag(g.lib_bar, LV_OBJ_FLAG_HIDDEN);
}

void dj_ui_library_set_load_enabled(bool enabled)
{
    if (g.load_enabled == enabled) return;
    g.load_enabled = enabled;
    refresh_load_btns();
}

void dj_ui_library_set_load_locked(uint8_t deck, bool locked)
{
    if (deck >= DJ_DECKS || g.load_locked[deck] == locked) return;
    g.load_locked[deck] = locked;
    refresh_load_btns();
}

void dj_ui_library_set_source_label(const char *label)
{
    lv_label_set_text(lv_obj_get_child(g.lib_src_btn, 0), label ? label : "SOURCE");
}

void dj_ui_library_set_playlists_label(const char *label)
{
    lv_label_set_text(lv_obj_get_child(g.lib_pl_btn, 0), label ? label : "PLAYLISTS");
}

void dj_ui_set_field(dj_field_t field, const char *text, dj_tone_t tone)
{
    if (field >= DJ_F_COUNT || !g.f_lbl[field]) return;
    if (text && strcmp(lv_label_get_text(g.f_lbl[field]), text))
        lv_label_set_text(g.f_lbl[field], text);
    apply_tone(field, tone);
}

void dj_ui_set_brightness(uint8_t pct)
{
    lv_slider_set_value(g.bright, pct, LV_ANIM_OFF);
    lv_label_set_text_fmt(g.bright_lbl, "%u%%", (unsigned)lv_slider_get_value(g.bright));
}

void dj_ui_set_wireless(bool on)
{
    if (on) lv_obj_add_state(g.wl_sw, LV_STATE_CHECKED);
    else lv_obj_remove_state(g.wl_sw, LV_STATE_CHECKED);
    lv_label_set_text(g.wl_lbl, on ? "P4 REMOTE: ON" : "P4 REMOTE: OFF");
}

void dj_ui_set_link_enabled(bool on)
{
    if (on) lv_obj_add_state(g.link_sw, LV_STATE_CHECKED);
    else lv_obj_remove_state(g.link_sw, LV_STATE_CHECKED);
}

void dj_ui_set_link_peers(const dj_ui_link_peer_t *peers, uint8_t count)
{
    for (int r = 0; r < DJ_LINK_ROWS; r++) {
        if (r >= count || !peers) { lv_label_set_text(g.peer_lbl[r], ""); continue; }
        const dj_ui_link_peer_t *p = &peers[r];
        char bpm[12];
        if (p->bpm > 0.f) snprintf(bpm, sizeof bpm, "%.1f", (double)p->bpm);
        else snprintf(bpm, sizeof bpm, "---");
        lv_label_set_text_fmt(g.peer_lbl[r], "%s #%u   %s BPM%s%s%s", p->name ? p->name : "?", (unsigned)p->number, bpm,
                              p->master ? "   MASTER" : "", p->on_air ? "   ON AIR" : "", p->playing ? "   PLAY" : "");
        lv_obj_set_style_text_color(g.peer_lbl[r], p->master ? C_OK : C_INK, 0);
    }
}

void dj_ui_set_recording(bool on)
{
    g.recording = on;
    refresh_record();
}

void dj_ui_set_status(const char *text, dj_tone_t tone)
{
    bool empty = !text || !text[0];
    lv_label_set_text(g.status_lbl, empty ? "" : text);
    lv_obj_set_style_text_color(g.status_lbl, tone_col(tone), 0);
    lv_obj_set_style_border_color(g.status_box, empty || tone == DJ_TONE_NORMAL || tone == DJ_TONE_MUTED ? C_LINE : tone_col(tone), 0);
}

void dj_ui_set_link_master(const dj_ui_link_master_t *m)
{
    lv_color_t c = C_MUTED;
    int8_t lit = -1;
    if (!m) {
        lv_label_set_text(g.master_lbl, "LINK OFF");
    } else if (!m->player) {
        lv_label_set_text(g.master_lbl, "NO MASTER");
    } else {
        char bpm[12];
        if (m->bpm > 0.f) snprintf(bpm, sizeof bpm, "%.1f", (double)m->bpm);
        else snprintf(bpm, sizeof bpm, "---");
        lv_label_set_text_fmt(g.master_lbl, "M#%u%s %s", (unsigned)m->player, m->confirmed ? "" : "?", bpm);
        c = m->confirmed ? C_OK : C_WARN;
        if (m->beat >= 1 && m->beat <= 4) lit = (int8_t)(m->beat - 1);
    }
    lv_obj_set_style_text_color(g.master_lbl, c, 0);
    for (int k = 0; k < 4; k++)
        lv_obj_set_style_bg_color(g.master_seg[k], k == lit ? (k == 0 ? C_RED : c) : C_BEAT_OFF, 0);
}

void dj_ui_set_screensaver_font(const lv_font_t *font)
{
    if (!font) font = F28;
    for (int k = 0; k < 9; k++) lv_obj_set_style_text_font(g.saver_ch[k], font, 0);
}

void dj_ui_set_screensaver(bool on)
{
    if (on == dj_ui_screensaver_active()) return;
    if (on) {
        g.saver_step = 0;
        saver_tick(NULL);
        lv_obj_remove_flag(g.saver, LV_OBJ_FLAG_HIDDEN);
        lv_timer_resume(g.saver_timer);
    } else {
        lv_timer_pause(g.saver_timer);
        lv_obj_add_flag(g.saver, LV_OBJ_FLAG_HIDDEN);
    }
}

bool dj_ui_screensaver_active(void)
{
    return g.saver && !lv_obj_has_flag(g.saver, LV_OBJ_FLAG_HIDDEN);
}

void dj_ui_set_blackout(bool on)
{
    if (on) lv_obj_remove_flag(g.blackout, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(g.blackout, LV_OBJ_FLAG_HIDDEN);
}
