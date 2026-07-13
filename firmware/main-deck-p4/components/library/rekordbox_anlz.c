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

/* ── Big-endian read helpers ─────────────────────────────────────────────── */

static inline uint16_t read_be16(FILE *fp)
{
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}
static inline uint32_t read_be32(FILE *fp)
{
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
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
    long fsz = ftell(fp);
    if (fsz < 12) return TAG_WALK_MALFORMED;

    uint32_t file_len = (uint32_t)fsz;
    uint32_t pos = 0;

    while (pos + 12u <= file_len) {
        if (fseek(fp, (long)pos, SEEK_SET) != 0) return TAG_WALK_MALFORMED;
        uint32_t tag          = read_be32(fp);
        uint32_t header_size  = read_be32(fp);
        uint32_t segment_size = read_be32(fp);

        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) return TAG_WALK_MALFORMED;
            return TAG_WALK_FOUND;
        }

        /* PMAI's segment_size covers the entire file; its sections start
         * right after the PMAI header. Every other section advances by its
         * own total segment size. */
        uint32_t advance = (tag == ANLZ_TAG_PMAI) ? header_size : segment_size;
        if (advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }
        pos += advance;
    }

    return TAG_WALK_ABSENT;
}

/* Legacy fallback for structurally broken files: scan forward with a
 * single-byte sliding window. Can false-match a tag inside payload data,
 * which is why it only runs when the section walk cannot parse the file.
 *
 * On success the file pointer is positioned immediately after the tag ID
 * (i.e. at the header_size field). */
static bool scan_bytes_for_tag(FILE *fp, uint32_t target)
{
    rewind(fp);
    uint32_t window = 0;
    int c;
    while ((c = fgetc(fp)) != EOF) {
        window = (window << 8) | (uint8_t)c;
        if (window == target) return true;
    }
    return false;
}

static bool find_tag(FILE *fp, uint32_t target)
{
    tag_walk_result_t rc = walk_sections_for_tag(fp, target);
    if (rc == TAG_WALK_FOUND) return true;
    if (rc == TAG_WALK_ABSENT) return false;   /* well-formed file, tag not present */
    return scan_bytes_for_tag(fp, target);
}

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
        uint8_t hi = (uint8_t)fgetc(fp);
        uint8_t lo = (uint8_t)fgetc(fp);
        uint16_t wc = (uint16_t)(((uint16_t)hi << 8u) | (uint16_t)lo);
        if (wc == 0u) break; /* NUL terminator */

        uint32_t cp = wc;
        if (wc >= 0xD800u && wc <= 0xDBFFu && i + 3u < data_len) {
            uint8_t hi2 = (uint8_t)fgetc(fp);
            uint8_t lo2 = (uint8_t)fgetc(fp);
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

    size_t n = fread(out->waveform_low, 1, bytes, fp);
    out->has_waveform_low = (n == bytes);

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
        if (fread(buf, 1, 56, fp) != 56) break;

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

    size_t n = fread(meta->waveform_high, 1, data_len, fp);
    meta->waveform_high_len = (uint32_t)n;

    ANLZ_LOGI(TAG, "PWV3: %u bytes high-res waveform", (unsigned)n);
    return (n == data_len) ? ESP_OK : ESP_FAIL;
}

/* ── Public API ───────────────────────────────────────────────────────────── */

esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out)
{
    if (!dat_path || !out) return ESP_ERR_INVALID_ARG;

    memset(out, 0, sizeof(*out));

    FILE *fp = fopen(dat_path, "rb");
    if (!fp) {
        ANLZ_LOGE(TAG, "Cannot open: %s", dat_path);
        return ESP_ERR_NOT_FOUND;
    }

    ANLZ_LOGI(TAG, "Parsing DAT: %s", dat_path);

    /* Tags we need to find — use a bitmask to track what we got */
    uint8_t found = 0;
#define GOT_PPTH  (1u << 0)
#define GOT_PVBR  (1u << 1)
#define GOT_PQTZ  (1u << 2)
#define GOT_PWAV  (1u << 3)
#define GOT_PCOB  (1u << 4)

    /* For each tag we do a full seek-from-start so that tags can appear in
     * any order (Rekordbox typically writes them in a fixed order, but we
     * must not rely on that). */
    const struct { uint32_t id; uint8_t flag; } wanted[] = {
        { ANLZ_TAG_PPTH, GOT_PPTH },
        { ANLZ_TAG_PVBR, GOT_PVBR },
        { ANLZ_TAG_PQTZ, GOT_PQTZ },
        { ANLZ_TAG_PWAV, GOT_PWAV },
        { ANLZ_TAG_PCOB, GOT_PCOB },
    };

    for (size_t i = 0; i < sizeof(wanted)/sizeof(wanted[0]); i++) {
        rewind(fp);
        if (!find_tag(fp, wanted[i].id)) {
            ANLZ_LOGW(TAG, "Tag 0x%08X not found", wanted[i].id);
            continue;
        }
        esp_err_t rc = ESP_FAIL;
        switch (wanted[i].id) {
            case ANLZ_TAG_PPTH: rc = parse_ppth(fp, out); break;
            case ANLZ_TAG_PVBR: rc = parse_pvbr(fp, out); break;
            case ANLZ_TAG_PQTZ: rc = parse_pqtz(fp, out); break;
            case ANLZ_TAG_PWAV: rc = parse_pwav(fp, out); break;
            case ANLZ_TAG_PCOB: rc = parse_pcob(fp, out); break;
        }
        if (rc == ESP_OK) {
            found |= wanted[i].flag;
        } else {
            ANLZ_LOGW(TAG, "Tag 0x%08X parse error: %d", wanted[i].id, rc);
        }
    }

    fclose(fp);

    if (!(found & GOT_PPTH)) {
        ANLZ_LOGE(TAG, "PPTH (audio path) missing — DAT invalid");
        anlz_free(out);
        return ESP_ERR_NOT_FOUND;
    }

    ANLZ_LOGI(TAG, "DAT parsed OK: bpm=%u beats=%u cues=%u vbr=%d wav=%d",
              out->bpm, out->beat_count, out->cue_count,
              out->has_vbr, out->has_waveform_low);
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

    ANLZ_LOGI(TAG, "Parsing EXT: %s", ext_path);

    rewind(fp);
    esp_err_t rc = ESP_ERR_NOT_FOUND;
    if (find_tag(fp, ANLZ_TAG_PWV3)) {
        rc = parse_pwv3(fp, meta);
    } else {
        ANLZ_LOGW(TAG, "PWV3 tag not found in EXT file");
    }

    fclose(fp);
    return rc;
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

