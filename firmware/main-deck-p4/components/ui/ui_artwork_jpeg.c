#include "ui_artwork_jpeg.h"

#include <string.h>

/* The checks of TJpgDec's jd_prepare, in its order. */
static ui_artwork_jpeg_verdict_t judge(const ui_artwork_jpeg_info_t *info)
{
    if (info->sof != 0xC0) return UI_ARTWORK_JPEG_PROGRESSIVE;
    if (info->components != 1 && info->components != 3) return UI_ARTWORK_JPEG_SAMPLING;
    uint8_t y = info->sampling[0];
    if (info->components == 3) {
        if (y != 0x11 && y != 0x21 && y != 0x22) return UI_ARTWORK_JPEG_SAMPLING;
        if (info->sampling[1] != 0x11 || info->sampling[2] != 0x11) return UI_ARTWORK_JPEG_SAMPLING;
    }
    return UI_ARTWORK_JPEG_OK;
}

ui_artwork_jpeg_info_t ui_artwork_jpeg_probe(const uint8_t *jpg, size_t len)
{
    ui_artwork_jpeg_info_t info;
    memset(&info, 0, sizeof info);
    info.verdict = UI_ARTWORK_JPEG_NOT_JPEG;
    if (!jpg || len < 4 || jpg[0] != 0xFF || jpg[1] != 0xD8) return info;

    size_t pos = 2;
    while (pos + 4 <= len) {
        if (jpg[pos] != 0xFF) return info;
        uint8_t marker = jpg[pos + 1];
        if (marker == 0xFF) { pos++; continue; }            /* fill byte */
        if (marker == 0xDA || marker == 0xD9) return info;  /* scan before any frame */
        size_t seg = ((size_t)jpg[pos + 2] << 8) | jpg[pos + 3];
        if (seg < 2 || pos + 2 + seg > len) return info;
        /* SOF0..SOF15, less DHT (C4), JPG (C8) and DAC (CC). */
        if (marker >= 0xC0 && marker <= 0xCF &&
            marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
            const uint8_t *s = &jpg[pos + 4];
            if (seg < 8) return info;
            info.sof = marker;
            info.height = (uint16_t)((s[1] << 8) | s[2]);
            info.width = (uint16_t)((s[3] << 8) | s[4]);
            info.components = s[5];
            if (8u + 3u * info.components > seg) return info;   /* id, HV, table each */
            for (uint8_t c = 0; c < info.components && c < 3; c++) {
                info.sampling[c] = s[6 + 3 * c + 1];
            }
            info.verdict = judge(&info);
            return info;
        }
        pos += 2 + seg;
    }
    return info;
}

const char *ui_artwork_jpeg_verdict_name(ui_artwork_jpeg_verdict_t verdict)
{
    switch (verdict) {
    case UI_ARTWORK_JPEG_OK: return "ok";
    case UI_ARTWORK_JPEG_NOT_JPEG: return "not a JPEG";
    case UI_ARTWORK_JPEG_PROGRESSIVE: return "not baseline";
    case UI_ARTWORK_JPEG_SAMPLING: return "chroma sampling";
    }
    return "?";
}
