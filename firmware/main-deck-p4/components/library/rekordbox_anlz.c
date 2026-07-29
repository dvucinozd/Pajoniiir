/*
 * rekordbox_anlz.c  —  Rekordbox ANLZ file parser
 *
 * Parses ANLZ0000.DAT (PPTH, PVBR, PQTZ, PWAV, PCOB) and
 * ANLZ0000.EXT (PWV3) from a Rekordbox USB drive.
 *
 * All multi-byte fields in the file are big-endian.
 * This code runs on both ESP32-P4 (via ESP-IDF VFS) and PC (ANLZ_STANDALONE_TEST).
 */

#include "rekordbox_anlz.h"

#include <stdio.h>
#include <stdlib.h>

#include <string.h>
#include <stdint.h>
#include <stdbool.h>

static const char *TAG = "anlz";
#define ANLZ_MAX_BEATS 0xFFFFu

/* ── Short-read detection ────────────────────────────────────────────────── *
 *
 * A truncated ANLZ file must be rejected, not silently turned into zero-valued
 * fields that then get cached as if they were real analysis. Every read goes
 * through the two helpers below, which latch the fact that the medium delivered
 * less than was asked for; parse_one_strict() checks the latch and refuses to
 * publish anything when it is set.
 *
 * The latch is per-task. Two decks resolve ANLZ concurrently on their own
 * workers, and a single shared flag would let one parse's reset erase a short
 * read the other had just detected.
 */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ANLZ_PARSE_LOCAL _Thread_local
#else
#define ANLZ_PARSE_LOCAL __thread
#endif

static ANLZ_PARSE_LOCAL bool s_anlz_short_read;

/* Read exactly `count` bytes or latch a short read. Returns false on failure so
 * a caller can bail immediately rather than continuing over garbage. */
static bool anlz_read_exact(void *dst, size_t count, FILE *fp)
{
    if (fread(dst, 1u, count, fp) == count) {
        return true;
    }
    s_anlz_short_read = true;
    return false;
}

/* One byte, or -1 with the short read latched. */
static int anlz_read_u8(FILE *fp)
{
    const int value = fgetc(fp);
    if (value == EOF) {
        s_anlz_short_read = true;
        return -1;
    }
    return value;
}

/* ── Big-endian read helpers ─────────────────────────────────────────────── */

static inline uint16_t read_be16(FILE *fp)
{
    uint8_t b[2];
    if (!anlz_read_exact(b, sizeof(b), fp)) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}
static inline uint32_t read_be32(FILE *fp)
{
    uint8_t b[4];
    if (!anlz_read_exact(b, sizeof(b), fp)) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] <<  8) |  (uint32_t)b[3];
}

static bool utf8_append_codepoint(char *dst, size_t dst_sz, size_t *out_i, uint32_t cp)
{
    if (!dst || dst_sz == 0 || !out_i || cp == 0u) {
        return false;
    }
    size_t i = *out_i;
    if (cp <= 0x7Fu) {
        if (i + 1u >= dst_sz) return false;
        dst[i++] = (char)cp;
    } else if (cp <= 0x7FFu) {
        if (i + 2u >= dst_sz) return false;
        dst[i++] = (char)(0xC0u | (cp >> 6));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        if (i + 3u >= dst_sz) return false;
        dst[i++] = (char)(0xE0u | (cp >> 12));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0x10FFFFu) {
        if (i + 4u >= dst_sz) return false;
        dst[i++] = (char)(0xF0u | (cp >> 18));
        dst[i++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        return false;
    }
    *out_i = i;
    return true;
}

/* ── Tag search ──────────────────────────────────────────────────────────── */

/* ANLZ files are a sequence of sections, each with a
 * (tag, header_size, segment_size) header, optionally preceded by a PMAI
 * file header whose segment_size spans the whole file. Walking the section
 * headers finds a tag without ever reading payload bytes, so tag-like byte
 * patterns inside another section's payload can never produce a false hit
 * (and the walk is a handful of seeks instead of an fgetc() pass per tag). */
typedef enum {
    TAG_WALK_FOUND = 0,     /* fp positioned right after the 4-byte tag  */
    TAG_WALK_ABSENT,        /* clean walk to EOF, tag not in the file    */
    TAG_WALK_MALFORMED,     /* section chain broken — structure unusable */
} tag_walk_result_t;

static tag_walk_result_t walk_sections_for_tag(FILE *fp, uint32_t target)
{
    if (fseek(fp, 0, SEEK_END) != 0) return TAG_WALK_MALFORMED;
    const long fsz = ftell(fp);
    if (fsz < 12 || (unsigned long)fsz > UINT32_MAX) {
        return TAG_WALK_MALFORMED;
    }

    const uint32_t file_len = (uint32_t)fsz;
    uint32_t pos = 0u;

    while (pos + 12u <= file_len) {
        if (fseek(fp, (long)pos, SEEK_SET) != 0) return TAG_WALK_MALFORMED;
        const uint32_t tag          = read_be32(fp);
        const uint32_t header_size  = read_be32(fp);
        const uint32_t segment_size = read_be32(fp);
        if (s_anlz_short_read) return TAG_WALK_MALFORMED;

        /* PMAI's segment_size covers the entire file; its sections start
         * right after the PMAI header. Every other section advances by its
         * own total segment size. */
        const uint32_t advance = (tag == ANLZ_TAG_PMAI) ? header_size : segment_size;
        if (header_size < 12u ||
            (tag != ANLZ_TAG_PMAI && segment_size < header_size) ||
            advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }

        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) return TAG_WALK_MALFORMED;
            return TAG_WALK_FOUND;
        }
        pos += advance;
    }

    /* One to eleven trailing bytes are a partial section header, not a clean
     * absence of an optional tag. */
    return pos == file_len ? TAG_WALK_ABSENT : TAG_WALK_MALFORMED;
}

/* There is deliberately no byte-scan fallback. Scanning for a tag pattern with a
 * sliding window can false-match the same four bytes inside another section's
 * payload, and the "recovered" offsets then parse payload bytes as a header —
 * producing plausible-looking BPM, beatgrid and cue values from a corrupt file.
 * A file whose section chain does not walk cleanly is rejected instead. */

/* ── PPTH parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after the 4-byte tag:
 *   4B  header_size  (typically 20)
 *   4B  segment_size (total including header; path data follows header)
 *   4B  flags / padding
 *   4B  path_length  (number of bytes of UTF-16 BE data that follow, incl. NUL)
 *   N×2B  UTF-16 BE characters
 *
 * We convert UTF-16 BE to UTF-8 so FatFs can open non-ASCII Rekordbox paths.
 * Directory separators '\' are converted to '/'.
 */
static esp_err_t parse_ppth(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp); /* bytes in header incl. tag+hdr_size+seg_size */
    uint32_t segment_size = read_be32(fp); /* total segment including header              */

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PPTH: bad sizes hdr=%u seg=%u", header_size, segment_size);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Skip remaining header bytes (flags + path_length field are part of header) */
    /* Reference: after tag (4B) + header_size field (4B) + segment_size field (4B)
     * we have already consumed 8B past the tag.  The header_size counts from the
     * tag start, so remaining header bytes = header_size - 4 (tag) - 4 (hdr) - 4 (seg) */
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size; /* byte count of UTF-16 path data */
    if (data_len == 0 || data_len > (ANLZ_PATH_MAX * 2u)) {
        ANLZ_LOGE(TAG, "PPTH: path data length %u out of range", data_len);
        return ESP_ERR_INVALID_SIZE;
    }

    /* Read UTF-16 BE pairs and emit UTF-8 */
    size_t out_idx = 0;
    for (uint32_t i = 0; i + 1 < data_len; i += 2) {
        const int hi = anlz_read_u8(fp);
        const int lo = anlz_read_u8(fp);
        if (hi < 0 || lo < 0) break;
        uint16_t wc = (uint16_t)(((uint16_t)hi << 8u) | (uint16_t)lo);
        if (wc == 0u) break; /* NUL terminator */

        uint32_t cp = wc;
        if (wc >= 0xD800u && wc <= 0xDBFFu && i + 3u < data_len) {
            const int hi2 = anlz_read_u8(fp);
            const int lo2 = anlz_read_u8(fp);
            if (hi2 < 0 || lo2 < 0) break;
            uint16_t wc2 = (uint16_t)(((uint16_t)hi2 << 8u) | (uint16_t)lo2);
            if (wc2 >= 0xDC00u && wc2 <= 0xDFFFu) {
                cp = 0x10000u + ((((uint32_t)wc - 0xD800u) << 10) | ((uint32_t)wc2 - 0xDC00u));
                i += 2;
            }
        }
        if (cp == '\\') cp = '/';
        if (!utf8_append_codepoint(out->audio_path, ANLZ_PATH_MAX, &out_idx, cp)) break;
    }
    out->audio_path[out_idx] = '\0';

    ANLZ_LOGI(TAG, "PPTH: \"%s\"", out->audio_path);
    return ESP_OK;
}

/* ── PVBR parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes of padding  [typically 8 bytes → skip 4 more]
 *   400 × 4B  VBR seek offsets (big-endian uint32)
 */
static esp_err_t parse_pvbr(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PVBR: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t entries  = data_len / 4u;
    if (entries > ANLZ_VBR_TABLE_LEN) entries = ANLZ_VBR_TABLE_LEN;

    for (uint32_t i = 0; i < entries; i++) {
        out->vbr[i] = read_be32(fp);
    }
    out->has_vbr = (entries > 0);

    ANLZ_LOGI(TAG, "PVBR: %u seek entries. First 10: %u, %u, %u, %u, %u, %u, %u, %u, %u, %u",
              entries, out->vbr[0], out->vbr[1], out->vbr[2], out->vbr[3], out->vbr[4],
              out->vbr[5], out->vbr[6], out->vbr[7], out->vbr[8], out->vbr[9]);
    return ESP_OK;
}

/* ── PQTZ parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   N × 8B  beat entries:
 *     2B beat_phase (uint16 BE)
 *     2B bpm_x100  (uint16 BE)
 *     4B time_ms   (uint32 BE)
 */
static esp_err_t parse_pqtz(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PQTZ: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t count    = data_len / 8u;

    if (count == 0) {
        ANLZ_LOGW(TAG, "PQTZ: no beat entries");
        return ESP_OK;
    }

    uint32_t stored_count = count > ANLZ_MAX_BEATS ? ANLZ_MAX_BEATS : count;
    out->beats = (anlz_beat_t *)malloc(stored_count * sizeof(anlz_beat_t));
    if (!out->beats) {
        ANLZ_LOGE(TAG, "PQTZ: malloc failed (%u entries)", stored_count);
        return ESP_ERR_NO_MEM;
    }
    out->beat_count = (uint16_t)stored_count;

    for (uint32_t i = 0; i < stored_count; i++) {
        out->beats[i].beat_phase = read_be16(fp);
        out->beats[i].bpm_x100  = read_be16(fp);
        out->beats[i].time_ms   = read_be32(fp);
    }
    if (count > stored_count) {
        uint32_t skipped = count - stored_count;
        if (fseek(fp, (long)(skipped * 8u), SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;
        ANLZ_LOGW(TAG, "PQTZ: truncated beat grid from %u to %u entries",
                  count, stored_count);
    }

    /* BPM from first entry */
    if (stored_count > 0 && out->beats[0].bpm_x100 > 0) {
        out->bpm = (uint16_t)((out->beats[0].bpm_x100 + 50u) / 100u);
    }

    ANLZ_LOGI(TAG, "PQTZ: %u beats, BPM=%u (raw=%u)", stored_count, out->bpm,
              stored_count > 0 ? out->beats[0].bpm_x100 : 0);
    return ESP_OK;
}

/* ── PWAV parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding  [often 8 B → skip 4 more]
 *   400B waveform data (1B per column: bits[7:5]=color index, bits[4:0]=height)
 */
static esp_err_t parse_pwav(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PWAV: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t bytes    = data_len < ANLZ_WAVEFORM_LOW_LEN ? data_len : ANLZ_WAVEFORM_LOW_LEN;

    out->has_waveform_low = anlz_read_exact(out->waveform_low, bytes, fp);
    const size_t n = out->has_waveform_low ? (size_t)bytes : 0u;

    ANLZ_LOGI(TAG, "PWAV: %zu/%u bytes", n, ANLZ_WAVEFORM_LOW_LEN);
    return out->has_waveform_low ? ESP_OK : ESP_FAIL;
}

/* ── PCOB / PCPT parser ──────────────────────────────────────────────────── *
 *
 * PCOB is a container tag.  Its data section contains one or more PCPT
 * sub-records, each 56 bytes long.  We extract up to ANLZ_MAX_CUES cues.
 *
 * PCOB layout after tag:
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   [PCPT sub-records, each 56B]
 *
 * Each PCPT (56 bytes):
 *   Byte 0     : entry_type  (0x01 = single, 0x02 = loop)
 *   Byte 1     : index       (0–7)
 *   Bytes 2–3  : unknown / color
 *   Bytes 4–7  : start_ms    (uint32 BE)
 *   Bytes 8–11 : end_ms      (uint32 BE, loops only)
 *   Bytes 12–55: name, color info (unused here)
 *
 * Note: The exact layout varies slightly between Rekordbox versions.
 * This follows the spectran/rekordbox + DeepSymmetry specifications.
 */
static esp_err_t parse_pcob(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PCOB: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len   = segment_size - header_size;
    uint32_t pcpt_count = data_len / 56u;

    out->cue_count = 0;

    for (uint32_t i = 0; i < pcpt_count; i++) {
        /* Read 56 bytes for this PCPT entry */
        uint8_t buf[56];
        if (!anlz_read_exact(buf, sizeof(buf), fp)) break;

        uint8_t entry_type = buf[0];
        uint8_t index      = buf[1];

        if (index >= ANLZ_MAX_CUES) continue; /* ignore out-of-range slots */

        uint32_t start_ms = ((uint32_t)buf[4]  << 24) | ((uint32_t)buf[5]  << 16) |
                            ((uint32_t)buf[6]  <<  8) |  (uint32_t)buf[7];
        uint32_t end_ms   = ((uint32_t)buf[8]  << 24) | ((uint32_t)buf[9]  << 16) |
                            ((uint32_t)buf[10] <<  8) |  (uint32_t)buf[11];

        anlz_cue_t *cue = &out->cues[out->cue_count];
        cue->type     = (entry_type == 2) ? ANLZ_CUE_LOOP : ANLZ_CUE_SINGLE;
        cue->index    = index;
        cue->start_ms = start_ms;
        cue->end_ms   = (cue->type == ANLZ_CUE_LOOP) ? end_ms : 0u;

        out->cue_count++;
        if (out->cue_count >= ANLZ_MAX_CUES) break;
    }

    ANLZ_LOGI(TAG, "PCOB: %u cue/loop entries", out->cue_count);
    return ESP_OK;
}

/* ── PWV3 parser ─────────────────────────────────────────────────────────── *
 *
 * Byte layout after tag (same as PWAV but data can be up to 60 000 bytes):
 *   4B  header_size
 *   4B  segment_size
 *   (header_size − 12) bytes padding
 *   N×1B waveform data (N = segment_size − header_size)
 */
static esp_err_t parse_pwv3(FILE *fp, anlz_metadata_t *meta)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PWV3: bad sizes");
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    if (data_len == 0) {
        ANLZ_LOGW(TAG, "PWV3: empty waveform");
        return ESP_OK;
    }
    if (data_len > ANLZ_WAVEFORM_HIGH_MAX) {
        data_len = ANLZ_WAVEFORM_HIGH_MAX;
    }

    /* Free any previous allocation (safe double-parse guard) */
    if (meta->waveform_high) {
        free(meta->waveform_high);
        meta->waveform_high     = NULL;
        meta->waveform_high_len = 0;
    }

    meta->waveform_high = (uint8_t *)malloc(data_len);
    if (!meta->waveform_high) {
        ANLZ_LOGE(TAG, "PWV3: malloc %u bytes failed", data_len);
        return ESP_ERR_NO_MEM;
    }

    const bool complete = anlz_read_exact(meta->waveform_high, data_len, fp);
    const size_t n = complete ? (size_t)data_len : 0u;
    meta->waveform_high_len = (uint32_t)n;

    ANLZ_LOGI(TAG, "PWV3: %u bytes high-res waveform", (unsigned)n);
    return complete ? ESP_OK : ESP_FAIL;
}

/* ── Public API ───────────────────────────────────────────────────────────── *
 *
 * Parsing is transactional. Every section is walked and parsed into a temporary
 * object, and that object is published to the caller only after the whole file
 * has validated. A truncated or structurally broken file therefore cannot leave
 * the caller holding half-populated analysis — which previously became a cache
 * entry indistinguishable from a good one.
 */

static esp_err_t parse_one_strict(FILE *fp,
                                  uint32_t tag,
                                  anlz_metadata_t *meta,
                                  bool *found)
{
    s_anlz_short_read = false;
    const tag_walk_result_t walk = walk_sections_for_tag(fp, tag);
    if (s_anlz_short_read || walk == TAG_WALK_MALFORMED) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (walk == TAG_WALK_ABSENT) {
        *found = false;
        return ESP_OK;
    }

    *found = true;
    esp_err_t rc = ESP_ERR_INVALID_ARG;
    switch (tag) {
    case ANLZ_TAG_PPTH: rc = parse_ppth(fp, meta); break;
    case ANLZ_TAG_PVBR: rc = parse_pvbr(fp, meta); break;
    case ANLZ_TAG_PQTZ: rc = parse_pqtz(fp, meta); break;
    case ANLZ_TAG_PWAV: rc = parse_pwav(fp, meta); break;
    case ANLZ_TAG_PCOB: rc = parse_pcob(fp, meta); break;
    case ANLZ_TAG_PWV3: rc = parse_pwv3(fp, meta); break;
    default: break;
    }
    if (s_anlz_short_read && rc == ESP_OK) rc = ESP_ERR_INVALID_SIZE;
    return rc;
}

esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out)
{
    if (!dat_path || !out) return ESP_ERR_INVALID_ARG;

    FILE *fp = fopen(dat_path, "rb");
    if (!fp) {
        ANLZ_LOGE(TAG, "Cannot open: %s", dat_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* Each tag is located by its own walk from the start, so the sections may
     * appear in any order. Rekordbox writes them in a fixed order today, but
     * nothing in the format requires it. */
    anlz_metadata_t next = {0};
    const uint32_t tags[] = {
        ANLZ_TAG_PPTH,
        ANLZ_TAG_PVBR,
        ANLZ_TAG_PQTZ,
        ANLZ_TAG_PWAV,
        ANLZ_TAG_PCOB,
    };
    bool has_path = false;
    esp_err_t result = ESP_OK;

    for (size_t i = 0u; i < sizeof(tags) / sizeof(tags[0]); ++i) {
        bool found = false;
        result = parse_one_strict(fp, tags[i], &next, &found);
        if (result != ESP_OK) break;
        if (tags[i] == ANLZ_TAG_PPTH) has_path = found && next.audio_path[0] != '\0';
    }
    fclose(fp);

    /* Without PPTH there is no audio path, so the analysis cannot be tied to a
     * file — treat it as absent rather than partially usable. */
    if (result == ESP_OK && !has_path) result = ESP_ERR_NOT_FOUND;
    if (result != ESP_OK) {
        anlz_free(&next);
        memset(&next, 0, sizeof(next));
        memset(out, 0, sizeof(*out));
        ANLZ_LOGE(TAG, "DAT rejected before publish: %s (%d)", dat_path, result);
        return result;
    }

    *out = next;
    ANLZ_LOGI(TAG, "DAT transaction published: bpm=%u beats=%u cues=%u",
              out->bpm, out->beat_count, out->cue_count);
    return ESP_OK;
}

esp_err_t anlz_parse_ext(const char *ext_path, anlz_metadata_t *meta)
{
    if (!ext_path || !meta) return ESP_ERR_INVALID_ARG;

    FILE *fp = fopen(ext_path, "rb");
    if (!fp) {
        ANLZ_LOGE(TAG, "Cannot open EXT: %s", ext_path);
        return ESP_ERR_NOT_FOUND;
    }

    /* The high-resolution waveform is an enrichment of metadata the caller
     * already holds, so work on a clone: a rejected EXT leaves the existing
     * object untouched rather than clearing a good DAT parse. */
    anlz_metadata_t next = {0};
    esp_err_t result = anlz_clone(meta, &next);
    bool found = false;
    if (result == ESP_OK) {
        result = parse_one_strict(fp, ANLZ_TAG_PWV3, &next, &found);
        if (result == ESP_OK && !found) result = ESP_ERR_NOT_FOUND;
    }
    fclose(fp);

    if (result != ESP_OK) {
        anlz_free(&next);
        ANLZ_LOGE(TAG, "EXT rejected; previous metadata retained: %s (%d)",
                  ext_path, result);
        return result;
    }

    anlz_free(meta);
    *meta = next;
    return ESP_OK;
}

esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *out)
{
    if (!src || !out || src == out) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(out, 0, sizeof(*out));
    *out = *src;
    out->beats = NULL;
    out->waveform_high = NULL;

    if (src->beat_count > 0u) {
        if (!src->beats) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        out->beats = (anlz_beat_t *)malloc((size_t)src->beat_count * sizeof(*out->beats));
        if (!out->beats) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->beats, src->beats, (size_t)src->beat_count * sizeof(*out->beats));
    }

    if (src->waveform_high_len > 0u) {
        if (!src->waveform_high) {
            anlz_free(out);
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        out->waveform_high = (uint8_t *)malloc(src->waveform_high_len);
        if (!out->waveform_high) {
            anlz_free(out);
            memset(out, 0, sizeof(*out));
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->waveform_high, src->waveform_high, src->waveform_high_len);
    }
    return ESP_OK;
}

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) return;

    if (meta->beats) {
        free(meta->beats);
        meta->beats      = NULL;
        meta->beat_count = 0;
    }
    if (meta->waveform_high) {
        free(meta->waveform_high);
        meta->waveform_high     = NULL;
        meta->waveform_high_len = 0;
    }
}

