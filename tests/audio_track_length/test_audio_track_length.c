/* v271: the decoded file length, which can run past the Rekordbox analysis.
 * The PVBR table and the waveforms keep the analysis time base; only the
 * track length and the seeks past the table use the file length.
 * v273: where the PVBR entries really are (after the ID3v2 tag, at exact
 * frame times), from real Rekordbox exports. */
#include "audio_seek_skip.h"
#include "audio_track_length.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LEN 400u
/* v270 hardware: 4926489 byte file, analysis 248140 ms, ~12 kB per entry,
 * table from byte 7752 (after the ID3 tag). */
#define HW_SPAN_MS 248140u
#define HW_FILE_SIZE 4926489u
#define HW_FIRST_BYTE 7752u
#define HW_ENTRY_BYTES 11940u

static uint32_t s_pvbr[LEN];

static void fill_table(uint32_t first, uint32_t entry_bytes)
{
    for (uint32_t i = 0; i < LEN; i++) s_pvbr[i] = first + i * entry_bytes;
}

static uint32_t entry_ms(uint32_t idx, uint32_t span_ms)
{
    return (uint32_t)(((uint64_t)idx * span_ms) / LEN);
}

static uint32_t abs_diff(uint32_t a, uint32_t b)
{
    return a > b ? a - b : b - a;
}

/* First frame of an MPEG1 Layer III 48 kHz stereo file with a Xing/Info
 * frame count, after `pad` bytes of junk. */
static size_t xing_frame(uint8_t *buf, size_t pad, const char *tag, uint32_t frames)
{
    memset(buf, 0, 256);
    uint8_t *h = buf + pad;
    h[0] = 0xFF; h[1] = 0xFB; h[2] = 0x94; h[3] = 0x44;   /* MPEG1 L3 128k 48k joint */
    uint8_t *x = h + 4 + 32;
    memcpy(x, tag, 4);
    x[7] = 0x0F;                                          /* frames|bytes|toc|quality */
    x[8] = (uint8_t)(frames >> 24); x[9] = (uint8_t)(frames >> 16);
    x[10] = (uint8_t)(frames >> 8); x[11] = (uint8_t)frames;
    return pad + 4 + 32 + 12;
}

static void test_header_duration(void)
{
    uint8_t buf[256];
    /* 10850 frames x 1152 / 48000 = 260.4 s */
    size_t n = xing_frame(buf, 0, "Xing", 10850u);
    assert(audio_mp3_header_duration_ms(buf, n) == 260400u);
    n = xing_frame(buf, 3, "Info", 10850u);
    assert(audio_mp3_header_duration_ms(buf, n) == 260400u);
    /* A plain first frame carries no count. */
    n = xing_frame(buf, 0, "abcd", 10850u);
    assert(audio_mp3_header_duration_ms(buf, n) == 0u);
    /* Cut before the count. */
    n = xing_frame(buf, 0, "Xing", 10850u);
    assert(audio_mp3_header_duration_ms(buf, n - 4u) == 0u);

    /* VBRI sits 32 bytes after the header whatever the channel mode. */
    memset(buf, 0, sizeof buf);
    buf[0] = 0xFF; buf[1] = 0xFB; buf[2] = 0x94; buf[3] = 0xC4;   /* mono */
    memcpy(buf + 36, "VBRI", 4);
    buf[36 + 16] = 0x2A; buf[36 + 17] = 0x62;                      /* 10850 frames */
    assert(audio_mp3_header_duration_ms(buf, 64u) == 260400u);

    /* ID3v2: 10 + syncsafe size (+ footer). */
    const uint8_t id3[10] = {'I', 'D', '3', 4, 0, 0, 0x00, 0x00, 0x3C, 0x3E};
    assert(audio_id3v2_size(id3, sizeof id3) == 10u + 7742u);
    const uint8_t id3_footer[10] = {'I', 'D', '3', 4, 0, 0x10, 0, 0, 0, 0x10};
    assert(audio_id3v2_size(id3_footer, sizeof id3_footer) == 36u);
    assert(audio_id3v2_size(buf, sizeof buf) == 0u);
}

/* v270 hardware: the table is uniform in real time over the analysis, and
 * the file holds ~155 kB past its last entry. Carrying the tail byte rate
 * there lands on the measured decoder EOF (~255.7 s). */
static void test_hw_table_extrapolates_to_eof(void)
{
    fill_table(HW_FIRST_BYTE, HW_ENTRY_BYTES);
    uint32_t end_ms = audio_pvbr_extrapolate_end_ms(s_pvbr, LEN, HW_SPAN_MS, 0u, HW_FILE_SIZE);
    assert(abs_diff(end_ms, 255700u) < 500u);
    assert(audio_track_length_resolve(HW_SPAN_MS, end_ms) == end_ms);

    /* A table that already reaches the end of the file adds nothing. */
    uint32_t covered = audio_pvbr_extrapolate_end_ms(s_pvbr, LEN, HW_SPAN_MS, 0u,
                                                     s_pvbr[LEN - 1u] + HW_ENTRY_BYTES);
    /* v273: the same file with its entries after a 155 kB tag ends at the
     * table. */
    assert(audio_pvbr_extrapolate_end_ms(s_pvbr, LEN, HW_SPAN_MS, 155000u, HW_FILE_SIZE) <
           HW_SPAN_MS + AUDIO_TRACK_LENGTH_TOLERANCE_MS);
    assert(audio_track_length_resolve(HW_SPAN_MS, covered) == HW_SPAN_MS);
    /* Unusable table. */
    memset(s_pvbr, 0, sizeof s_pvbr);
    assert(audio_pvbr_extrapolate_end_ms(s_pvbr, LEN, HW_SPAN_MS, 0u, HW_FILE_SIZE) == 0u);
    assert(audio_pvbr_extrapolate_end_ms(NULL, LEN, HW_SPAN_MS, 0u, HW_FILE_SIZE) == 0u);
}

static void test_resolve(void)
{
    assert(audio_track_length_resolve(248140u, 0u) == 248140u);
    assert(audio_track_length_resolve(0u, 255700u) == 255700u);
    /* within tolerance: the analysis stays the length */
    assert(audio_track_length_resolve(248140u, 248140u + AUDIO_TRACK_LENGTH_TOLERANCE_MS) == 248140u);
    /* a shorter file never shortens the track (EOF ends it anyway) */
    assert(audio_track_length_resolve(248140u, 240000u) == 248140u);
    assert(audio_bitrate_duration_ms(4000000u, 128u) == 250000u);
    assert(audio_bitrate_duration_ms(4000000u, 0u) == 0u);
}

/* The requested case: the real file is 5% longer than the metadata. */
static void test_file_five_percent_longer(void)
{
    const uint32_t span_ms = 248000u;
    const uint32_t file_ms = 260400u;                   /* +5% */
    fill_table(HW_FIRST_BYTE, HW_ENTRY_BYTES);
    const uint32_t track_ms = audio_track_length_resolve(span_ms, file_ms);
    assert(track_ms == file_ms);
    const size_t file_size = s_pvbr[LEN - 1u] +
        (size_t)(file_ms - entry_ms(LEN - 1u, span_ms)) * HW_ENTRY_BYTES / (span_ms / LEN);

    /* Inside the analysis nothing moves: same entry, same byte as before the
     * file length was known. Rescaling the table to 260.4 s would land
     * 120 s at entry 184, 6 s early. */
    uint32_t byte = 0;
    uint32_t landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, track_ms, file_size, NULL, 120000u, &byte);
    const uint32_t idx = audio_pvbr_index(120000u, span_ms, LEN);
    assert(idx == 193u);
    assert(byte == s_pvbr[idx]);
    assert(landing == audio_pvbr_entry_ms(idx, span_ms, LEN, 120000u));
    assert(landing <= 120000u && 120000u - landing < span_ms / LEN);
    assert(audio_pvbr_index(120000u, file_ms, LEN) == 184u);

    /* Past the analysis: the tail byte rate, one lead before the target,
     * the rest decoded and dropped by the seek skip. */
    landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, track_ms, file_size, NULL, 255000u, &byte);
    assert(landing == 255000u - AUDIO_PVBR_TAIL_LEAD_MS);
    const uint32_t expect = s_pvbr[LEN - 1u] +
        (uint32_t)((uint64_t)(landing - entry_ms(LEN - 1u, span_ms)) * HW_ENTRY_BYTES / (span_ms / LEN));
    assert(abs_diff(byte, expect) <= HW_ENTRY_BYTES / 100u);
    assert(byte > s_pvbr[LEN - 1u] && byte < file_size);
    assert(audio_seek_skip_frames(255000u, landing, 48000u) == 48000u);

    /* Just past the last entry: the entry itself, a short skip. */
    landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, track_ms, file_size, NULL, 247900u, &byte);
    assert(byte == s_pvbr[LEN - 1u] && landing == entry_ms(LEN - 1u, span_ms));

    /* Clamped to the file length, never past the end of the file. */
    landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, track_ms, file_size, NULL, 999999u, &byte);
    assert(landing == track_ms - AUDIO_PVBR_TAIL_LEAD_MS);
    assert(byte <= file_size);
}

/* Without a longer file the landing is the v263 one. */
static void test_locate_matches_v263_inside_span(void)
{
    fill_table(HW_FIRST_BYTE, HW_ENTRY_BYTES);
    const uint32_t targets[] = {0u, 946u, 168923u, 223893u, 247519u, HW_SPAN_MS};
    for (size_t i = 0; i < sizeof targets / sizeof targets[0]; i++) {
        uint32_t byte = 0;
        uint32_t landing = audio_pvbr_locate(s_pvbr, LEN, HW_SPAN_MS, HW_SPAN_MS, HW_FILE_SIZE,
                                             NULL, targets[i], &byte);
        uint32_t idx = audio_pvbr_index(targets[i], HW_SPAN_MS, LEN);
        assert(byte == s_pvbr[idx]);
        assert(landing == audio_pvbr_entry_ms(idx, HW_SPAN_MS, LEN, targets[i]));
    }
    /* v270 log: 223893 ms -> entry 360 at 223326 ms */
    uint32_t byte = 0;
    assert(audio_pvbr_locate(s_pvbr, LEN, HW_SPAN_MS, HW_SPAN_MS, HW_FILE_SIZE, NULL, 223893u, &byte) == 223326u);
}

/* A synthetic MPEG1 L3 128 kbps 48 kHz file: 384-byte frames of 24 ms, a
 * Xing frame, then `real` frames and an ID3v1 tag. v272 read Nerdy Roller
 * (Xing 181080 ms, decoded end 185250 ms) as a header 174 frames short;
 * v273 keeps the header whatever the scan says. */
#define NERDY_XING_FRAMES 7545u
#define LIE_REAL_FRAMES 7719u
#define LIE_FRAME_BYTES 384u
#define LIE_FRAME_MS 24u
#define LIE_SPAN_MS 181106u
#define LIE_REAL_MS (LIE_REAL_FRAMES * LIE_FRAME_MS)

typedef struct {
    uint8_t *data;
    size_t size;
    uint32_t reads;
} mem_file_t;

static size_t mem_read(void *ctx, size_t offset, void *dst, size_t bytes)
{
    mem_file_t *f = ctx;
    f->reads++;
    if (offset >= f->size) return 0u;
    if (bytes > f->size - offset) bytes = f->size - offset;
    memcpy(dst, f->data + offset, bytes);
    return bytes;
}

static void put_header(uint8_t *h)
{
    h[0] = 0xFF; h[1] = 0xFB; h[2] = 0x94; h[3] = 0x44;
}

/* `id3` bytes of ID3v2 tag (0 = none) before the Xing frame. */
static mem_file_t make_file(size_t id3, uint32_t xing_frames, uint32_t real)
{
    mem_file_t f = {0};
    f.size = id3 + (size_t)(1u + real) * LIE_FRAME_BYTES + 128u;
    f.data = calloc(1, f.size);
    assert(f.data);
    if (id3) {
        const size_t body = id3 - 10u;
        memcpy(f.data, "ID3\x04\x00\x00", 6);
        f.data[6] = (uint8_t)((body >> 21) & 0x7F); f.data[7] = (uint8_t)((body >> 14) & 0x7F);
        f.data[8] = (uint8_t)((body >> 7) & 0x7F);  f.data[9] = (uint8_t)(body & 0x7F);
        put_header(f.data + 100u);        /* a sync pattern inside the tag */
    }
    xing_frame(f.data + id3, 0, "Xing", xing_frames);
    for (uint32_t i = 1; i <= real; i++) put_header(f.data + id3 + (size_t)i * LIE_FRAME_BYTES);
    uint8_t *tag = f.data + f.size - 128u;
    memcpy(tag, "TAG", 3);
    put_header(tag + 3);
    return f;
}

/* The Rekordbox table of that file: entry k at frame
 * floor((k + 1) * N / 400) - 8, counted from the end of the tag. */
static void fill_rekordbox_table(uint32_t frames)
{
    for (uint32_t i = 0; i < LEN; i++) {
        s_pvbr[i] = audio_pvbr_entry_frame(i, frames, LEN) * LIE_FRAME_BYTES;
    }
}

/* What ae_resolve_track_length does with a PVBR table. */
static audio_track_length_decision_t load_with_table(mem_file_t *f, audio_pvbr_geometry_t *geom,
                                                     audio_mp3_scan_t *scan)
{
    uint8_t head[1024];
    const size_t id3 = audio_id3v2_size(f->data, f->size);
    memcpy(head, f->data + id3, sizeof head);
    audio_mp3_first_frame_t ff;
    assert(audio_mp3_first_frame(head, sizeof head, &ff));
    *geom = (audio_pvbr_geometry_t){
        .base = audio_pvbr_base(mem_read, f, s_pvbr, LEN, id3, f->size),
        .frames = ff.count + 1u,
        .frame_samples = ff.frame_samples,
        .hz = ff.hz,
    };
    audio_track_length_inputs_t in = {
        .span_ms = LIE_SPAN_MS,
        .xing_ms = audio_mp3_header_duration_ms(head, sizeof head),
        .pvbr_ms = audio_pvbr_extrapolate_end_ms(s_pvbr, LEN, LIE_SPAN_MS, geom->base, f->size),
    };
    assert(audio_mp3_scan(mem_read, f, geom->base + s_pvbr[LEN - 1u], f->size, 0u, scan));
    in.scan_ms = audio_pvbr_entry_time_ms(geom, LEN - 1u, LEN, LIE_SPAN_MS) + scan->ms;
    return audio_track_length_decide(&in);
}

static void test_xing_kept_at_load(void)
{
    /* Nerdy Roller as it is: a 99513-byte tag, 7545 frames after the Xing
     * frame, exactly what the header says. */
    mem_file_t f = make_file(99513u, NERDY_XING_FRAMES, NERDY_XING_FRAMES);
    fill_rekordbox_table(NERDY_XING_FRAMES + 1u);
    audio_pvbr_geometry_t geom;
    audio_mp3_scan_t scan;
    audio_track_length_decision_t d = load_with_table(&f, &geom, &scan);
    assert(geom.base == 99513u && geom.frames == 7546u && geom.hz == 48000u);
    /* The last entry is 8 frames before the end; the tag's sync pattern and
     * the ID3v1 tag are not frames. */
    assert(scan.frames == AUDIO_PVBR_ENTRY_FRAME_LAG && scan.last_end == f.size - 128u);
    assert(d.xing == AUDIO_XING_TRUSTED && d.source == AUDIO_TRACK_LENGTH_XING);
    assert(d.file_ms == 181080u && d.track_ms == LIE_SPAN_MS);
    free(f.data);
}

/* v273: real Rekordbox 6 exports against their files (all 400 entries of
 * each, checked offline; a handful kept here). */
typedef struct {
    const char *name;
    size_t id3;
    uint32_t xing_frames;
    uint32_t idx[4];
    uint32_t pvbr[4];
    uint32_t frame[4];            /* file frame at id3 + pvbr, Xing frame = 0 */
} real_table_t;

static const real_table_t k_real[] = {
    {"California Dreamin'", 133217u, 7045u, {0, 24, 99, 399},
     {4896u, 199008u, 838080u, 3411744u}, {9u, 432u, 1753u, 7038u}},
    {"Nerdy Roller", 99513u, 7545u, {0, 24, 99, 399},
     {1248u, 220320u, 870288u, 3736656u}, {10u, 463u, 1878u, 7538u}},
    {"Stubborn", 1117u, 5545u, {0, 24, 99, 399},
     {2592u, 158784u, 656928u, 2624736u}, {5u, 338u, 1378u, 5538u}},
};

static void test_real_tables_entry_law(void)
{
    for (size_t t = 0; t < sizeof k_real / sizeof k_real[0]; t++) {
        const real_table_t *r = &k_real[t];
        const uint32_t n = r->xing_frames + 1u;
        for (int i = 0; i < 4; i++) {
            assert(audio_pvbr_entry_frame(r->idx[i], n, LEN) == r->frame[i]);
        }
        /* Rekordbox's own span gives the same N within a frame. */
        const uint32_t span = (uint32_t)(((uint64_t)n * 1152u * 1000u) / 48000u);
        assert(abs_diff(audio_pvbr_frames_from_span(span + 20u, 48000u, 1152u), n) <= 1u);
    }
    assert(audio_pvbr_frames_from_span(1000u, 0u, 1152u) == 0u);
    /* entries near the start never go below frame 0 */
    assert(audio_pvbr_entry_frame(0u, 400u, LEN) == 0u);
}

/* v272 hardware: California Dreamin' started its 10530 ms memory cue from
 * the start of the track. */
static void test_california_cue(void)
{
    const real_table_t *r = &k_real[0];
    const uint32_t n = r->xing_frames + 1u;
    const uint32_t span_ms = 169104u;
    const size_t file_size = 3547441u;
    const uint32_t per_frame = (uint32_t)((file_size - r->id3) / n);
    for (uint32_t i = 0; i < LEN; i++) s_pvbr[i] = audio_pvbr_entry_frame(i, n, LEN) * per_frame;
    for (int i = 0; i < 4; i++) s_pvbr[r->idx[i]] = r->pvbr[i];
    const audio_pvbr_geometry_t geom = {.base = r->id3, .frames = n, .frame_samples = 1152u, .hz = 48000u};

    /* v272: entry 24 read as a file offset, 133 kB (6.8 s) early, at its
     * nominal time 10146 ms. */
    uint32_t byte = 0;
    uint32_t landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, file_size, NULL, 10530u, &byte);
    assert(byte == 199008u && landing == 10146u);

    /* v273: the same entry after the tag, at frame 432 (10368 ms); the
     * 162 ms to the cue are decoded and dropped. */
    landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, file_size, &geom, 10530u, &byte);
    assert(byte == 133217u + 199008u && landing == 432u * 24u);
    assert(audio_seek_skip_frames(10530u, landing, 48000u) == 162u * 48u);

    /* Before entry 0, or within the lead of the start: the Xing frame. */
    assert(audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, file_size, &geom, 0u, &byte) == 0u &&
           byte == r->id3);
    assert(audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, file_size, &geom, 300u, &byte) == 0u &&
           byte == r->id3);

    /* Every target keeps the lead, so the frames a restarted decoder outputs
     * nothing for come out of the skip, and lands within ~0.5 s. */
    for (uint32_t target = 0; target <= span_ms; target += 7u) {
        landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, file_size, &geom, target, &byte);
        assert(landing <= target && target - landing < 600u);
        if (landing > 0u) {
            assert(audio_seek_skip_frames(target, landing, 48000u) >=
                   AUDIO_PVBR_RESYNC_LEAD_FRAMES * 1152u);
        }
        assert(byte >= r->id3 && byte <= file_size);
    }
}

/* The base check: frames after the tag, or (another writer) at the raw
 * offsets. */
static void test_pvbr_base(void)
{
    mem_file_t f = make_file(4096u, 999u, 999u);
    fill_rekordbox_table(1000u);
    assert(audio_pvbr_base(mem_read, &f, s_pvbr, LEN, 4096u, f.size) == 4096u);
    /* entries already counting the tag */
    for (uint32_t i = 0; i < LEN; i++) s_pvbr[i] += 4096u;
    assert(audio_pvbr_base(mem_read, &f, s_pvbr, LEN, 4096u, f.size) == 0u);
    /* neither: keep the tag */
    for (uint32_t i = 0; i < LEN; i++) s_pvbr[i] += 7u;
    assert(audio_pvbr_base(mem_read, &f, s_pvbr, LEN, 4096u, f.size) == 4096u);
    assert(audio_pvbr_base(mem_read, &f, s_pvbr, LEN, 0u, f.size) == 0u);
    free(f.data);
}

static void test_decide_tolerance(void)
{
    /* Stubborn (v271): Xing 271104, analysis 271133 */
    audio_track_length_inputs_t in = {.span_ms = 271133u, .xing_ms = 271104u, .scan_ms = 271150u};
    audio_track_length_decision_t d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_TRUSTED && d.track_ms == 271133u);
    /* 0.5 % of the scan: ~1360 ms on 272 s */
    in.scan_ms = 271104u + 1300u;
    assert(audio_track_length_decide(&in).xing == AUDIO_XING_TRUSTED);
    in.scan_ms = 271104u + 1400u;
    d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_MISMATCH && d.source == AUDIO_TRACK_LENGTH_XING &&
           d.file_ms == 271104u);
    /* never under 200 ms on a short file */
    in = (audio_track_length_inputs_t){.span_ms = 20000u, .xing_ms = 20000u, .scan_ms = 20190u};
    assert(audio_track_length_decide(&in).xing == AUDIO_XING_TRUSTED);
    /* no scan: the PVBR extrapolation checks it, the header stays */
    in = (audio_track_length_inputs_t){.span_ms = LIE_SPAN_MS, .xing_ms = 181080u, .pvbr_ms = 185200u};
    d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_MISMATCH && d.source == AUDIO_TRACK_LENGTH_XING && d.track_ms == LIE_SPAN_MS);
    /* a VBR average of the first frames cannot check it */
    in = (audio_track_length_inputs_t){.span_ms = LIE_SPAN_MS, .xing_ms = 181080u, .bitrate_ms = 199000u};
    d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_UNVERIFIED && d.file_ms == 181080u && d.track_ms == LIE_SPAN_MS);
    /* no header: the scan, then the table, then the bitrate */
    in = (audio_track_length_inputs_t){.span_ms = LIE_SPAN_MS, .scan_ms = 185000u, .pvbr_ms = 186000u};
    d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_ABSENT && d.source == AUDIO_TRACK_LENGTH_SCAN && d.track_ms == 185000u);
    in = (audio_track_length_inputs_t){.span_ms = LIE_SPAN_MS, .bitrate_ms = 185000u};
    d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_ABSENT && d.source == AUDIO_TRACK_LENGTH_BITRATE && d.track_ms == 185000u);
}

/* No PVBR table, CBR file: the averaged frame headers are compared with the
 * header, which stays. */
static void test_cbr_without_table(void)
{
    mem_file_t f = make_file(0u, NERDY_XING_FRAMES, LIE_REAL_FRAMES);
    audio_mp3_scan_t scan;
    assert(audio_mp3_scan(mem_read, &f, 0u, f.size, 1u, &scan) && scan.first == 0u);
    /* skip the Xing frame, as ae_resolve_track_length does */
    assert(audio_mp3_scan(mem_read, &f, scan.first + 1u, f.size, AUDIO_BITRATE_SCAN_FRAMES, &scan));
    assert(scan.first == LIE_FRAME_BYTES && scan.frames == AUDIO_BITRATE_SCAN_FRAMES && scan.cbr);
    audio_track_length_inputs_t in = {
        .span_ms = LIE_SPAN_MS,
        .xing_ms = audio_mp3_header_duration_ms(f.data, 256u),
        .bitrate_ms = (uint32_t)(((uint64_t)(f.size - scan.first) * scan.ms) / (scan.last_end - scan.first)),
        .bitrate_cbr = scan.cbr,
    };
    assert(abs_diff(in.bitrate_ms, LIE_REAL_MS) <= 2u * LIE_FRAME_MS);   /* + the ID3v1 tag */
    audio_track_length_decision_t d = audio_track_length_decide(&in);
    assert(d.xing == AUDIO_XING_MISMATCH && d.source == AUDIO_TRACK_LENGTH_XING);
    free(f.data);
}

/* Junk between frames and a different frame size: resync, VBR. */
static void test_scan_resync(void)
{
    uint8_t buf[4096] = {0};
    size_t pos = 0;
    put_header(buf + pos); pos += 384u;
    memcpy(buf + pos, "\xFF\xFB junk", 7); pos += 7u;      /* a false sync, no successor */
    buf[pos] = 0xFF; buf[pos + 1] = 0xFB; buf[pos + 2] = 0xA4; buf[pos + 3] = 0x44;  /* 160 kbps */
    pos += 480u;
    put_header(buf + pos); pos += 384u;
    mem_file_t f = {.data = buf, .size = pos};
    audio_mp3_scan_t scan;
    assert(audio_mp3_scan(mem_read, &f, 0u, f.size, 0u, &scan));
    assert(scan.frames == 3u && scan.ms == 72u && !scan.cbr && scan.last_end == pos);
    /* a truncated last frame does not count */
    f.size = pos - 1u;
    assert(audio_mp3_scan(mem_read, &f, 0u, f.size, 0u, &scan) && scan.frames == 2u);
    /* nothing to find */
    memset(buf, 0, sizeof buf);
    f.size = sizeof buf;
    assert(!audio_mp3_scan(mem_read, &f, 0u, f.size, 0u, &scan));
}

/* v302: a DJ Link peer track has no PVBR table. A VBR file: a 64 kbps
 * intro, then 320 kbps, junk to resync over; frame i starts at s_vbr_at[i]
 * (frame 0 = the Xing frame). */
#define VBR_ID3 2000u
#define VBR_FRAMES 4000u                  /* after the Xing frame: 96 s */
#define VBR_JUNK_AFTER 1000u
static size_t s_vbr_at[VBR_FRAMES + 1u];

static mem_file_t make_vbr_file(uint32_t xing_frames, uint32_t real)
{
    mem_file_t f = {0};
    f.size = VBR_ID3 + 384u + (size_t)real * 960u + 5u + 128u;
    f.data = calloc(1, f.size);
    assert(f.data);
    const size_t body = VBR_ID3 - 10u;
    memcpy(f.data, "ID3\x04\x00\x00", 6);
    f.data[6] = (uint8_t)((body >> 21) & 0x7F); f.data[7] = (uint8_t)((body >> 14) & 0x7F);
    f.data[8] = (uint8_t)((body >> 7) & 0x7F);  f.data[9] = (uint8_t)(body & 0x7F);
    put_header(f.data + 100u);            /* a sync pattern inside the tag */
    size_t pos = VBR_ID3;
    s_vbr_at[0] = pos;
    xing_frame(f.data + pos, 0, "Xing", xing_frames);
    pos += 384u;
    for (uint32_t i = 1; i <= real; i++) {
        if (i == VBR_JUNK_AFTER + 1u) {
            memcpy(f.data + pos, "\xFF\xFBjk", 4);  /* a false sync */
            pos += 5u;
        }
        /* MPEG1 L3 48 kHz: 64 kbps = 192 B, 320 kbps = 960 B */
        const bool quiet = i <= real / 4u;
        uint8_t *h = f.data + pos;
        h[0] = 0xFF; h[1] = 0xFB; h[2] = quiet ? 0x54 : 0xE4; h[3] = 0x44;
        s_vbr_at[i] = pos;
        pos += quiet ? 192u : 960u;
    }
    f.size = pos;
    return f;
}

static void test_pvbr_build(void)
{
    mem_file_t f = make_vbr_file(VBR_FRAMES, VBR_FRAMES);
    const uint32_t n = VBR_FRAMES + 1u;
    assert(audio_pvbr_build(mem_read, &f, f.size, s_pvbr, LEN));
    /* The Rekordbox law, counted from the end of the tag. */
    for (uint32_t i = 0; i < LEN; i++) {
        assert(s_pvbr[i] == s_vbr_at[audio_pvbr_entry_frame(i, n, LEN)] - VBR_ID3);
    }
    assert(audio_pvbr_base(mem_read, &f, s_pvbr, LEN, VBR_ID3, f.size) == VBR_ID3);

    /* The engine seeks it like an exported table: every landing is a frame
     * start at its exact time, never after the target. */
    const audio_pvbr_geometry_t geom = {.base = VBR_ID3, .frames = n, .frame_samples = 1152u, .hz = 48000u};
    const uint32_t span_ms = n * 24u;
    uint32_t worst_estimate_ms = 0u;
    for (uint32_t target = 0; target < span_ms; target += 37u) {
        uint32_t byte = 0;
        const uint32_t landing = audio_pvbr_locate(s_pvbr, LEN, span_ms, span_ms, f.size, &geom,
                                                   target, &byte);
        assert(landing % 24u == 0u && landing <= target && target - landing < 600u);
        assert(byte == s_vbr_at[landing / 24u]);
        /* What seek_estimate decoded instead: the frame at its linear byte. */
        const size_t guess = VBR_ID3 + ((uint64_t)target * (f.size - VBR_ID3)) / span_ms;
        uint32_t frame = 0;
        while (frame < VBR_FRAMES && s_vbr_at[frame + 1u] <= guess) frame++;
        worst_estimate_ms = abs_diff(frame * 24u, target) > worst_estimate_ms
                                ? abs_diff(frame * 24u, target) : worst_estimate_ms;
    }
    assert(worst_estimate_ms > 10000u);   /* the bug: seconds off on this file */
    free(f.data);

    /* No frame count: nothing to build (CBR seeks right by estimate). */
    f = make_vbr_file(VBR_FRAMES, VBR_FRAMES);
    memcpy(f.data + VBR_ID3 + 36u, "abcd", 4);
    assert(!audio_pvbr_build(mem_read, &f, f.size, s_pvbr, LEN));
    free(f.data);
    /* Fewer frames than the header says: the last entries are missing. */
    f = make_vbr_file(VBR_FRAMES, VBR_FRAMES - 20u);
    assert(!audio_pvbr_build(mem_read, &f, f.size, s_pvbr, LEN));
    free(f.data);
    /* Not an MP3. */
    uint8_t zero[2048] = {0};
    mem_file_t z = {.data = zero, .size = sizeof zero};
    assert(!audio_pvbr_build(mem_read, &z, z.size, s_pvbr, LEN));
}

/* No way to check the header at load: the lie is caught while decoding, as
 * soon as the decoder passes the length, and EOF pins the exact end. */
static void test_runtime_extension(void)
{
    audio_track_length_inputs_t in = {.span_ms = LIE_SPAN_MS, .xing_ms = 181080u};
    uint32_t track = audio_track_length_decide(&in).track_ms;
    assert(track == LIE_SPAN_MS);
    bool extended = false;
    uint32_t steps = 0;
    for (uint32_t frame = 1; frame <= LIE_REAL_FRAMES; frame++) {
        const uint32_t decoded = frame * LIE_FRAME_MS;
        const uint32_t next = audio_track_length_extend(track, decoded);
        if (next != track) {
            assert(next == decoded + AUDIO_TRACK_EXTEND_STEP_MS);
            extended = true;
            steps++;
        }
        track = next;
        assert(track >= decoded);       /* never behind the decoder */
    }
    assert(steps == 5u);                /* 181.1 -> 185.3 s in 1 s steps */
    assert(track > LIE_REAL_MS);
    track = audio_track_length_at_eof(LIE_SPAN_MS, track, LIE_REAL_MS, extended);
    assert(track == LIE_REAL_MS);
    /* Without an extension the v271 rule holds. */
    assert(audio_track_length_at_eof(LIE_SPAN_MS, LIE_SPAN_MS, LIE_SPAN_MS + 500u, false) == LIE_SPAN_MS);
    assert(audio_track_length_at_eof(LIE_SPAN_MS, LIE_SPAN_MS, LIE_REAL_MS, false) == LIE_REAL_MS);
}

static void test_decoder_length_authority(void)
{
    audio_track_decode_length_t m;
    audio_track_decode_length_begin(&m);
    assert(audio_track_decode_length_ms(&m) == 0u);
    audio_track_decode_length_add(&m, 44100u, 44100u);
    assert(audio_track_decode_length_ms(&m) == 1000u);
    audio_track_decode_length_add(&m, 22050u, 44100u);
    assert(audio_track_decode_length_ms(&m) == 1500u);
    audio_track_decode_length_invalidate(&m); /* seek, loop or read fault */
    audio_track_decode_length_add(&m, 44100u, 44100u);
    assert(audio_track_decode_length_ms(&m) == 0u);
    audio_track_decode_length_begin(&m); /* actual rewind, not estimated seek */
    audio_track_decode_length_add(&m, 48000u, 48000u);
    assert(audio_track_decode_length_ms(&m) == 1000u);
    audio_track_decode_length_add(&m, 44100u, 44100u);
    assert(audio_track_decode_length_ms(&m) == 0u); /* mixed sample rate */
    audio_track_decode_length_begin(&m);
    audio_track_decode_length_add(&m, 1u, 0u);
    assert(audio_track_decode_length_ms(&m) == 0u);
    m = (audio_track_decode_length_t){.frames = UINT64_MAX, .hz = 44100u, .anchored = true};
    assert(audio_track_decode_length_ms(&m) == 0u);
    audio_track_decode_length_add(&m, 1u, 44100u);
    assert(!m.anchored);
}

int main(void)
{
    test_decoder_length_authority();
    test_header_duration();
    test_hw_table_extrapolates_to_eof();
    test_resolve();
    test_file_five_percent_longer();
    test_locate_matches_v263_inside_span();
    test_xing_kept_at_load();
    test_real_tables_entry_law();
    test_california_cue();
    test_pvbr_base();
    test_decide_tolerance();
    test_cbr_without_table();
    test_scan_resync();
    test_runtime_extension();
    test_pvbr_build();
    printf("audio_track_length: all tests passed\n");
    return 0;
}
