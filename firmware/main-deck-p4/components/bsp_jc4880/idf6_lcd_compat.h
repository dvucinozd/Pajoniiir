#pragma once

#include "esp_idf_version.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(6, 0, 0)
/*
 * Include the IDF 6 LCD declarations before defining the source-level aliases,
 * so the aliases only translate the two ESP-IDF 5.5 designated initializers in
 * bsp_jc4880.c and cannot rewrite identifiers inside the framework headers.
 */
#include "esp_lcd_mipi_dsi.h"

#define pixel_format in_color_format
#define LCD_COLOR_PIXEL_FORMAT_RGB565 LCD_COLOR_FMT_RGB565

/* IDF 6 removed flags.use_dma2d. The old initializer set it to false, so map
 * that false assignment to the remaining harmless false-by-default flag. */
#define use_dma2d disable_lp
#endif
