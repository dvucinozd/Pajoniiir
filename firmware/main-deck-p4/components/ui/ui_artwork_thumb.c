#include "ui_artwork_thumb.h"

#include "src/libs/tjpgd/tjpgd.h"

#include <string.h>

#if !LV_USE_TJPGD
#error "ui_artwork_thumb needs TJpgDec: set CONFIG_LV_USE_TJPGD (LV_USE_TJPGD in lv_conf.h)"
#endif

static size_t thumb_input(JDEC *jd, uint8_t *buf, size_t n)
{
    ui_artwork_thumb_work_t *w = (ui_artwork_thumb_work_t *)jd->device;
    size_t left = w->len - w->pos;
    if (n > left) n = left;
    if (buf) memcpy(buf, w->src + w->pos, n);   /* buf NULL = skip */
    w->pos += n;
    return n;
}

static void accumulate(uint32_t *a, const uint8_t *bgr)
{
    a[0] += bgr[0];
    a[1] += bgr[1];
    a[2] += bgr[2];
    a[3]++;
}

/* The LVGL copy of TJpgDec writes B, G, R bytes (RGB888 in LVGL's order). */
static int thumb_output(JDEC *jd, void *bitmap, JRECT *rect)
{
    ui_artwork_thumb_work_t *w = (ui_artwork_thumb_work_t *)jd->device;
    const uint8_t *px = (const uint8_t *)bitmap;
    for (int y = rect->top; y <= rect->bottom; y++) {
        uint32_t (*row)[4] = &w->row_acc[w->row_dy[y] * UI_ARTWORK_ROW_PX];
        uint32_t (*deck)[4] = &w->deck_acc[w->deck_dy[y] * UI_ARTWORK_DECK_PX];
        for (int x = rect->left; x <= rect->right; x++, px += 3) {
            accumulate(row[w->row_dx[x]], px);
            accumulate(deck[w->deck_dx[x]], px);
        }
    }
    return 1;
}

static void cell_map(uint8_t *map, int side, int src)
{
    for (int i = 0; i < src; i++) map[i] = (uint8_t)(i * side / src);
}

/* Averages to RGB565. A source smaller than the thumbnail leaves cells
 * without samples; they repeat their left (else upper) neighbour. */
static void resolve(uint32_t (*acc)[4], int side, uint16_t *dst)
{
    for (int y = 0; y < side; y++) {
        for (int x = 0; x < side; x++) {
            int i = y * side + x;
            const uint32_t *a = acc[i];
            if (a[3] == 0) {
                dst[i] = x > 0 ? dst[i - 1] : y > 0 ? dst[i - side] : 0;
                continue;
            }
            uint32_t b = (a[0] + a[3] / 2) / a[3];
            uint32_t g = (a[1] + a[3] / 2) / a[3];
            uint32_t r = (a[2] + a[3] / 2) / a[3];
            dst[i] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
        }
    }
}

bool ui_artwork_thumb_decode(const uint8_t *jpg, size_t len,
                             ui_artwork_thumb_work_t *work, ui_artwork_thumb_t *out)
{
    if (!jpg || len < 4 || !work || !out) return false;
    work->src = jpg;
    work->len = len;
    work->pos = 0;

    JDEC jd;
    if (jd_prepare(&jd, thumb_input, work->pool, sizeof work->pool, work) != JDR_OK) return false;
    if (jd.width == 0 || jd.height == 0 ||
        jd.width > UI_ARTWORK_SRC_MAX || jd.height > UI_ARTWORK_SRC_MAX) return false;
    work->src_w = jd.width;
    work->src_h = jd.height;
    cell_map(work->row_dx, UI_ARTWORK_ROW_PX, jd.width);
    cell_map(work->row_dy, UI_ARTWORK_ROW_PX, jd.height);
    cell_map(work->deck_dx, UI_ARTWORK_DECK_PX, jd.width);
    cell_map(work->deck_dy, UI_ARTWORK_DECK_PX, jd.height);
    memset(work->row_acc, 0, sizeof work->row_acc);
    memset(work->deck_acc, 0, sizeof work->deck_acc);
    if (jd_decomp(&jd, thumb_output, 0) != JDR_OK) return false;

    resolve(work->row_acc, UI_ARTWORK_ROW_PX, out->row);
    resolve(work->deck_acc, UI_ARTWORK_DECK_PX, out->deck);
    return true;
}
