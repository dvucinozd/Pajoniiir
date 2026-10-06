#ifndef UI_ARTWORK_PLACEHOLDER_H
#define UI_ARTWORK_PLACEHOLDER_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/* User-supplied Pajoniiir favicon, pre-rendered in flash. These descriptors
 * require no filesystem access, runtime decode, mutable copy or cache slot. */
extern const lv_image_dsc_t ui_artwork_placeholder_row;
extern const lv_image_dsc_t ui_artwork_placeholder_deck;

#ifdef __cplusplus
}
#endif

#endif
