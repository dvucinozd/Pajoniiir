/* Bounded JPEG decode into RGB565 Library/deck thumbnail sizes. Pure and
 * reentrant per work area; only the Library row is presented in this phase. */
#ifndef UI_ARTWORK_THUMB_H
#define UI_ARTWORK_THUMB_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define UI_ARTWORK_ROW_PX     40         /* Library row thumbnail */
#define UI_ARTWORK_DECK_PX    34         /* deck header, next to the title */
#define UI_ARTWORK_SRC_MAX    2048       /* larger JPEG dimensions are refused */
#define UI_ARTWORK_POOL_BYTES 8192       /* TJpgDec session pool (needs ~5.5 KiB) */

typedef struct {
    uint16_t row[UI_ARTWORK_ROW_PX * UI_ARTWORK_ROW_PX];     /* LV_COLOR_FORMAT_RGB565 */
    uint16_t deck[UI_ARTWORK_DECK_PX * UI_ARTWORK_DECK_PX];
} ui_artwork_thumb_t;

/* Decoder scratch, ~58 KiB: allocate once (PSRAM), reuse for every decode. */
typedef struct {
    uint8_t pool[UI_ARTWORK_POOL_BYTES];
    uint32_t row_acc[UI_ARTWORK_ROW_PX * UI_ARTWORK_ROW_PX][4];    /* B, G, R, count */
    uint32_t deck_acc[UI_ARTWORK_DECK_PX * UI_ARTWORK_DECK_PX][4];
    /* source column/row -> thumbnail cell, set per image: no division per pixel */
    uint8_t row_dx[UI_ARTWORK_SRC_MAX], row_dy[UI_ARTWORK_SRC_MAX];
    uint8_t deck_dx[UI_ARTWORK_SRC_MAX], deck_dy[UI_ARTWORK_SRC_MAX];
    const uint8_t *src;
    size_t len, pos;
    uint16_t src_w, src_h;
} ui_artwork_thumb_work_t;

/* Baseline JPEG (TJpgDec: no progressive, no arithmetic coding) scaled to
 * both thumbnail sizes by area averaging. false leaves *out unspecified. */
bool ui_artwork_thumb_decode(const uint8_t *jpg, size_t len,
                             ui_artwork_thumb_work_t *work, ui_artwork_thumb_t *out);

#ifdef __cplusplus
}
#endif

#endif
