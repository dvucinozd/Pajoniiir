/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include "audio_track_length.h"

#include <stdbool.h>
#include <string.h>

#include "audio_seek_skip.h"

static uint32_t rd_be32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

size_t audio_id3v2_size(const uint8_t *buf, size_t len)
{
    if (!buf || len < 10u || buf[0] != 'I' || buf[1] != 'D' || buf[2] != '3') return 0u;
    for (int i = 6; i < 10; i++) {
        if (buf[i] & 0x80u) return 0u;
    }
    size_t size = ((size_t)buf[6] << 21) | ((size_t)buf[7] << 14) |
                  ((size_t)buf[8] << 7) | (size_t)buf[9];
    /* footer flag: a second 10-byte block closes the tag */
    return 10u + size + ((buf[5] & 0x10u) ? 10u : 0u);
}

typedef struct {
    uint32_t hz;
    uint32_t samples_per_frame;
    size_t side_info;
    size_t frame_bytes;           /* 0 for free-format */
} mpeg_l3_header_t;

static bool parse_l3_header(const uint8_t *h, mpeg_l3_header_t *out)
{
    if (h[0] != 0xFFu || (h[1] & 0xE0u) != 0xE0u) return false;
    const uint8_t version = (h[1] >> 3) & 3u;   /* 3 MPEG1, 2 MPEG2, 0 MPEG2.5 */
    const uint8_t layer = (h[1] >> 1) & 3u;     /* 1 = Layer III */
    const uint8_t bitrate_idx = h[2] >> 4;
    const uint8_t rate_idx = (h[2] >> 2) & 3u;
    if (version == 1u || layer != 1u || bitrate_idx == 15u || rate_idx == 3u) return false;
    static const uint32_t k_rates[3] = {44100u, 48000u, 32000u};
    const bool mpeg1 = version == 3u;
    const bool mono = (h[3] >> 6) == 3u;
    out->hz = k_rates[rate_idx] >> (mpeg1 ? 0 : version == 2u ? 1 : 2);
    out->samples_per_frame = mpeg1 ? 1152u : 576u;
    out->side_info = mpeg1 ? (mono ? 17u : 32u) : (mono ? 9u : 17u);
    static const uint16_t k_kbps[2][15] = {
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160},
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320},
    };
    const uint32_t kbps = k_kbps[mpeg1 ? 1 : 0][bitrate_idx];
    const uint32_t pad = (h[2] >> 1) & 1u;
    out->frame_bytes = kbps ? (size_t)((mpeg1 ? 144000u : 72000u) * kbps / out->hz + pad) : 0u;
    return true;
}

bool audio_mp3_first_frame(const uint8_t *buf, size_t len, audio_mp3_first_frame_t *out)
{
    if (!buf || !out) return false;
    for (size_t i = 0; i + 4u <= len; i++) {
        mpeg_l3_header_t hdr;
        if (!parse_l3_header(buf + i, &hdr)) continue;
        uint32_t frames = 0u;
        const size_t xing = i + 4u + hdr.side_info;
        const size_t vbri = i + 4u + 32u;
        if (xing + 12u <= len &&
            (memcmp(buf + xing, "Xing", 4) == 0 || memcmp(buf + xing, "Info", 4) == 0) &&
            (rd_be32(buf + xing + 4u) & 1u)) {
            frames = rd_be32(buf + xing + 8u);
        } else if (vbri + 18u <= len && memcmp(buf + vbri, "VBRI", 4) == 0) {
            frames = rd_be32(buf + vbri + 14u);
        }
        /* Only the first frame may carry the header. */
        *out = (audio_mp3_first_frame_t){
            .hz = hdr.hz,
            .frame_samples = hdr.samples_per_frame,
            .count = frames,
        };
        return true;
    }
    return false;
}

uint32_t audio_mp3_header_duration_ms(const uint8_t *buf, size_t len)
{
    audio_mp3_first_frame_t ff;
    if (!audio_mp3_first_frame(buf, len, &ff)) return 0u;
    return (uint32_t)(((uint64_t)ff.count * ff.frame_samples * 1000u) / ff.hz);
}

uint32_t audio_pvbr_frames_from_span(uint32_t span_ms, uint32_t hz, uint32_t frame_samples)
{
    if (hz == 0u || frame_samples == 0u) return 0u;
    const uint64_t per = (uint64_t)frame_samples * 1000u;
    return (uint32_t)(((uint64_t)span_ms * hz + per / 2u) / per);
}

uint32_t audio_pvbr_entry_frame(uint32_t idx, uint32_t frames, uint32_t len)
{
    if (len == 0u) return 0u;
    const uint32_t f = (uint32_t)(((uint64_t)(idx + 1u) * frames) / len);
    return f > AUDIO_PVBR_ENTRY_FRAME_LAG ? f - AUDIO_PVBR_ENTRY_FRAME_LAG : 0u;
}

static bool geom_exact(const audio_pvbr_geometry_t *geom)
{
    return geom && geom->frames && geom->hz && geom->frame_samples;
}

static uint32_t frame_ms(const audio_pvbr_geometry_t *geom, uint32_t frame)
{
    return (uint32_t)(((uint64_t)frame * geom->frame_samples * 1000u) / geom->hz);
}

static uint32_t entry_ms(uint32_t idx, uint32_t span_ms, uint32_t len)
{
    return (uint32_t)(((uint64_t)idx * span_ms) / len);
}

/* Byte rate over the last AUDIO_PVBR_TAIL_ENTRIES of the table. */
static bool tail_rate(const uint32_t *pvbr, uint32_t len, uint32_t span_ms,
                      uint32_t *bytes, uint32_t *ms)
{
    if (!pvbr || len < 2u || span_ms == 0u) return false;
    const uint32_t last = len - 1u;
    const uint32_t first = last > AUDIO_PVBR_TAIL_ENTRIES ? last - AUDIO_PVBR_TAIL_ENTRIES : 0u;
    if (pvbr[last] <= pvbr[first]) return false;
    *bytes = pvbr[last] - pvbr[first];
    *ms = entry_ms(last, span_ms, len) - entry_ms(first, span_ms, len);
    return *ms > 0u;
}

uint32_t audio_pvbr_entry_time_ms(const audio_pvbr_geometry_t *geom, uint32_t idx,
                                  uint32_t len, uint32_t span_ms)
{
    if (!geom_exact(geom)) return entry_ms(idx, span_ms, len);
    return frame_ms(geom, audio_pvbr_entry_frame(idx, geom->frames, len));
}

uint32_t audio_pvbr_extrapolate_end_ms(const uint32_t *pvbr, uint32_t len,
                                       uint32_t span_ms, size_t base, size_t file_size)
{
    uint32_t bytes, ms;
    if (!tail_rate(pvbr, len, span_ms, &bytes, &ms)) return 0u;
    const uint32_t last = len - 1u;
    const uint32_t last_ms = entry_ms(last, span_ms, len);
    const size_t last_byte = base + pvbr[last];
    if (file_size <= last_byte) return last_ms;
    return last_ms + (uint32_t)(((uint64_t)(file_size - last_byte) * ms) / bytes);
}

uint32_t audio_bitrate_duration_ms(size_t audio_bytes, uint32_t bitrate_kbps)
{
    if (bitrate_kbps == 0u) return 0u;
    return (uint32_t)(((uint64_t)audio_bytes * 8u) / bitrate_kbps);
}

uint32_t audio_track_length_resolve(uint32_t span_ms, uint32_t file_ms)
{
    if (file_ms == 0u) return span_ms;
    if (span_ms == 0u) return file_ms;
    return file_ms > span_ms + AUDIO_TRACK_LENGTH_TOLERANCE_MS ? file_ms : span_ms;
}

/* Last entry at or before target_frame, -1 when the first is past it. */
static int32_t entry_at_or_before(const audio_pvbr_geometry_t *geom, uint32_t len,
                                  uint32_t target_frame)
{
    int32_t lo = -1, hi = (int32_t)len - 1;
    while (lo < hi) {
        const int32_t mid = lo + (hi - lo + 1) / 2;
        if (audio_pvbr_entry_frame((uint32_t)mid, geom->frames, len) <= target_frame) lo = mid;
        else hi = mid - 1;
    }
    return lo;
}

uint32_t audio_pvbr_locate(const uint32_t *pvbr, uint32_t len, uint32_t span_ms,
                           uint32_t track_ms, size_t file_size,
                           const audio_pvbr_geometry_t *geom,
                           uint32_t target_ms, uint32_t *byte)
{
    uint32_t landing = 0u;
    size_t at = 0u;
    const size_t base = geom ? geom->base : 0u;
    if (track_ms > 0u && target_ms > track_ms) target_ms = track_ms;
    if (pvbr && len > 0u) {
        const uint32_t last = len - 1u;
        const uint32_t last_ms = audio_pvbr_entry_time_ms(geom, last, len, span_ms);
        uint32_t bytes, ms;
        if (target_ms > span_ms && target_ms > last_ms + AUDIO_PVBR_TAIL_LEAD_MS &&
            tail_rate(pvbr, len, span_ms, &bytes, &ms)) {
            landing = target_ms - AUDIO_PVBR_TAIL_LEAD_MS;
            /* v272: the track length is now measured (tail scan), so the last
             * entry and the end of the file bound the tail; the table's own
             * byte rate is only the fallback. */
            if (track_ms > last_ms && file_size > base + pvbr[last]) {
                bytes = (uint32_t)(file_size - base - pvbr[last]);
                ms = track_ms - last_ms;
            }
            at = base + pvbr[last] + (uint32_t)(((uint64_t)(landing - last_ms) * bytes) / ms);
        } else if (geom_exact(geom)) {
            const uint32_t target_frame = (uint32_t)(((uint64_t)target_ms * geom->hz) /
                                                     ((uint64_t)geom->frame_samples * 1000u));
            const int32_t idx = target_frame >= AUDIO_PVBR_RESYNC_LEAD_FRAMES
                ? entry_at_or_before(geom, len, target_frame - AUDIO_PVBR_RESYNC_LEAD_FRAMES)
                : -1;
            if (idx < 0) {
                at = base;                /* the Xing frame, or the first audio */
                landing = 0u;
            } else {
                at = base + pvbr[idx];
                landing = audio_pvbr_entry_time_ms(geom, (uint32_t)idx, len, span_ms);
            }
        } else {
            const uint32_t idx = audio_pvbr_index(target_ms, span_ms, len);
            at = base + pvbr[idx];
            landing = audio_pvbr_entry_ms(idx, span_ms, len, target_ms);
        }
    }
    if (at > file_size) at = file_size;
    if (byte) *byte = (uint32_t)at;
    return landing;
}

/* Next frame start at or after pos: a header whose successor is a header of
 * the same stream (or the end), so sync bytes inside tags are skipped. */
static bool scan_sync(audio_track_read_fn read, void *ctx, size_t pos, size_t end,
                      uint32_t hz, size_t *found, mpeg_l3_header_t *hdr)
{
    uint8_t buf[512];
    while (pos + 4u <= end) {
        size_t want = end - pos < sizeof buf ? end - pos : sizeof buf;
        size_t got = read(ctx, pos, buf, want);
        if (got < 4u) return false;
        for (size_t i = 0; i + 4u <= got; i++) {
            mpeg_l3_header_t h;
            if (!parse_l3_header(buf + i, &h) || h.frame_bytes == 0u || (hz && h.hz != hz)) continue;
            const size_t at = pos + i;
            const size_t next = at + h.frame_bytes;
            if (next > end) continue;
            if (next + 4u <= end) {
                uint8_t nb[4];
                mpeg_l3_header_t nh;
                if (read(ctx, next, nb, 4u) != 4u || !parse_l3_header(nb, &nh) || nh.hz != h.hz) continue;
            }
            *found = at;
            *hdr = h;
            return true;
        }
        pos += got - 3u;
    }
    return false;
}

bool audio_mp3_scan(audio_track_read_fn read, void *ctx, size_t start, size_t end,
                    uint32_t max_frames, audio_mp3_scan_t *out)
{
    if (!read || !out) return false;
    *out = (audio_mp3_scan_t){0};
    size_t pos = start;
    uint64_t samples = 0u;
    uint8_t hb[4];
    size_t first_bytes = 0u;
    out->cbr = true;
    while (pos + 4u <= end && (max_frames == 0u || out->frames < max_frames)) {
        mpeg_l3_header_t h;
        if (read(ctx, pos, hb, 4u) != 4u) return false;
        if (!parse_l3_header(hb, &h) || h.frame_bytes == 0u || (out->hz && h.hz != out->hz)) {
            size_t at;
            if (!scan_sync(read, ctx, pos + 1u, end, out->hz, &at, &h)) break;
            pos = at;
        }
        if (pos + h.frame_bytes > end) break;   /* truncated last frame */
        if (out->frames == 0u) {
            out->first = pos;
            first_bytes = h.frame_bytes;
        } else if (h.frame_bytes + 1u < first_bytes || h.frame_bytes > first_bytes + 1u) {
            out->cbr = false;             /* padding moves a frame by one byte */
        }
        out->hz = h.hz;
        samples += h.samples_per_frame;
        out->frames++;
        pos += h.frame_bytes;
        out->last_end = pos;
    }
    if (out->frames == 0u || out->hz == 0u) return false;
    out->ms = (uint32_t)((samples * 1000u) / out->hz);
    return true;
}

static bool index_padding(audio_track_read_fn read, void *ctx, size_t pos, size_t end)
{
    if (end - pos > 4096u) return false;
    uint8_t buf[128];
    while (pos < end) {
        size_t n = end - pos < sizeof(buf) ? end - pos : sizeof(buf);
        if (read(ctx, pos, buf, n) != n) return false;
        if (end - pos == 128u && memcmp(buf, "TAG", 3u) == 0) return true;
        for (size_t i = 0u; i < n; ++i) if (buf[i] != 0u) return false;
        pos += n;
    }
    return true;
}

bool audio_mp3_index_build(audio_track_read_fn read, void *ctx, size_t file_size,
                           audio_mp3_index_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!read || file_size < 4u || file_size > AUDIO_MP3_INDEX_MAX_BYTES) return false;
    uint8_t head[10];
    size_t n = file_size < sizeof(head) ? file_size : sizeof(head);
    if (read(ctx, 0u, head, n) != n) return false;
    size_t pos = audio_id3v2_size(head, n);
    if (n >= 3u && memcmp(head, "ID3", 3u) == 0 && pos == 0u) return false;
    if (pos >= file_size) return false;
    out->stride = 8u;
    out->file_size = file_size;
    while (pos + 4u <= file_size) {
        uint8_t hb[4];
        mpeg_l3_header_t h;
        if (read(ctx, pos, hb, sizeof(hb)) != sizeof(hb)) return false;
        if (!parse_l3_header(hb, &h)) {
            if (!out->frames || !index_padding(read, ctx, pos, file_size)) return false;
            pos = file_size;
            break;
        }
        if (!h.frame_bytes || h.frame_bytes > file_size - pos ||
            (out->hz && (h.hz != out->hz || h.samples_per_frame != out->frame_samples)) ||
            out->frames >= AUDIO_MP3_INDEX_MAX_FRAMES) return false;
        out->hz = h.hz;
        out->frame_samples = h.samples_per_frame;
        if (out->frames % out->stride == 0u) {
            if (out->count == AUDIO_MP3_INDEX_SLOTS) {
                for (uint32_t i = 0u; i < AUDIO_MP3_INDEX_SLOTS / 2u; ++i)
                    out->entries[i] = out->entries[i * 2u];
                out->count /= 2u;
                out->stride *= 2u;
            }
            if (out->frames % out->stride == 0u)
                out->entries[out->count++] = (audio_mp3_index_entry_t){(uint32_t)pos, out->frames};
        }
        out->frames++;
        pos += h.frame_bytes;
        out->audio_end = pos;
    }
    if (!out->frames || (pos < file_size && !index_padding(read, ctx, pos, file_size))) return false;
    uint64_t ms = (uint64_t)out->frames * out->frame_samples * 1000u / out->hz;
    if (!ms || ms > UINT32_MAX) return false;
    out->duration_ms = (uint32_t)ms;
    out->complete = true;
    return true;
}

bool audio_mp3_index_locate(const audio_mp3_index_t *index,
                            audio_track_read_fn read, void *ctx, uint32_t target_ms,
                            uint32_t *byte, uint32_t *skip_frames)
{
    if (!index || !index->complete || !index->count || index->count > AUDIO_MP3_INDEX_SLOTS ||
        !index->stride || !index->frames || !index->hz || !index->audio_end ||
        index->audio_end > index->file_size ||
        !index->frame_samples || !read || !byte || !skip_frames) return false;
    uint64_t sample = (uint64_t)target_ms * index->hz / 1000u;
    uint64_t total = (uint64_t)index->frames * index->frame_samples;
    if (sample >= total) sample = total - 1u;
    uint32_t frame = (uint32_t)(sample / index->frame_samples);
    frame = frame > AUDIO_PVBR_RESYNC_LEAD_FRAMES ? frame - AUDIO_PVBR_RESYNC_LEAD_FRAMES : 0u;
    uint32_t lo = 0u, hi = index->count;
    while (lo + 1u < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if (index->entries[mid].frame <= frame) lo = mid;
        else hi = mid;
    }
    size_t pos = index->entries[lo].byte;
    if (index->entries[lo].frame > frame || frame - index->entries[lo].frame >= index->stride ||
        pos >= index->file_size) return false;
    for (uint32_t at = index->entries[lo].frame; at < frame; ++at) {
        uint8_t hb[4];
        mpeg_l3_header_t h;
        if (pos + 4u > index->file_size || read(ctx, pos, hb, sizeof(hb)) != sizeof(hb) ||
            !parse_l3_header(hb, &h) || !h.frame_bytes || h.hz != index->hz ||
            h.samples_per_frame != index->frame_samples || h.frame_bytes > index->file_size - pos)
            return false;
        pos += h.frame_bytes;
    }
    *byte = (uint32_t)pos;
    *skip_frames = (uint32_t)(sample - (uint64_t)frame * index->frame_samples);
    return true;
}

bool audio_mp3_frame_at(audio_track_read_fn read, void *ctx, size_t pos, size_t end)
{
    uint8_t hb[4];
    mpeg_l3_header_t h, nh;
    if (!read || pos + 4u > end || read(ctx, pos, hb, 4u) != 4u) return false;
    if (!parse_l3_header(hb, &h) || h.frame_bytes == 0u) return false;
    const size_t next = pos + h.frame_bytes;
    if (next + 4u > end) return next <= end;
    return read(ctx, next, hb, 4u) == 4u && parse_l3_header(hb, &nh) && nh.hz == h.hz;
}

size_t audio_pvbr_base(audio_track_read_fn read, void *ctx, const uint32_t *pvbr,
                       uint32_t len, size_t id3_size, size_t file_size)
{
    if (!pvbr || len < 4u || id3_size == 0u) return id3_size;
    /* Two entries away from the ends, where the table cannot be degenerate. */
    const uint32_t probe[2] = {len / 4u, len / 2u};
    bool shifted = true, raw = true;
    for (int i = 0; i < 2; i++) {
        shifted = shifted && audio_mp3_frame_at(read, ctx, id3_size + pvbr[probe[i]], file_size);
        raw = raw && audio_mp3_frame_at(read, ctx, pvbr[probe[i]], file_size);
    }
    return !shifted && raw ? 0u : id3_size;
}

bool audio_pvbr_build(audio_track_read_fn read, void *ctx, size_t file_size,
                      uint32_t *pvbr, uint32_t len)
{
    if (!read || !pvbr || len == 0u) return false;
    /* The engine's reading of the start of the file (ae_resolve_track_length). */
    uint8_t head[1024];
    size_t got = read(ctx, 0u, head, file_size < 10u ? file_size : 10u);
    size_t base = audio_id3v2_size(head, got);
    if (base >= file_size) base = 0u;
    const size_t want = file_size - base;
    got = read(ctx, base, head, want < sizeof head ? want : sizeof head);
    audio_mp3_first_frame_t ff;
    if (!audio_mp3_first_frame(head, got, &ff) || ff.count == 0u) return false;
    const uint32_t frames = ff.count + 1u;

    /* The frames as audio_mp3_scan walks them: resync over junk, one rate. */
    size_t pos = base;
    uint32_t hz = 0u, frame = 0u, idx = 0u;
    uint8_t hb[4];
    while (idx < len && pos + 4u <= file_size) {
        mpeg_l3_header_t h;
        if (read(ctx, pos, hb, 4u) != 4u) return false;
        if (!parse_l3_header(hb, &h) || h.frame_bytes == 0u || (hz && h.hz != hz)) {
            size_t at;
            if (!scan_sync(read, ctx, pos + 1u, file_size, hz, &at, &h)) break;
            pos = at;
        }
        if (pos + h.frame_bytes > file_size) break;   /* truncated last frame */
        hz = h.hz;
        while (idx < len && audio_pvbr_entry_frame(idx, frames, len) == frame) {
            pvbr[idx++] = (uint32_t)(pos - base);
        }
        frame++;
        pos += h.frame_bytes;
    }
    return idx == len;
}

audio_track_length_decision_t audio_track_length_decide(const audio_track_length_inputs_t *in)
{
    audio_track_length_decision_t d = {0};
    if (!in) return d;
    uint32_t ref = 0u;
    audio_track_length_source_t ref_source = AUDIO_TRACK_LENGTH_NONE;
    if (in->scan_ms) {
        ref = in->scan_ms;
        ref_source = AUDIO_TRACK_LENGTH_SCAN;
    } else if (in->pvbr_ms) {
        ref = in->pvbr_ms;
        ref_source = AUDIO_TRACK_LENGTH_PVBR;
    } else if (in->bitrate_ms && in->bitrate_cbr) {
        ref = in->bitrate_ms;
        ref_source = AUDIO_TRACK_LENGTH_BITRATE;
    }
    uint32_t file_ms = 0u;
    if (in->xing_ms && ref) {
        uint32_t tol = (uint32_t)(((uint64_t)ref * AUDIO_XING_TOLERANCE_PERMILLE) / 1000u);
        if (tol < AUDIO_XING_TOLERANCE_MIN_MS) tol = AUDIO_XING_TOLERANCE_MIN_MS;
        const uint32_t diff = in->xing_ms > ref ? in->xing_ms - ref : ref - in->xing_ms;
        d.xing = diff <= tol ? AUDIO_XING_TRUSTED : AUDIO_XING_MISMATCH;
    } else {
        d.xing = in->xing_ms ? AUDIO_XING_UNVERIFIED : AUDIO_XING_ABSENT;
    }
    if (in->xing_ms) {
        file_ms = in->xing_ms;
        d.source = AUDIO_TRACK_LENGTH_XING;
    } else if (ref) {
        file_ms = ref;
        d.source = ref_source;
    } else if (in->bitrate_ms) {
        file_ms = in->bitrate_ms;
        d.source = AUDIO_TRACK_LENGTH_BITRATE;
    }
    d.file_ms = file_ms;
    d.track_ms = audio_track_length_resolve(in->span_ms, file_ms);
    return d;
}

uint32_t audio_track_length_extend(uint32_t track_ms, uint32_t decoded_ms)
{
    return decoded_ms > track_ms ? decoded_ms + AUDIO_TRACK_EXTEND_STEP_MS : track_ms;
}

uint32_t audio_track_length_at_eof(uint32_t span_ms, uint32_t track_ms, uint32_t end_ms,
                                   bool extended)
{
    if (extended) return end_ms > span_ms ? end_ms : span_ms;
    return end_ms > track_ms + AUDIO_TRACK_LENGTH_TOLERANCE_MS ? end_ms : track_ms;
}

const char *audio_xing_verdict_name(audio_xing_verdict_t verdict)
{
    switch (verdict) {
    case AUDIO_XING_TRUSTED: return "xing trusted";
    case AUDIO_XING_MISMATCH: return "xing MISMATCH (kept)";
    case AUDIO_XING_UNVERIFIED: return "xing unverified";
    default: return "no xing";
    }
}

const char *audio_track_length_source_name(audio_track_length_source_t source)
{
    switch (source) {
    case AUDIO_TRACK_LENGTH_XING: return "xing";
    case AUDIO_TRACK_LENGTH_PVBR: return "pvbr";
    case AUDIO_TRACK_LENGTH_BITRATE: return "bitrate";
    case AUDIO_TRACK_LENGTH_EOF: return "eof";
    case AUDIO_TRACK_LENGTH_SCAN: return "scan";
    case AUDIO_TRACK_LENGTH_DECODE: return "decode";
    default: return "none";
    }
}
