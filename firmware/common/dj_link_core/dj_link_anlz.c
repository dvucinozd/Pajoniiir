#include "dj_link_anlz.h"

#include <string.h>

/* Section layouts per the DJ Link analysis (anlz.html); the library's
 * rekordbox_anlz.c parser walks them by (tag, header size, section size). */
#define TAG_PMAI 0x504d4149u
#define TAG_PPTH 0x50505448u
#define TAG_PQTZ 0x5051545au
#define TAG_PWAV 0x50574156u
#define TAG_PWV3 0x50575633u
#define TAG_PWV4 0x50575634u
#define TAG_PCOB 0x50434f42u
#define TAG_PCPT 0x50435054u

#define PMAI_HEAD 28u
#define PPTH_HEAD 16u
#define PQTZ_HEAD 24u
#define PWAV_HEAD 20u
#define PWV3_HEAD 24u
#define PWV4_HEAD 24u
#define PCOB_HEAD 24u
#define PCPT_HEAD 28u
#define PCPT_ENTRY 56u
#define PCOB_LIST_MEMORY 0u
#define PCOB_LIST_HOT 1u
#define PATH_MAX_CHARS 255u

static uint16_t rd16le(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
}

static void wr32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
}

/* tag, header size, section size, then up to three u32 header fields. */
static bool put_head(dj_link_anlz_write_fn write, void *ctx, uint32_t tag, uint32_t head,
                     uint32_t total, const uint32_t *fields, size_t count)
{
    uint8_t b[PMAI_HEAD];
    memset(b, 0, sizeof(b));
    wr32(&b[0], tag);
    wr32(&b[4], head);
    wr32(&b[8], total);
    for (size_t i = 0; i < count; i++) {
        wr32(&b[12u + 4u * i], fields[i]);
    }
    return write(ctx, b, head);
}

dj_link_anlz_commit_t dj_link_anlz_commit(const dj_link_anlz_answers_t *a)
{
    dj_link_anlz_commit_t c = { false, false, false, false };
    const bool any = a->wave_len > 0u || a->beats > 0u || a->cues > 0u;
    const bool complete = a->wave_answered && a->grid_answered && a->cues_answered;
    if (!any || (a->dat_cached && !complete)) {
        return c; /* nothing to write, or keep the cached analysis whole */
    }
    c.write_dat = true;
    c.cue_lists = a->cues_answered;
    c.write_ext = a->wave_len > 0u;
    c.drop_ext = !c.write_ext;
    return c;
}

size_t dj_link_anlz_cue_count(const uint8_t *blob, size_t len)
{
    size_t n = 0u;
    size_t at = 0u;
    while (blob && n < DJ_LINK_ANLZ_CUES_MAX_ENTRIES && len - at >= DJ_LINK_ANLZ_CUE_ENTRY_MIN) {
        const uint32_t entry = rd32le(&blob[at]);
        if (entry < DJ_LINK_ANLZ_CUE_ENTRY_MIN || entry > len - at) {
            break;
        }
        at += entry;
        n++;
    }
    return n;
}

bool dj_link_anlz_cue(const uint8_t *blob, size_t len, size_t i, dj_link_anlz_cue_t *out)
{
    if (!out || i >= dj_link_anlz_cue_count(blob, len)) {
        return false;
    }
    size_t at = 0u;
    for (size_t k = 0; k < i; k++) {
        at += rd32le(&blob[at]);
    }
    const uint8_t *e = &blob[at];
    const uint16_t number = rd16le(&e[0x04]);
    const uint32_t end = rd32le(&e[0x20]);
    out->hot_cue = number >= 1u && number <= 8u ? (uint8_t)number : 0u;
    out->time_ms = rd32le(&e[0x0c]);
    out->loop = rd16le(&e[0x06]) == 2u && end != 0xffffffffu && end > out->time_ms;
    out->loop_end_ms = out->loop ? end : 0u;
    return true;
}

/* Cues of one PCOB list: hot cues for PCOB_LIST_HOT, memory cues else. */
static uint32_t pcob_count(const uint8_t *cues, size_t cues_len, uint32_t list)
{
    uint32_t n = 0u;
    const size_t total = dj_link_anlz_cue_count(cues, cues_len);
    for (size_t i = 0; i < total; i++) {
        dj_link_anlz_cue_t q;
        (void)dj_link_anlz_cue(cues, cues_len, i, &q);
        n += (q.hot_cue != 0u) == (list == PCOB_LIST_HOT) ? 1u : 0u;
    }
    return n;
}

/* PCOB + its PCPT entries (Deep Symmetry ANLZ layout, what
 * rekordbox_anlz.c read_pcob_list walks). */
static bool put_pcob(dj_link_anlz_write_fn write, void *ctx, const uint8_t *cues,
                     size_t cues_len, uint32_t list)
{
    const uint32_t count = pcob_count(cues, cues_len, list);
    /* type, then u16 0 + u16 count, then memory count (-1, as rekordbox). */
    const uint32_t pcob[] = { list, count, 0xffffffffu };
    if (!put_head(write, ctx, TAG_PCOB, PCOB_HEAD, PCOB_HEAD + PCPT_ENTRY * count, pcob, 3u)) {
        return false;
    }
    const size_t total = dj_link_anlz_cue_count(cues, cues_len);
    for (size_t i = 0; i < total; i++) {
        dj_link_anlz_cue_t q;
        (void)dj_link_anlz_cue(cues, cues_len, i, &q);
        if ((q.hot_cue != 0u) != (list == PCOB_LIST_HOT)) {
            continue;
        }
        uint8_t e[PCPT_ENTRY];
        memset(e, 0, sizeof(e));
        wr32(&e[0x00], TAG_PCPT);
        wr32(&e[0x04], PCPT_HEAD);
        wr32(&e[0x08], PCPT_ENTRY);
        wr32(&e[0x0c], q.hot_cue);
        wr32(&e[0x10], 4u);                 /* status: enabled */
        wr32(&e[0x14], 0x00100000u);
        wr16(&e[0x18], 0xffffu);            /* order first / last */
        wr16(&e[0x1a], 0xffffu);
        e[0x1c] = q.loop ? 2u : 1u;
        wr16(&e[0x1e], 1000u);
        wr32(&e[0x20], q.time_ms);
        wr32(&e[0x24], q.loop ? q.loop_end_ms : 0xffffffffu);
        if (!write(ctx, e, sizeof(e))) {
            return false;
        }
    }
    return true;
}

size_t dj_link_anlz_grid_count(const uint8_t *blob, size_t len)
{
    if (!blob || len < DJ_LINK_ANLZ_GRID_HEAD + DJ_LINK_ANLZ_GRID_ENTRY) {
        return 0;
    }
    size_t n = (len - DJ_LINK_ANLZ_GRID_HEAD) / DJ_LINK_ANLZ_GRID_ENTRY;
    return n < DJ_LINK_ANLZ_GRID_MAX_BEATS ? n : DJ_LINK_ANLZ_GRID_MAX_BEATS;
}

bool dj_link_anlz_grid_beat(const uint8_t *blob, size_t len, size_t i,
                            dj_link_anlz_beat_t *out)
{
    if (!out || i >= dj_link_anlz_grid_count(blob, len)) {
        return false;
    }
    const uint8_t *e = &blob[DJ_LINK_ANLZ_GRID_HEAD + i * DJ_LINK_ANLZ_GRID_ENTRY];
    out->beat_in_bar = rd16le(&e[0]);
    out->bpm100 = rd16le(&e[2]);
    out->time_ms = rd32le(&e[4]);
    return true;
}

bool dj_link_anlz_preview(const uint8_t *detail, size_t len,
                          uint8_t out[DJ_LINK_ANLZ_PREVIEW_LEN])
{
    if(!out)return false;
    memset(out, 0, DJ_LINK_ANLZ_PREVIEW_LEN);
    if (!detail || len == 0u) {
        return false;
    }
    for (size_t c = 0; c < DJ_LINK_ANLZ_PREVIEW_LEN; c++) {
        size_t from = c * len / DJ_LINK_ANLZ_PREVIEW_LEN;
        size_t to = (c + 1u) * len / DJ_LINK_ANLZ_PREVIEW_LEN;
        if (to <= from) {
            to = from + 1u; /* fewer entries than columns */
        }
        uint8_t best = detail[from];
        for (size_t i = from + 1u; i < to; i++) {
            if ((detail[i] & 0x1fu) > (best & 0x1fu)) {
                best = detail[i];
            }
        }
        out[c] = best;
    }
    return true;
}

bool dj_link_anlz_write_dat(dj_link_anlz_write_fn write, void *ctx, const char *audio_path,
                            const uint8_t *grid, size_t grid_len,
                            const uint8_t *detail, size_t detail_len,
                            const uint8_t *cues, size_t cues_len)
{
    if (!write || !audio_path || !audio_path[0]) {
        return false;
    }
    size_t chars = strlen(audio_path);
    if (chars > PATH_MAX_CHARS) {
        return false;
    }
    const uint32_t path_bytes = (uint32_t)(chars + 1u) * 2u; /* UTF-16BE + NUL */
    const uint32_t beats = (uint32_t)dj_link_anlz_grid_count(grid, grid_len);
    const bool wave = detail && detail_len > 0u;
    const uint32_t cue_bytes =
        cues ? 2u * PCOB_HEAD + PCPT_ENTRY * (uint32_t)dj_link_anlz_cue_count(cues, cues_len)
             : 0u;
    const uint32_t total = PMAI_HEAD + PPTH_HEAD + path_bytes +
                           (beats ? PQTZ_HEAD + 8u * beats : 0u) +
                           (wave ? PWAV_HEAD + DJ_LINK_ANLZ_PREVIEW_LEN : 0u) + cue_bytes;

    const uint32_t pmai[] = { 1u, 0x00010000u, 0x00010000u };
    if (!put_head(write, ctx, TAG_PMAI, PMAI_HEAD, total, pmai, 3u)) {
        return false;
    }
    const uint32_t ppth[] = { path_bytes };
    if (!put_head(write, ctx, TAG_PPTH, PPTH_HEAD, PPTH_HEAD + path_bytes, ppth, 1u)) {
        return false;
    }
    for (size_t i = 0; i <= chars; i++) {
        uint8_t ch[2] = { 0, 0 };
        unsigned char c = i < chars ? (unsigned char)audio_path[i] : 0u;
        ch[1] = c < 0x80u ? c : '?';
        if (!write(ctx, ch, sizeof(ch))) {
            return false;
        }
    }
    if (beats) {
        const uint32_t pqtz[] = { 0u, 0x00080000u, beats };
        if (!put_head(write, ctx, TAG_PQTZ, PQTZ_HEAD, PQTZ_HEAD + 8u * beats, pqtz, 3u)) {
            return false;
        }
        for (uint32_t i = 0; i < beats; i++) {
            dj_link_anlz_beat_t b;
            uint8_t e[8];
            (void)dj_link_anlz_grid_beat(grid, grid_len, i, &b);
            wr16(&e[0], b.beat_in_bar);
            wr16(&e[2], b.bpm100);
            wr32(&e[4], b.time_ms);
            if (!write(ctx, e, sizeof(e))) {
                return false;
            }
        }
    }
    if (wave) {
        uint8_t preview[DJ_LINK_ANLZ_PREVIEW_LEN];
        const uint32_t pwav[] = { DJ_LINK_ANLZ_PREVIEW_LEN, 0x00010000u };
        (void)dj_link_anlz_preview(detail, detail_len, preview);
        if (!put_head(write, ctx, TAG_PWAV, PWAV_HEAD, PWAV_HEAD + DJ_LINK_ANLZ_PREVIEW_LEN,
                      pwav, 2u) ||
            !write(ctx, preview, sizeof(preview))) {
            return false;
        }
    }
    if (cues && (!put_pcob(write, ctx, cues, cues_len, PCOB_LIST_HOT) ||
                 !put_pcob(write, ctx, cues, cues_len, PCOB_LIST_MEMORY))) {
        return false;
    }
    return true;
}

bool dj_link_anlz_write_ext(dj_link_anlz_write_fn write, void *ctx,
                            const uint8_t *detail, size_t detail_len,
                            const uint8_t *color, size_t color_len)
{
    if (!write || !detail || detail_len == 0u) {
        return false;
    }
    const uint32_t n = (uint32_t)(detail_len < DJ_LINK_ANLZ_WAVE_MAX
                                      ? detail_len : DJ_LINK_ANLZ_WAVE_MAX);
    uint32_t c = color ? (uint32_t)(color_len < DJ_LINK_ANLZ_COLOR_MAX
                                        ? color_len : DJ_LINK_ANLZ_COLOR_MAX) : 0u;
    c -= c % DJ_LINK_ANLZ_COLOR_ENTRY;
    const uint32_t pwv4_bytes = c ? PWV4_HEAD + c : 0u;
    const uint32_t pmai[] = { 1u, 0x00010000u, 0x00010000u };
    const uint32_t pwv3[] = { 1u, n, 0x00960000u }; /* entry bytes, entries, 150/s */
    const uint32_t pwv4[] = { DJ_LINK_ANLZ_COLOR_ENTRY, c / DJ_LINK_ANLZ_COLOR_ENTRY, 0u };
    return put_head(write, ctx, TAG_PMAI, PMAI_HEAD,
                    PMAI_HEAD + PWV3_HEAD + n + pwv4_bytes, pmai, 3u) &&
           put_head(write, ctx, TAG_PWV3, PWV3_HEAD, PWV3_HEAD + n, pwv3, 3u) &&
           write(ctx, detail, n) &&
           (c == 0u || (put_head(write, ctx, TAG_PWV4, PWV4_HEAD, PWV4_HEAD + c, pwv4, 3u) &&
                        write(ctx, color, c)));
}

bool dj_link_anlz_color_entries(const uint8_t *blob, size_t blob_len,
                                const uint8_t **entries, size_t *len)
{
    if (!blob || !entries || !len || blob_len < 4u + 20u) {
        return false;
    }
    const uint32_t sec_len = (uint32_t)blob[0] | ((uint32_t)blob[1] << 8) |
                             ((uint32_t)blob[2] << 16) | ((uint32_t)blob[3] << 24);
    const uint8_t *s = &blob[4];
    const size_t avail = blob_len - 4u;
    if (rd32be(&s[0]) != TAG_PWV4) {
        return false;
    }
    const uint32_t head = rd32be(&s[4]);
    const uint32_t total = rd32be(&s[8]);
    if (head < 20u || total < head || total != sec_len || total > avail ||
        rd32be(&s[12]) != DJ_LINK_ANLZ_COLOR_ENTRY) {
        return false;
    }
    size_t n = total - head;
    if (n > DJ_LINK_ANLZ_COLOR_MAX) {
        n = DJ_LINK_ANLZ_COLOR_MAX;
    }
    n -= n % DJ_LINK_ANLZ_COLOR_ENTRY;
    if (n == 0u) {
        return false;
    }
    *entries = &s[head];
    *len = n;
    return true;
}
