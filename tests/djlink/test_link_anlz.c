/* Host tests for dj_link_anlz (v300): dbserver analysis blobs in vynull's
 * format become ANLZ DAT/EXT files that the library's own parser
 * (rekordbox_anlz.c, built standalone) reads back. */
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dj_link_anlz.h"
#include "rekordbox_anlz.h"

static int s_failures;
static int pcob_count(const char *path)
{
    FILE *f=fopen(path,"rb");if(!f)return -1;
    int count=0;uint8_t head[12];
    if(fseek(f,28,SEEK_SET)) {fclose(f);return -1;}
    while(fread(head,1,12,f)==12) {
        uint32_t len=((uint32_t)head[8]<<24)|((uint32_t)head[9]<<16)|((uint32_t)head[10]<<8)|head[11];
        if(len<12)break;
        if(!memcmp(head,"PCOB",4))count++;
        if(fseek(f,(long)len-12,SEEK_CUR))break;
    }
    fclose(f);return count;
}

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                    #cond);                                                  \
            s_failures++;                                                    \
        }                                                                    \
    } while (0)

static bool file_write(void *ctx, const void *data, size_t len)
{
    return fwrite(data, 1u, len, (FILE *)ctx) == len;
}

typedef struct {
    size_t limit;
    size_t used;
} short_sink_t;

static bool short_write(void *ctx, const void *data, size_t len)
{
    short_sink_t *s = ctx;
    (void)data;
    if (s->used + len > s->limit) {
        return false;
    }
    s->used += len;
    return true;
}

/* vynull's beat-grid blob: 20-byte preamble, 16 LE bytes per beat. */
static size_t make_grid(uint8_t *b, uint32_t beats, uint16_t bpm100)
{
    size_t len = DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * beats;
    memset(b, 0, len);
    b[2] = 0x08u;
    b[4] = (uint8_t)beats;
    b[5] = (uint8_t)(beats >> 8);
    for (uint32_t i = 0; i < beats; i++) {
        uint8_t *e = &b[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * i];
        uint32_t t = 250u + (uint32_t)(((uint64_t)i * 6000000u) / bpm100);
        e[0] = (uint8_t)(i % 4u + 1u);
        e[2] = (uint8_t)bpm100;
        e[3] = (uint8_t)(bpm100 >> 8);
        e[4] = (uint8_t)t;
        e[5] = (uint8_t)(t >> 8);
        e[6] = (uint8_t)(t >> 16);
        e[7] = (uint8_t)(t >> 24);
        memset(&e[8], 0xff, 8);
    }
    return len;
}

static void test_grid_parse(void)
{
    static uint8_t g[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * 8u];
    size_t len = make_grid(g, 8u, 12850u);
    dj_link_anlz_beat_t b;
    CHECK(dj_link_anlz_grid_count(g, len) == 8u);
    CHECK(dj_link_anlz_grid_count(g, len - 1u) == 7u);    /* partial last beat */
    CHECK(dj_link_anlz_grid_count(g, DJ_LINK_ANLZ_GRID_HEAD) == 0u);
    CHECK(dj_link_anlz_grid_count(NULL, len) == 0u);
    CHECK(dj_link_anlz_grid_beat(g, len, 5u, &b));
    CHECK(b.beat_in_bar == 2u && b.bpm100 == 12850u);
    CHECK(b.time_ms == 250u + (5u * 6000000u) / 12850u);
    CHECK(!dj_link_anlz_grid_beat(g, len, 8u, &b));
}

static void test_preview(void)
{
    uint8_t out[DJ_LINK_ANLZ_PREVIEW_LEN];
    static uint8_t detail[1200];
    for (size_t i = 0; i < sizeof(detail); i++) {
        detail[i] = (uint8_t)(0x20u | (i % 3u)); /* whiteness 1, heights 0..2 */
    }
    detail[4] = 0xe0u | 31u;                     /* column 1's tallest */
    CHECK(dj_link_anlz_preview(detail, sizeof(detail), out));
    CHECK(out[0] == (0x20u | 2u));               /* max of 0,1,2 */
    CHECK(out[1] == (0xe0u | 31u));              /* keeps its whiteness */
    CHECK(out[399] == (0x20u | 2u));
    /* Fewer entries than columns: each column samples its entry. */
    CHECK(dj_link_anlz_preview(detail, 3u, out));
    CHECK(out[0] == detail[0] && out[399] == detail[2]);
    CHECK(!dj_link_anlz_preview(NULL, 0u, out) && out[0] == 0u);
}

/* Write DAT + EXT, then parse them exactly like a local USB track. */
static void test_roundtrip(void)
{
    const char *dat = "test_dj_link_anlz.DAT";
    const char *ext = "test_dj_link_anlz.EXT";
    const char *audio = "/sd/djlcache/0A1B2C3D.mp3";
    static uint8_t grid[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * 300u];
    size_t grid_len = make_grid(grid, 300u, 12400u);
    size_t wave_len = 150u * 95u;                /* 95 s */
    uint8_t *wave = malloc(wave_len);
    for (size_t i = 0; i < wave_len; i++) {
        wave[i] = (uint8_t)((i * 13u) & 0xffu);
    }

    FILE *f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, audio, grid, grid_len, wave, wave_len, NULL, 0u));
    if (f) {
        fclose(f);
    }
    f = fopen(ext, "wb");
    CHECK(f && dj_link_anlz_write_ext(file_write, f, wave, wave_len, NULL, 0u));
    if (f) {
        fclose(f);
    }

    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(strcmp(meta.audio_path, audio) == 0);
    CHECK(meta.beat_count == 300u);
    CHECK(meta.bpm == 124u);
    if (meta.beats && meta.beat_count == 300u) {
        CHECK(meta.beats[0].beat_phase == 1u && meta.beats[3].beat_phase == 4u);
        CHECK(meta.beats[4].beat_phase == 1u && meta.beats[4].bpm_x100 == 12400u);
        CHECK(meta.beats[299].time_ms == 250u + (299u * 6000000u) / 12400u);
    }
    uint8_t preview[DJ_LINK_ANLZ_PREVIEW_LEN];
    dj_link_anlz_preview(wave, wave_len, preview);
    CHECK(meta.has_waveform_low);
    CHECK(memcmp(meta.waveform_low, preview, sizeof(preview)) == 0);
    CHECK(!meta.has_vbr && meta.cue_count == 0u);

    // Borrowed stream wrappers must parse the same metadata and leave ownership
    // with the worker, which may use an in-memory stream after gated SD reads.
    anlz_metadata_t borrowed={0};
    f=fopen(dat,"rb");
    CHECK(f && anlz_parse_dat_stream(f,&borrowed)==ESP_OK);
    CHECK(borrowed.beat_count==meta.beat_count && !strcmp(borrowed.audio_path,audio));
    if(f) {CHECK(!fseek(f,0,SEEK_SET) && fgetc(f)=='P');fclose(f);}
    anlz_free(&borrowed);
    CHECK(anlz_parse_dat_stream(NULL,&borrowed)==ESP_ERR_INVALID_ARG);

    CHECK(anlz_parse_ext(ext, &meta) == ESP_OK);
    CHECK(meta.waveform_high_len == wave_len);
    CHECK(meta.waveform_high && memcmp(meta.waveform_high, wave, wave_len) == 0);
    CHECK(anlz_analysis_span_ms(95000u, meta.waveform_span_ms) == 95000u);
    anlz_free(&meta);

    /* Waveform only (the peer has no grid): DAT without PQTZ. */
    f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, audio, NULL, 0u, wave, wave_len, NULL, 0u));
    if (f) {
        fclose(f);
    }
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(meta.beat_count == 0u && meta.beats == NULL && meta.has_waveform_low);
    anlz_free(&meta);

    /* Grid only: DAT without PWAV, no EXT. */
    f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, audio, grid, grid_len, NULL, 0u, NULL, 0u));
    if (f) {
        fclose(f);
    }
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(meta.beat_count == 300u && !meta.has_waveform_low);
    anlz_free(&meta);
    CHECK(!dj_link_anlz_write_ext(file_write, NULL, NULL, 0u, NULL, 0u));

    /* A failing sink and a bad path are reported. */
    short_sink_t s = { .limit = 40u, .used = 0u };
    CHECK(!dj_link_anlz_write_dat(short_write, &s, audio, grid, grid_len, wave, wave_len, NULL, 0u));
    s.used = 0u;
    CHECK(!dj_link_anlz_write_ext(short_write, &s, wave, wave_len, NULL, 0u));
    CHECK(!dj_link_anlz_write_dat(file_write, NULL, "", grid, grid_len, wave, wave_len, NULL, 0u));

    remove(dat);
    remove(ext);
    free(wave);
}

/* vynull's cue-list entry (cuepoints.go): 124 bytes LE, number 1..8 =
 * hot cue A..H, 0 = memory cue, type 1 cue / 2 loop. */
static size_t put_cue(uint8_t *b, uint32_t entry, uint16_t number, uint16_t type,
                      uint32_t time_ms, uint32_t end_ms)
{
    memset(b, 0, entry);
    b[0] = (uint8_t)entry;
    b[4] = (uint8_t)number;
    b[6] = (uint8_t)type;
    for (unsigned i = 0; i < 4u; i++) {
        b[0x0c + i] = (uint8_t)(time_ms >> (8u * i));
        b[0x20 + i] = (uint8_t)(end_ms >> (8u * i));
    }
    return entry;
}

/* v306: a vynull grid whose downbeat the user moved to the third beat
 * (numbers 3, 4, 1, 2, ...) keeps its beat numbers through the DAT, so beat
 * number 1 (anlz_beat_is_downbeat) is the downbeat, not phase 0. */
static void test_downbeat_roundtrip(void)
{
    const char *dat = "test_dj_link_anlz_downbeat.DAT";
    static uint8_t grid[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * 16u];
    size_t grid_len = make_grid(grid, 16u, 12000u);
    for (uint32_t i = 0; i < 16u; i++) {
        grid[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * i] = (uint8_t)((i + 2u) % 4u + 1u);
    }
    FILE *f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, "/sd/djlcache/1.mp3", grid, grid_len,
                                      NULL, 0u, NULL, 0u));
    if (f) {
        fclose(f);
    }
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(meta.beat_count == 16u);
    if (meta.beats && meta.beat_count == 16u) {
        for (uint16_t i = 0; i < 16u; i++) {
            CHECK(anlz_beat_is_downbeat(meta.beats[i].beat_phase) == (i % 4u == 2u));
            CHECK(anlz_beat_bar_index(meta.beats[i].beat_phase) == (i + 2u) % 4u);
        }
        CHECK(meta.beats[2].time_ms == 250u + 1000u);   /* 120 BPM, 500 ms beats */
    }
    anlz_free(&meta);
    remove(dat);
}

/* Pajoniiir keeps source order and reports truncation (package B contract).
 * It does not silently replace source order with the donor's earliest sort. */
static void test_memory_cue_list(void)
{
    const char *dat = "test_dj_link_anlz_memory.DAT";
    enum { N = 20 };
    static uint8_t c[124u * (N + 2u)];
    size_t len = 0;
    for (uint32_t i = 0; i < N; i++) {           /* 20000, 19000, ... 1000 */
        len += put_cue(&c[len], 124u, 0u, 1u, (N - i) * 1000u, 0xffffffffu);
    }
    len += put_cue(&c[len], 124u, 0u, 2u, 2500u, 6500u);      /* a memory loop */
    len += put_cue(&c[len], 124u, 0u, 1u, 3000u, 0xffffffffu); /* duplicate */
    FILE *f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, "/sd/djlcache/M.mp3", NULL, 0u,
                                      NULL, 0u, c, len));
    if (f) {
        fclose(f);
    }
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(meta.memory_cue_count == ANLZ_MAX_MEMORY_CUES);
    CHECK(meta.memory_cues_truncated);
    for (uint8_t i=0;i<meta.memory_cue_count;++i)
        CHECK(meta.memory_cues[i].start_ms==(N-i)*1000u);
    CHECK(meta.cue_count == 0u);
    anlz_free(&meta);
    f=fopen(dat,"wb");
    CHECK(f && dj_link_anlz_write_dat(file_write,f,"/sd/djlcache/M.mp3",NULL,0,NULL,0,c+124u*N,248));
    if(f)fclose(f);
    CHECK(anlz_parse_dat(dat,&meta)==ESP_OK);
    CHECK(meta.memory_cue_count==2 && meta.memory_cues[0].type==ANLZ_CUE_LOOP &&
        meta.memory_cues[0].start_ms==2500 && meta.memory_cues[0].end_ms==6500);
    anlz_free(&meta);
    remove(dat);
}

/* v312: a USB track's overview (library, anlz_preview_from_high) is built
 * from PWV3 with the same rule as a DJ Link peer's PWAV, so one track looks
 * the same from both sources. rekordbox's own PWAV is a low average. */
static void test_usb_preview_matches_peer(void)
{
    static const size_t lens[] = { 28660u, 400u, 401u, 3u, 1u };
    static uint8_t detail[28660];
    uint32_t x = 12345u;
    for (size_t i = 0; i < sizeof(detail); i++) {
        x = x * 1103515245u + 12345u;
        detail[i] = (uint8_t)(x >> 16);
    }
    for (size_t k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        uint8_t usb[ANLZ_WAVEFORM_LOW_LEN];
        uint8_t peer[DJ_LINK_ANLZ_PREVIEW_LEN];
        FILE *f=fopen("link_preview.DAT","wb");
        CHECK(f && dj_link_anlz_write_dat(file_write,f,"/sd/test.mp3",NULL,0,detail,lens[k],NULL,0));
        if(f)fclose(f);
        anlz_metadata_t meta={0};CHECK(anlz_parse_dat("link_preview.DAT",&meta)==ESP_OK);
        memcpy(usb,meta.waveform_low,sizeof(usb));anlz_free(&meta);remove("link_preview.DAT");
        CHECK(dj_link_anlz_preview(detail, lens[k], peer));
        CHECK(memcmp(usb, peer, sizeof(usb)) == 0);
    }
    /* The loudest entry of a column wins, colour bits kept. */
    uint8_t quiet[800];
    memset(quiet, 0x22, sizeof(quiet));    /* height 2 */
    quiet[401] = 0xbf;                     /* column 200: height 31 */
    uint8_t out[ANLZ_WAVEFORM_LOW_LEN];
    CHECK(dj_link_anlz_preview(quiet, sizeof(quiet), out));
    CHECK(out[200] == 0xbfu && out[199] == 0x22u && out[0] == 0x22u);
    CHECK(!dj_link_anlz_preview(NULL, 10u, out) && out[0] == 0u);
    CHECK(!dj_link_anlz_preview(quiet, 0u, out));
    CHECK(!dj_link_anlz_preview(quiet, 10u, NULL));
}

/* v313: rekordbox's PWV4 colour preview read from .EXT (header: entry size
 * 6, count), deep-copied by anlz_clone, freed by anlz_free; one column is
 * the loudest byte 0 and the bass / mid / treble mix scaled to 255. A
 * missing or odd PWV4 leaves the PWV3 alone. */
static void put_be32(FILE *f, uint32_t v)
{
    const uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v };
    fwrite(b, 1, 4, f);
}

static void write_ext_with_pwv4(const char *path, uint32_t entry, uint32_t entries)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        return;
    }
    const uint32_t pwv3_len = 300u;
    const uint32_t pwv4_len = entry * entries;
    const uint32_t total = 28u + (24u + pwv3_len) + (24u + pwv4_len);
    fwrite("PMAI", 1, 4, f); put_be32(f, 28u); put_be32(f, total);
    for (int i = 0; i < 16; i++) fputc(0, f);
    fwrite("PWV3", 1, 4, f); put_be32(f, 24u); put_be32(f, 24u + pwv3_len);
    put_be32(f, 1u); put_be32(f, pwv3_len); put_be32(f, 0x00960000u);
    for (uint32_t i = 0; i < pwv3_len; i++) fputc((int)(i % 32u), f);
    fwrite("PWV4", 1, 4, f); put_be32(f, 24u); put_be32(f, 24u + pwv4_len);
    put_be32(f, entry); put_be32(f, entries); put_be32(f, 0u);
    for (uint32_t i = 0; i < entries; i++) {
        /* height i % 80, bass-heavy in the first half, treble after */
        const bool bass = i < entries / 2u;
        const uint8_t e[6] = { (uint8_t)(i % 80u), 255u, 20u,
                               bass ? 60u : 0u, 15u, bass ? 0u : 40u };
        fwrite(e, 1, entry < 6u ? entry : 6u, f);
        for (uint32_t k = 6u; k < entry; k++) fputc(0, f);
    }
    fclose(f);
}

static void test_color_preview(void)
{
    const char *ext = "test_dj_link_anlz_pwv4.EXT";
    write_ext_with_pwv4(ext, 6u, 1200u);
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_ext(ext, &meta) == ESP_OK);
    CHECK(meta.waveform_high_len == 300u);
    CHECK(meta.color_preview && meta.color_preview_len == 7200u);
    CHECK(anlz_color_preview_peak(meta.color_preview, meta.color_preview_len) == 79u);

    anlz_color_column_t c;
    /* 400 columns: 3 entries each. Column 0 = entries 0..2, bass. */
    CHECK(anlz_color_preview_column(meta.color_preview, meta.color_preview_len, 0, 400, &c));
    CHECK(c.height == 2u && c.r == 255u && c.g == 63u && c.b == 0u);
    CHECK(anlz_color_preview_column(meta.color_preview, meta.color_preview_len, 399, 400, &c));
    CHECK(c.r == 0u && c.b == 255u && c.g == 95u);
    /* Wider than the preview: each column samples its entry. */
    CHECK(anlz_color_preview_column(meta.color_preview, meta.color_preview_len, 2399, 2400, &c));
    CHECK(c.height == (uint8_t)(1199u % 80u));
    CHECK(!anlz_color_preview_column(meta.color_preview, meta.color_preview_len, 400, 400, &c));
    CHECK(!anlz_color_preview_column(NULL, 0, 0, 400, &c));
    CHECK(!anlz_color_preview_column(meta.color_preview, 5u, 0, 400, &c));
    CHECK(anlz_color_preview_peak(NULL, 10u) == 0u);

    /* Silent bands: no colour, the caller's default. */
    uint8_t silent[6] = { 30u, 255u, 0u, 0u, 0u, 0u };
    CHECK(anlz_color_preview_column(silent, 6u, 0, 1, &c));
    CHECK(c.height == 30u && c.r == 0u && c.g == 0u && c.b == 0u);

    anlz_metadata_t copy;
    CHECK(anlz_clone(&meta, &copy) == ESP_OK);
    CHECK(copy.color_preview && copy.color_preview != meta.color_preview);
    CHECK(copy.color_preview_len == 7200u &&
          memcmp(copy.color_preview, meta.color_preview, 7200u) == 0);
    anlz_free(&meta);
    CHECK(!meta.color_preview && meta.color_preview_len == 0u);
    anlz_free(&copy);
    CHECK(!copy.color_preview);

    /* Another entry size: ignored, the PWV3 is kept. */
    write_ext_with_pwv4(ext, 8u, 100u);
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_ext(ext, &meta) == ESP_OK);
    CHECK(meta.waveform_high_len == 300u && !meta.color_preview && meta.color_preview_len == 0u);
    anlz_free(&meta);
    remove(ext);
}

/* v314: a DJ Link peer's PWV4 (0x2c04 blob: LE length + section, as
 * vynull serves rekordbox's .EXT) goes into our EXT after the PWV3 and is
 * read back by the USB parser as the deck's colour preview. */
static size_t make_pwv4_blob(uint8_t *b, uint32_t head, uint32_t entry, uint32_t entries)
{
    const uint32_t sec = head + entry * entries;
    b[0] = (uint8_t)sec; b[1] = (uint8_t)(sec >> 8); b[2] = (uint8_t)(sec >> 16); b[3] = (uint8_t)(sec >> 24);
    uint8_t *s = &b[4];
    memset(s, 0, sec);
    memcpy(s, "PWV4", 4);
    const uint32_t f[4] = { head, sec, entry, entries };
    for (int k = 0; k < 4; k++) {
        s[4 + k * 4] = (uint8_t)(f[k] >> 24); s[5 + k * 4] = (uint8_t)(f[k] >> 16);
        s[6 + k * 4] = (uint8_t)(f[k] >> 8);  s[7 + k * 4] = (uint8_t)f[k];
    }
    for (uint32_t i = 0; i < entry * entries; i++) {
        s[head + i] = (uint8_t)(i * 7u + 3u);
    }
    return 4u + sec;
}

static void test_peer_color_preview(void)
{
    static uint8_t blob[DJ_LINK_ANLZ_COLOR_BLOB_MAX + 64u];
    const uint8_t *e = NULL;
    size_t n = 0;
    size_t len = make_pwv4_blob(blob, 24u, 6u, 1200u);
    CHECK(len == DJ_LINK_ANLZ_COLOR_BLOB_MAX);
    CHECK(dj_link_anlz_color_entries(blob, len, &e, &n) && n == 7200u && e == &blob[4 + 24]);
    /* refused: truncated, another tag, another entry size, empty, NULL */
    CHECK(!dj_link_anlz_color_entries(blob, len - 1u, &e, &n));
    blob[7] = '5';
    CHECK(!dj_link_anlz_color_entries(blob, len, &e, &n));
    blob[7] = '4';
    CHECK(!dj_link_anlz_color_entries(blob, make_pwv4_blob(blob, 24u, 8u, 10u), &e, &n));
    CHECK(!dj_link_anlz_color_entries(blob, make_pwv4_blob(blob, 24u, 6u, 0u), &e, &n));
    CHECK(!dj_link_anlz_color_entries(NULL, 100u, &e, &n));
    CHECK(!dj_link_anlz_color_entries(blob, 10u, &e, &n));

    /* written after the PWV3, read back by anlz_parse_ext */
    len = make_pwv4_blob(blob, 24u, 6u, 1200u);
    CHECK(dj_link_anlz_color_entries(blob, len, &e, &n));
    static uint8_t wave[900];
    for (size_t i = 0; i < sizeof(wave); i++) wave[i] = (uint8_t)(i % 32u);
    const char *ext = "test_dj_link_anlz_peer_pwv4.EXT";
    FILE *f = fopen(ext, "wb");
    CHECK(f && dj_link_anlz_write_ext(file_write, f, wave, sizeof(wave), e, n));
    if (f) fclose(f);
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_ext(ext, &meta) == ESP_OK);
    CHECK(meta.waveform_high_len == sizeof(wave));
    CHECK(meta.color_preview_len == 7200u && meta.color_preview &&
          memcmp(meta.color_preview, e, 7200u) == 0);
    anlz_free(&meta);
    /* no colour: PWV3 only, as before */
    f = fopen(ext, "wb");
    CHECK(f && dj_link_anlz_write_ext(file_write, f, wave, sizeof(wave), NULL, 0u));
    if (f) fclose(f);
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_ext(ext, &meta) == ESP_OK);
    CHECK(meta.waveform_high_len == sizeof(wave) && !meta.color_preview);
    anlz_free(&meta);
    remove(ext);
}

static void test_cue_parse(void)
{
    uint8_t c[124u * 3u];
    size_t len = put_cue(c, 124u, 0u, 1u, 1234u, 0xffffffffu);
    len += put_cue(&c[len], 76u, 2u, 2u, 2000u, 4000u);     /* legacy CDJ entry */
    len += put_cue(&c[len], 124u, 9u, 1u, 500u, 0xffffffffu); /* past H: memory */
    dj_link_anlz_cue_t q;
    CHECK(dj_link_anlz_cue_count(c, len) == 3u);
    CHECK(dj_link_anlz_cue_count(c, len - 1u) == 2u);         /* last one cut */
    CHECK(dj_link_anlz_cue_count(NULL, len) == 0u);
    CHECK(dj_link_anlz_cue_count(c, 0u) == 0u);
    CHECK(dj_link_anlz_cue(c, len, 0u, &q) && q.hot_cue == 0u && !q.loop && q.time_ms == 1234u);
    CHECK(dj_link_anlz_cue(c, len, 1u, &q) && q.hot_cue == 2u && q.loop &&
          q.time_ms == 2000u && q.loop_end_ms == 4000u);
    CHECK(dj_link_anlz_cue(c, len, 2u, &q) && q.hot_cue == 0u && q.time_ms == 500u);
    CHECK(!dj_link_anlz_cue(c, len, 3u, &q));
    /* A zero or short entry length stops the walk instead of looping. */
    uint8_t bad[124];
    put_cue(bad, 124u, 1u, 1u, 10u, 0xffffffffu);
    bad[0] = 0u;
    CHECK(dj_link_anlz_cue_count(bad, sizeof(bad)) == 0u);
    bad[0] = 0x20u;
    CHECK(dj_link_anlz_cue_count(bad, sizeof(bad)) == 0u);
}

/* v303: the peer's cues and grid edits come through on reload. */
static void test_cue_roundtrip(void)
{
    const char *dat = "test_dj_link_anlz_cues.DAT";
    static uint8_t grid[DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY * 16u];
    size_t grid_len = make_grid(grid, 16u, 12800u);
    uint8_t c[124u * 4u];
    size_t len = put_cue(c, 124u, 0u, 1u, 3000u, 0xffffffffu);
    len += put_cue(&c[len], 124u, 2u, 2u, 2000u, 4000u);
    len += put_cue(&c[len], 124u, 0u, 1u, 1234u, 0xffffffffu);
    len += put_cue(&c[len], 124u, 8u, 1u, 9000u, 0xffffffffu);

    FILE *f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, "/sd/djlcache/X.mp3", grid, grid_len,
                                      NULL, 0u, c, len));
    if (f) {
        fclose(f);
    }
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(meta.beat_count == 16u && pcob_count(dat)==2);
    /* v309: every memory cue, by time. */
    CHECK(meta.memory_cue_count == 2u);
    CHECK(meta.memory_cues[0].start_ms == 3000u && meta.memory_cues[1].start_ms == 1234u);
    CHECK(meta.memory_cues[0].end_ms == 0u && meta.memory_cues[1].end_ms == 0u);
    CHECK(meta.cue_count == 2u);
    for (uint8_t i = 0; i < meta.cue_count; i++) {
        const anlz_cue_t *q = &meta.cues[i];
        if (q->index == 1u) {
            CHECK(q->type == ANLZ_CUE_LOOP && q->start_ms == 2000u && q->end_ms == 4000u);
        } else {
            CHECK(q->index == 7u && q->type == ANLZ_CUE_SINGLE && q->start_ms == 9000u);
        }
    }
    anlz_free(&meta);

    /* The peer answered "no cues": empty lists, still read as a statement. */
    f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, "/sd/djlcache/X.mp3", grid, grid_len,
                                      NULL, 0u, c, 0u));
    if (f) {
        fclose(f);
    }
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(pcob_count(dat)==2 && meta.cue_count == 0u);
    CHECK(meta.memory_cue_count == 0u);
    CHECK(meta.beat_count == 16u);
    anlz_free(&meta);

    /* Cues unknown (no answer): no PCOB, the deck keeps what it has. */
    f = fopen(dat, "wb");
    CHECK(f && dj_link_anlz_write_dat(file_write, f, "/sd/djlcache/X.mp3", grid, grid_len,
                                      NULL, 0u, NULL, 0u));
    if (f) {
        fclose(f);
    }
    memset(&meta, 0, sizeof(meta));
    CHECK(anlz_parse_dat(dat, &meta) == ESP_OK);
    CHECK(pcob_count(dat)==0 && meta.cue_count == 0u);
    anlz_free(&meta);
    remove(dat);
}

/* v303: v302 hardware kept a vynull beat grid / cue edit off the deck - a
 * cached .DAT was never asked for again. Now every load asks, and only a
 * complete answer replaces the cache. */
static void test_commit(void)
{
    dj_link_anlz_answers_t a = { false, true, true, true, 1000u, 16u, 2u };
    dj_link_anlz_commit_t c = dj_link_anlz_commit(&a);
    CHECK(c.write_dat && c.cue_lists && c.write_ext && !c.drop_ext);

    /* First fetch, cues unsupported (older peer): DAT without PCOB. */
    a = (dj_link_anlz_answers_t){ false, true, true, false, 1000u, 16u, 0u };
    c = dj_link_anlz_commit(&a);
    CHECK(c.write_dat && !c.cue_lists && c.write_ext);

    /* First fetch, cues only. */
    a = (dj_link_anlz_answers_t){ false, false, false, true, 0u, 0u, 3u };
    c = dj_link_anlz_commit(&a);
    CHECK(c.write_dat && c.cue_lists && !c.write_ext && c.drop_ext);

    /* Nothing at all: nothing written, cached or not. */
    a = (dj_link_anlz_answers_t){ false, true, true, true, 0u, 0u, 0u };
    c = dj_link_anlz_commit(&a);
    CHECK(!c.write_dat && !c.write_ext && !c.drop_ext);
    a.dat_cached = true;
    c = dj_link_anlz_commit(&a);
    CHECK(!c.write_dat && !c.write_ext && !c.drop_ext);

    /* Cached and fully answered: refreshed, even with empty cue lists. */
    a = (dj_link_anlz_answers_t){ true, true, true, true, 1000u, 16u, 0u };
    c = dj_link_anlz_commit(&a);
    CHECK(c.write_dat && c.cue_lists && c.write_ext);

    /* Cached, peer re-analysed without a wave: stale EXT goes. */
    a = (dj_link_anlz_answers_t){ true, true, true, true, 0u, 16u, 1u };
    c = dj_link_anlz_commit(&a);
    CHECK(c.write_dat && !c.write_ext && c.drop_ext);

    /* Cached, partial answer (timeout, unsupported request): keep it all. */
    a = (dj_link_anlz_answers_t){ true, true, true, false, 1000u, 16u, 0u };
    c = dj_link_anlz_commit(&a);
    CHECK(!c.write_dat && !c.write_ext && !c.drop_ext);
    a = (dj_link_anlz_answers_t){ true, false, true, true, 0u, 16u, 2u };
    c = dj_link_anlz_commit(&a);
    CHECK(!c.write_dat && !c.write_ext && !c.drop_ext);
}

int main(void)
{
    test_grid_parse();
    test_preview();
    test_roundtrip();
    test_downbeat_roundtrip();
    test_cue_parse();
    test_memory_cue_list();
    test_usb_preview_matches_peer();
    test_color_preview();
    test_peer_color_preview();
    test_cue_roundtrip();
    test_commit();
    if (s_failures) {
        fprintf(stderr, "%d dj_link_anlz check(s) failed\n", s_failures);
        return 1;
    }
    printf("all dj_link_anlz tests passed\n");
    return 0;
}
