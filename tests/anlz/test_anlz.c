/*
 * test_anlz.c  —  PC test harness for the Rekordbox ANLZ parser
 *
 * Builds on Linux/macOS/Windows with:
 *   make
 * or manually:
 *   gcc -DANLZ_STANDALONE_TEST -I../../firmware/main-deck-p4/components/library/include \
 *       ../../firmware/main-deck-p4/components/library/rekordbox_anlz.c \
 *       test_anlz.c -o test_anlz
 *
 * Usage:
 *   ./test_anlz <path_to_ANLZ0000.DAT> [path_to_ANLZ0000.EXT]
 *   ./test_anlz                        (runs built-in synthetic tests)
 */

/* ANLZ_STANDALONE_TEST is passed via -D on the compiler command line (see Makefile) */
#include "rekordbox_anlz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ── Test helpers ─────────────────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)  do { g_tests_run++; printf("  %-50s", name); } while (0)
#define PASS()      do { g_tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)   do { g_tests_failed++; printf("FAIL  (%s)\n", msg); } while (0)
#define CHECK(cond, msg) do { if (cond) PASS(); else FAIL(msg); } while (0)

/* ── Big-endian write helpers (for building synthetic test files) ──────────── */

static void w_be16(FILE *fp, uint16_t v)
{
    fputc((v >> 8) & 0xFF, fp);
    fputc( v       & 0xFF, fp);
}

static void w_be32(FILE *fp, uint32_t v)
{
    fputc((v >> 24) & 0xFF, fp);
    fputc((v >> 16) & 0xFF, fp);
    fputc((v >>  8) & 0xFF, fp);
    fputc( v        & 0xFF, fp);
}

static void w_tag(FILE *fp, uint32_t tag)  { w_be32(fp, tag); }
static void w_str(FILE *fp, const char *s) { fputs(s, fp); }

/* ── Build a minimal synthetic ANLZ0000.DAT for unit testing ──────────────── *
 *
 * Contains:
 *   PMAI  — file header (skipped by parser, present for realism)
 *   PPTH  — UTF-16 BE path "/Music/TestArtist/test.mp3"
 *   PVBR  — 4 × uint32 VBR offsets [100, 200, 300, 400]
 *   PQTZ  — 2 beat entries:
 *              {phase=0, bpm_x100=12800, time_ms=0}
 *              {phase=0, bpm_x100=12800, time_ms=469}
 *   PWAV  — 400 bytes of synthetic waveform (sawtooth)
 *   PCOB  — 2 PCPT entries (hot cue 0 at 1000ms, loop 1 at 2000–4000ms)
 */
static const char *SYNTH_DAT = "test_synth.dat";
static const char *SYNTH_EXT = "test_synth.ext";
static const char *SYNTH_UNICODE_DAT = "test_unicode_ppth.dat";

static void build_synthetic_dat(void)
{
    FILE *fp = fopen(SYNTH_DAT, "wb");
    if (!fp) { perror("fopen synth.dat"); exit(1); }

    /* PMAI header (28 bytes total):
     *  tag(4) + header_size(4=28) + segment_size(4=28) + 16B zeros */
    w_tag(fp, ANLZ_TAG_PMAI);
    w_be32(fp, 28);   /* header_size */
    w_be32(fp, 28);   /* segment_size — no extra data */
    for (int i = 0; i < 16; i++) fputc(0, fp);

    /* PPTH — path "/Music/TestArtist/test.mp3" in UTF-16 BE
     * The path as UTF-16 BE with null terminator:
     *   each char is 0x00 <ascii>
     * path string: /Music/TestArtist/test.mp3  (28 chars) + NUL = 29 × 2 = 58 bytes
     * header_size = 20 (tag+hdr+seg+4spare+4path_len = 4+4+4+4+4 = 20)
     * segment_size = header_size + 58 = 78 */
    const char *path_ascii = "/Music/TestArtist/test.mp3";
    size_t path_chars = strlen(path_ascii) + 1; /* include NUL */
    uint32_t path_bytes = (uint32_t)(path_chars * 2);
    uint32_t ppth_header = 20;
    uint32_t ppth_segment = ppth_header + path_bytes;

    w_tag(fp, ANLZ_TAG_PPTH);
    w_be32(fp, ppth_header);
    w_be32(fp, ppth_segment);
    w_be32(fp, 0);            /* 4 spare bytes in header */
    w_be32(fp, path_bytes);   /* path_length field */
    /* UTF-16 BE path data */
    for (size_t i = 0; i < path_chars; i++) {
        fputc(0x00, fp);
        fputc((unsigned char)path_ascii[i], fp);
    }

    /* PVBR — 4 offsets: 100, 200, 300, 400
     * header_size = 12 (no extra header fields)
     * segment_size = 12 + 4×4 = 28 */
    w_tag(fp, ANLZ_TAG_PVBR);
    w_be32(fp, 12);  /* header_size */
    w_be32(fp, 28);  /* segment_size */
    w_be32(fp, 100);
    w_be32(fp, 200);
    w_be32(fp, 300);
    w_be32(fp, 400);

    /* PQTZ — 2 beat entries
     * header_size = 12, segment_size = 12 + 2×8 = 28 */
    w_tag(fp, ANLZ_TAG_PQTZ);
    w_be32(fp, 12);  /* header_size */
    w_be32(fp, 28);  /* segment_size */
    /* beat 0: phase=0, bpm_x100=12800, time_ms=0 */
    w_be16(fp, 0);
    w_be16(fp, 12800);
    w_be32(fp, 0);
    /* beat 1: phase=0, bpm_x100=12800, time_ms=469 (one beat at 128 BPM) */
    w_be16(fp, 0);
    w_be16(fp, 12800);
    w_be32(fp, 469);

    /* PWAV — 400 bytes sawtooth waveform
     * header_size = 12, segment_size = 12 + 400 = 412 */
    w_tag(fp, ANLZ_TAG_PWAV);
    w_be32(fp, 12);   /* header_size */
    w_be32(fp, 412);  /* segment_size */
    for (int i = 0; i < 400; i++) fputc((unsigned char)(i & 0x1F), fp); /* sawtooth heights */

    /* PCOB — 2 PCPT entries (each 56 bytes)
     * header_size = 12, segment_size = 12 + 2×56 = 124 */
    w_tag(fp, ANLZ_TAG_PCOB);
    w_be32(fp, 12);   /* header_size */
    w_be32(fp, 124);  /* segment_size */

    /* PCPT 0: single point, index=0, start_ms=1000 */
    {
        uint8_t pcpt[56]; memset(pcpt, 0, 56);
        pcpt[0] = 0x01; /* type = single */
        pcpt[1] = 0x00; /* index = 0     */
        pcpt[4] = 0x00; pcpt[5] = 0x00; pcpt[6] = 0x03; pcpt[7] = 0xE8; /* start=1000 */
        fwrite(pcpt, 1, 56, fp);
    }
    /* PCPT 1: loop, index=1, start_ms=2000, end_ms=4000 */
    {
        uint8_t pcpt[56]; memset(pcpt, 0, 56);
        pcpt[0] = 0x02; /* type = loop   */
        pcpt[1] = 0x01; /* index = 1     */
        pcpt[4] = 0x00; pcpt[5] = 0x00; pcpt[6] = 0x07; pcpt[7] = 0xD0; /* start=2000 */
        pcpt[8] = 0x00; pcpt[9] = 0x00; pcpt[10]= 0x0F; pcpt[11]= 0xA0; /* end=4000   */
        fwrite(pcpt, 1, 56, fp);
    }

    fclose(fp);
}

static void build_synthetic_ext(void)
{
    FILE *fp = fopen(SYNTH_EXT, "wb");
    if (!fp) { perror("fopen synth.ext"); exit(1); }

    /* PWV3 — 800 bytes high-res waveform
     * header_size = 12, segment_size = 12 + 800 = 812 */
    w_tag(fp, ANLZ_TAG_PWV3);
    w_be32(fp, 12);   /* header_size */
    w_be32(fp, 812);  /* segment_size */
    for (int i = 0; i < 800; i++) fputc((unsigned char)((i * 3) & 0x1F), fp);

    fclose(fp);
    (void)w_str; /* suppress unused warning */
}

static void build_unicode_ppth_dat(void)
{
    FILE *fp = fopen(SYNTH_UNICODE_DAT, "wb");
    if (!fp) { perror("fopen unicode.dat"); exit(1); }

    const uint16_t path_units[] = {
        '/', 'C', 'o', 'n', 't', 'e', 'n', 't', 's', '/',
        0x5468, 0x9632, 0x7FA9, 0x548C,
        '/', 'U', 'n', 'k', 'n', 'o', 'w', 'n', 'A', 'l', 'b', 'u', 'm', '/',
        'C', 'a', 'r', 'i', 'b', 'b', 'e', 'a', 'n', ' ', 'B', 'l', 'u', 'e', '.', 'm', 'p', '3',
        0
    };
    uint32_t path_bytes = (uint32_t)(sizeof(path_units));
    uint32_t ppth_header = 20;
    uint32_t ppth_segment = ppth_header + path_bytes;

    w_tag(fp, ANLZ_TAG_PPTH);
    w_be32(fp, ppth_header);
    w_be32(fp, ppth_segment);
    w_be32(fp, 0);
    w_be32(fp, path_bytes);
    for (size_t i = 0; i < sizeof(path_units) / sizeof(path_units[0]); i++) {
        w_be16(fp, path_units[i]);
    }

    fclose(fp);
}

/* ── Unit tests (synthetic files) ─────────────────────────────────────────── */

static void run_unit_tests(void)
{
    printf("\n=== Building synthetic test files ===\n");
    build_synthetic_dat();
    build_synthetic_ext();
    build_unicode_ppth_dat();
    printf("  Created: %s\n", SYNTH_DAT);
    printf("  Created: %s\n", SYNTH_EXT);
    printf("  Created: %s\n", SYNTH_UNICODE_DAT);

    printf("\n=== anlz_parse_dat() ===\n");
    anlz_metadata_t meta;
    esp_err_t rc = anlz_parse_dat(SYNTH_DAT, &meta);

    TEST("parse_dat returns ESP_OK");
    CHECK(rc == ESP_OK, "unexpected error");

    TEST("audio_path correct");
    CHECK(strcmp(meta.audio_path, "/Music/TestArtist/test.mp3") == 0,
          meta.audio_path);

    TEST("BPM = 128");
    CHECK(meta.bpm == 128, "unexpected BPM");

    TEST("beat_count = 2");
    CHECK(meta.beat_count == 2, "wrong beat count");

    TEST("beat[0].time_ms = 0");
    CHECK(meta.beats && meta.beats[0].time_ms == 0, "wrong time");

    TEST("beat[1].time_ms = 469");
    CHECK(meta.beats && meta.beats[1].time_ms == 469, "wrong time");

    TEST("has_vbr = true");
    CHECK(meta.has_vbr == true, "VBR not parsed");

    TEST("vbr[0] = 100");
    CHECK(meta.vbr[0] == 100, "wrong offset");

    TEST("vbr[3] = 400");
    CHECK(meta.vbr[3] == 400, "wrong offset");

    TEST("has_waveform_low = true");
    CHECK(meta.has_waveform_low == true, "waveform not parsed");

    TEST("waveform_low[0] = 0 (sawtooth start)");
    CHECK(meta.waveform_low[0] == 0, "wrong value");

    TEST("waveform_low[31] = 31 (sawtooth peak)");
    CHECK(meta.waveform_low[31] == 31, "wrong value");

    TEST("cue_count = 2");
    CHECK(meta.cue_count == 2, "wrong cue count");

    TEST("cue[0].type = SINGLE");
    CHECK(meta.cues[0].type == ANLZ_CUE_SINGLE, "wrong type");

    TEST("cue[0].index = 0");
    CHECK(meta.cues[0].index == 0, "wrong index");

    TEST("cue[0].start_ms = 1000");
    CHECK(meta.cues[0].start_ms == 1000, "wrong start");

    TEST("cue[1].type = LOOP");
    CHECK(meta.cues[1].type == ANLZ_CUE_LOOP, "wrong type");

    TEST("cue[1].start_ms = 2000");
    CHECK(meta.cues[1].start_ms == 2000, "wrong start");

    TEST("cue[1].end_ms = 4000");
    CHECK(meta.cues[1].end_ms == 4000, "wrong end");

    TEST("waveform_high = NULL before parse_ext");
    CHECK(meta.waveform_high == NULL, "should be NULL");

    printf("\n=== anlz_parse_ext() ===\n");
    rc = anlz_parse_ext(SYNTH_EXT, &meta);

    TEST("parse_ext returns ESP_OK");
    CHECK(rc == ESP_OK, "unexpected error");

    TEST("waveform_high != NULL after parse_ext");
    CHECK(meta.waveform_high != NULL, "still NULL");

    TEST("waveform_high_len = 800");
    CHECK(meta.waveform_high_len == 800, "wrong length");

    TEST("waveform_high[0] = 0");
    CHECK(meta.waveform_high && meta.waveform_high[0] == 0, "wrong value");

    printf("\n=== anlz_clone() ===\n");
    anlz_metadata_t cloned;
    rc = anlz_clone(&meta, &cloned);

    TEST("clone returns ESP_OK");
    CHECK(rc == ESP_OK, "unexpected clone error");

    TEST("clone owns an independent beat array");
    CHECK(cloned.beats && cloned.beats != meta.beats &&
          cloned.beat_count == meta.beat_count &&
          cloned.beats[1].time_ms == meta.beats[1].time_ms,
          "beat array not deeply copied");

    TEST("clone owns an independent high waveform");
    CHECK(cloned.waveform_high && cloned.waveform_high != meta.waveform_high &&
          cloned.waveform_high_len == meta.waveform_high_len &&
          memcmp(cloned.waveform_high, meta.waveform_high, meta.waveform_high_len) == 0,
          "high waveform not deeply copied");

    anlz_free(&cloned);

    printf("\n=== anlz_free() ===\n");
    anlz_free(&meta);

    printf("\n=== anlz_parse_dat() Unicode PPTH ===\n");
    memset(&meta, 0, sizeof(meta));
    rc = anlz_parse_dat(SYNTH_UNICODE_DAT, &meta);

    TEST("parse_dat Unicode PPTH returns ESP_OK");
    CHECK(rc == ESP_OK, "unexpected error");

    TEST("Unicode PPTH path preserved as UTF-8");
    CHECK(strcmp(meta.audio_path,
                 "/Contents/\xE5\x91\xA8\xE9\x98\xB2\xE7\xBE\xA9\xE5\x92\x8C/UnknownAlbum/Caribbean Blue.mp3") == 0,
          meta.audio_path);

    anlz_free(&meta);

    TEST("beats = NULL after free");
    CHECK(meta.beats == NULL, "not freed");

    TEST("waveform_high = NULL after free");
    CHECK(meta.waveform_high == NULL, "not freed");

    /* Double-free safety */
    TEST("double anlz_free() safe");
    anlz_free(&meta);
    PASS();

    printf("\n=== Error handling ===\n");
    anlz_metadata_t bad;

    TEST("parse_dat NULL path → INVALID_ARG");
    rc = anlz_parse_dat(NULL, &bad);
    CHECK(rc == ESP_ERR_INVALID_ARG, "wrong error");

    TEST("parse_dat NULL out → INVALID_ARG");
    rc = anlz_parse_dat("x.dat", NULL);
    CHECK(rc == ESP_ERR_INVALID_ARG, "wrong error");

    TEST("parse_dat missing file → NOT_FOUND");
    rc = anlz_parse_dat("/nonexistent/ANLZ0000.DAT", &bad);
    CHECK(rc == ESP_ERR_NOT_FOUND, "wrong error");

    TEST("parse_ext NULL meta → INVALID_ARG");
    rc = anlz_parse_ext("x.ext", NULL);
    CHECK(rc == ESP_ERR_INVALID_ARG, "wrong error");

    TEST("parse_ext missing file → NOT_FOUND");
    rc = anlz_parse_ext("/nonexistent/ANLZ0000.EXT", &meta);
    CHECK(rc == ESP_ERR_NOT_FOUND, "wrong error");
}

/* ── Real-file mode ───────────────────────────────────────────────────────── */

static void parse_real_file(const char *dat_path, const char *ext_path)
{
    printf("\n=== Parsing real file: %s ===\n", dat_path);
    anlz_metadata_t meta;
    esp_err_t rc = anlz_parse_dat(dat_path, &meta);
    if (rc != ESP_OK) {
        printf("ERROR: anlz_parse_dat returned %d\n", rc);
        return;
    }

    printf("  Audio path   : %s\n", meta.audio_path);
    printf("  BPM          : %u\n", meta.bpm);
    printf("  Beat entries : %u\n", meta.beat_count);
    printf("  Cue count    : %u\n", meta.cue_count);
    printf("  Has VBR      : %s\n", meta.has_vbr ? "yes" : "no");
    printf("  Has waveform : %s\n", meta.has_waveform_low ? "yes" : "no");

    for (int i = 0; i < meta.cue_count; i++) {
        anlz_cue_t *c = &meta.cues[i];
        if (c->type == ANLZ_CUE_LOOP) {
            printf("  Cue[%d] LOOP  idx=%u start=%ums end=%ums\n",
                   i, c->index, c->start_ms, c->end_ms);
        } else {
            printf("  Cue[%d] POINT idx=%u pos=%ums\n",
                   i, c->index, c->start_ms);
        }
    }

    if (meta.beat_count > 0) {
        printf("  First beat   : time=%ums  bpm_x100=%u\n",
               meta.beats[0].time_ms, meta.beats[0].bpm_x100);
        printf("  Last beat    : time=%ums\n",
               meta.beats[meta.beat_count - 1].time_ms);
    }

    if (ext_path) {
        printf("\n=== Parsing EXT: %s ===\n", ext_path);
        rc = anlz_parse_ext(ext_path, &meta);
        if (rc == ESP_OK) {
            printf("  Hi-res waveform: %u bytes\n", meta.waveform_high_len);
        } else {
            printf("  EXT parse error: %d\n", rc);
        }
    }

    anlz_free(&meta);
    printf("\nDone.\n");
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    printf("Pajoniiir ANLZ Parser Test\n");
    printf("============================\n");

    if (argc >= 2) {
        /* Real file mode */
        const char *ext = (argc >= 3) ? argv[2] : NULL;
        parse_real_file(argv[1], ext);
        return 0;
    }

    /* Synthetic unit test mode */
    run_unit_tests();

    printf("\n============================\n");
    printf("TESTS_RUN=%d\n", g_tests_run);
    printf("Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) {
        printf("  (%d FAILED)\n", g_tests_failed);
    } else {
        printf("  — ALL PASSED\n");
    }

    /* Clean up synthetic files */
    remove(SYNTH_DAT);
    remove(SYNTH_EXT);

    return (g_tests_failed > 0) ? 1 : 0;
}
