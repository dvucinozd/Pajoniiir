/* Cover JPEG probe: accepted baseline and rejected unsupported sampling or
 * progressive frame headers. An optional argument probes a file. */
#include "ui_artwork_jpeg.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int s_failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); s_failures++; } } while (0)

static const uint8_t APP0[] = { 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01, 0x02,
                                0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00 };
static const uint8_t COM[] = { 0xFF, 0xFE, 0x00, 0x05, 'L', 'a', 'v' };
static const uint8_t SOS[] = { 0xFF, 0xDA, 0x00, 0x0C, 0x03, 0x01, 0x00, 0x02, 0x11, 0x03, 0x11,
                               0x00, 0x3F, 0x00 };

static size_t jpeg(uint8_t *out, uint8_t sof, const uint8_t *comp, uint8_t ncomp, bool with_sos)
{
    size_t n = 0;
    out[n++] = 0xFF; out[n++] = 0xD8;
    memcpy(&out[n], APP0, sizeof APP0); n += sizeof APP0;
    memcpy(&out[n], COM, sizeof COM); n += sizeof COM;
    out[n++] = 0xFF; out[n++] = sof;
    uint16_t seg = (uint16_t)(8 + 3 * ncomp);
    out[n++] = (uint8_t)(seg >> 8); out[n++] = (uint8_t)seg;
    out[n++] = 8;
    out[n++] = 0x00; out[n++] = 0xF0;     /* 240 high */
    out[n++] = 0x00; out[n++] = 0xF0;     /* 240 wide */
    out[n++] = ncomp;
    memcpy(&out[n], comp, 3u * ncomp); n += 3u * ncomp;
    if (with_sos) { memcpy(&out[n], SOS, sizeof SOS); n += sizeof SOS; }
    return n;
}

static void test_vynull_covers(void)
{
    uint8_t b[128];
    /* vynull, PNG cover: SOF0 0800f000f003 011200 021200 031200 */
    static const uint8_t png444[] = { 1, 0x12, 0, 2, 0x12, 0, 3, 0x12, 0 };
    ui_artwork_jpeg_info_t i = ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, png444, 3, true));
    CHECK(i.verdict == UI_ARTWORK_JPEG_SAMPLING);
    CHECK(i.sof == 0xC0 && i.components == 3 && i.width == 240 && i.height == 240);
    CHECK(i.sampling[0] == 0x12 && i.sampling[1] == 0x12 && i.sampling[2] == 0x12);

    /* vynull, JPEG cover: 4:2:0 */
    static const uint8_t jpg420[] = { 1, 0x22, 0, 2, 0x11, 1, 3, 0x11, 1 };
    i = ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, jpg420, 3, true));
    CHECK(i.verdict == UI_ARTWORK_JPEG_OK && i.sampling[0] == 0x22);
}

static void test_tjpgd_rules(void)
{
    uint8_t b[128];
    static const uint8_t y11[] = { 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1 };
    static const uint8_t y21[] = { 1, 0x21, 0, 2, 0x11, 1, 3, 0x11, 1 };
    static const uint8_t y12[] = { 1, 0x12, 0, 2, 0x11, 1, 3, 0x11, 1 };   /* 4:4:0 */
    static const uint8_t c21[] = { 1, 0x22, 0, 2, 0x21, 1, 3, 0x11, 1 };
    static const uint8_t grey[] = { 1, 0x22, 0 };
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, y11, 3, true)).verdict == UI_ARTWORK_JPEG_OK);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, y21, 3, true)).verdict == UI_ARTWORK_JPEG_OK);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, y12, 3, true)).verdict == UI_ARTWORK_JPEG_SAMPLING);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, c21, 3, true)).verdict == UI_ARTWORK_JPEG_SAMPLING);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC0, grey, 1, true)).verdict == UI_ARTWORK_JPEG_OK);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC2, y11, 3, true)).verdict == UI_ARTWORK_JPEG_PROGRESSIVE);
    CHECK(ui_artwork_jpeg_probe(b, jpeg(b, 0xC1, y11, 3, true)).verdict == UI_ARTWORK_JPEG_PROGRESSIVE);
}

static void test_malformed(void)
{
    uint8_t b[128];
    static const uint8_t y11[] = { 1, 0x11, 0, 2, 0x11, 1, 3, 0x11, 1 };
    size_t n = jpeg(b, 0xC0, y11, 3, true);
    CHECK(ui_artwork_jpeg_probe(NULL, n).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
    CHECK(ui_artwork_jpeg_probe(b, 3).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
    /* Truncated inside the frame header. */
    CHECK(ui_artwork_jpeg_probe(b, n - sizeof SOS - 2).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
    /* A frame header announcing more components than it holds. */
    b[2 + sizeof APP0 + sizeof COM + 9] = 4;
    CHECK(ui_artwork_jpeg_probe(b, n).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
    /* PNG bytes. */
    static const uint8_t png[] = { 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A };
    CHECK(ui_artwork_jpeg_probe(png, sizeof png).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
    /* Scan before any frame header. */
    static const uint8_t early[] = { 0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x00, 0x00 };
    CHECK(ui_artwork_jpeg_probe(early, sizeof early).verdict == UI_ARTWORK_JPEG_NOT_JPEG);
}

static int probe_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return 1; }
    static uint8_t buf[1u << 20];
    size_t n = fread(buf, 1, sizeof buf, f);
    fclose(f);
    ui_artwork_jpeg_info_t i = ui_artwork_jpeg_probe(buf, n);
    printf("%s: %ux%u SOF%02X %u comp sampling %02X/%02X/%02X: %s\n", path,
           (unsigned)i.width, (unsigned)i.height, (unsigned)i.sof, (unsigned)i.components,
           (unsigned)i.sampling[0], (unsigned)i.sampling[1], (unsigned)i.sampling[2],
           ui_artwork_jpeg_verdict_name(i.verdict));
    return 0;
}

int main(int argc, char **argv)
{
    test_vynull_covers();
    test_tjpgd_rules();
    test_malformed();
    for (int a = 1; a < argc; a++) {
        if (probe_file(argv[a])) s_failures++;
    }
    if (s_failures) {
        fprintf(stderr, "%d ui_artwork_jpeg check(s) failed\n", s_failures);
        return 1;
    }
    printf("all ui_artwork_jpeg tests passed\n");
    return 0;
}
