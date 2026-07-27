#include "rekordbox_anlz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifdef ANLZ_STANDALONE_TEST
#  define ANLZ_LOGI(tag, fmt, ...) ((void)0)
#  define ANLZ_LOGW(tag, fmt, ...) ((void)0)
#  define ANLZ_LOGE(tag, fmt, ...) ((void)0)
#else
#  include "esp_log.h"
#  define ANLZ_LOGI(tag, fmt, ...) ESP_LOGI(tag, fmt, ##__VA_ARGS__)
#  define ANLZ_LOGW(tag, fmt, ...) ESP_LOGW(tag, fmt, ##__VA_ARGS__)
#  define ANLZ_LOGE(tag, fmt, ...) ESP_LOGE(tag, fmt, ##__VA_ARGS__)
#endif

static const char *TAG = "anlz";

/* ── Big-endian read helpers ─────────────────────────────────────────────── */

static uint16_t read_be16(FILE *fp)
{
    uint8_t b[2];
    if (fread(b, 1, 2, fp) != 2) return 0;
    return (uint16_t)((b[0] << 8) | b[1]);
}

static uint32_t read_be32(FILE *fp)
{
    uint8_t b[4];
    if (fread(b, 1, 4, fp) != 4) return 0;
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8) | b[3];
}

static bool utf8_append_codepoint(char *dst, size_t cap, size_t *io_pos, uint32_t cp)
{
    uint8_t encoded[4];
    size_t n = 0;
    if (cp <= 0x7Fu) {
        encoded[0] = (uint8_t)cp;
        n = 1;
    } else if (cp <= 0x7FFu) {
        encoded[0] = (uint8_t)(0xC0u | (cp >> 6));
        encoded[1] = (uint8_t)(0x80u | (cp & 0x3Fu));
        n = 2;
    } else if (cp <= 0xFFFFu) {
        if (cp >= 0xD800u && cp <= 0xDFFFu) return false;
        encoded[0] = (uint8_t)(0xE0u | (cp >> 12));
        encoded[1] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        encoded[2] = (uint8_t)(0x80u | (cp & 0x3Fu));
        n = 3;
    } else if (cp <= 0x10FFFFu) {
        encoded[0] = (uint8_t)(0xF0u | (cp >> 18));
        encoded[1] = (uint8_t)(0x80u | ((cp >> 12) & 0x3Fu));
        encoded[2] = (uint8_t)(0x80u | ((cp >> 6) & 0x3Fu));
        encoded[3] = (uint8_t)(0x80u | (cp & 0x3Fu));
        n = 4;
    } else {
        return false;
    }
    if (*io_pos + n >= cap) return false;
    memcpy(dst + *io_pos, encoded, n);
    *io_pos += n;
    return true;
}

typedef enum {
    TAG_WALK_FOUND,
    TAG_WALK_ABSENT,        /* clean walk to EOF, tag not in the file    */
    TAG_WALK_MALFORMED,     /* section chain broken — structure unusable */
} tag_walk_result_t;

static tag_walk_result_t walk_sections_for_tag(FILE *fp, uint32_t target)
{
    if (fseek(fp, 0, SEEK_END) != 0) return TAG_WALK_MALFORMED;
    long fsz = ftell(fp);
    if (fsz < 12 || (unsigned long)fsz > UINT32_MAX) return TAG_WALK_MALFORMED;

    uint32_t file_len = (uint32_t)fsz;
    uint32_t pos = 0;

    while (pos + 12u <= file_len) {
        if (fseek(fp, (long)pos, SEEK_SET) != 0) return TAG_WALK_MALFORMED;
        uint32_t tag          = read_be32(fp);
        uint32_t header_size  = read_be32(fp);
        uint32_t segment_size = read_be32(fp);

        /* Validate the section envelope even when it is the requested tag. A
         * target tag followed by an oversized segment must not bypass the
         * structural walk and become a parser-specific short-read. */
        uint32_t advance = (tag == ANLZ_TAG_PMAI) ? header_size : segment_size;
        if (header_size < 12u || segment_size < header_size ||
            advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }

        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) return TAG_WALK_MALFORMED;
            return TAG_WALK_FOUND;
        }

        /* PMAI's segment_size covers the entire file; its sections start
         * right after the PMAI header. Every other section advances by its
         * own total segment size. */
        pos += advance;
    }

    /* A clean section chain must end exactly at EOF. Trailing bytes shorter
     * than a section header are corruption, not an absent optional tag. */
    return pos == file_len ? TAG_WALK_ABSENT : TAG_WALK_MALFORMED;
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
    if (rc == TAG_WALK_ABSENT) return false;
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
 * Directory separators '\\' are converted to '/'.
 */
static esp_err_t parse_ppth(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) {
        ANLZ_LOGE(TAG, "PPTH: bad sizes hdr=%u seg=%u", header_size, segment_size);
        return ESP_ERR_INVALID_SIZE;
    }

    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    if (data_len == 0 || data_len > (ANLZ_PATH_MAX * 2u)) {
        ANLZ_LOGE(TAG, "PPTH: path data length %u out of range", data_len);
        return ESP_ERR_INVALID_SIZE;
    }

    size_t out_idx = 0;
    for (uint32_t i = 0; i + 1 < data_len; i += 2) {
        uint8_t hi = (uint8_t)fgetc(fp);
        uint8_t lo = (uint8_t)fgetc(fp);
        uint16_t wc = (uint16_t)(((uint16_t)hi << 8u) | (uint16_t)lo);
        if (wc == 0u) break;

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

    for (uint32_t i = 0; i < entries; i++) out->vbr[i] = read_be32(fp);
    out->has_vbr = (entries > 0);
    return ESP_OK;
}

static esp_err_t parse_pqtz(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);

    if (segment_size < header_size || header_size < 12) return ESP_ERR_INVALID_SIZE;
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    uint32_t count = data_len / 8u;
    if (count == 0) return ESP_OK;
    if (count > UINT16_MAX) return ESP_ERR_INVALID_SIZE;

    anlz_beat_t *beats = calloc(count, sizeof(*beats));
    if (!beats) return ESP_ERR_NO_MEM;
    for (uint32_t i = 0; i < count; ++i) {
        beats[i].phase = read_be16(fp);
        beats[i].bpm_x100 = read_be16(fp);
        beats[i].time_ms = read_be32(fp);
    }
    out->beats = beats;
    out->beat_count = (uint16_t)count;
    if (count > 0 && beats[0].bpm_x100 > 0) out->bpm = beats[0].bpm_x100 / 100u;
    return ESP_OK;
}

static esp_err_t parse_pwav(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);
    if (segment_size < header_size || header_size < 12) return ESP_ERR_INVALID_SIZE;
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;
    uint32_t data_len = segment_size - header_size;
    uint32_t take = data_len > ANLZ_WAVEFORM_LOW_LEN ? ANLZ_WAVEFORM_LOW_LEN : data_len;
    if (fread(out->waveform_low, 1, take, fp) != take) return ESP_ERR_INVALID_SIZE;
    out->has_waveform_low = take > 0u;
    return ESP_OK;
}

static esp_err_t parse_pcob(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);
    if (segment_size < header_size || header_size < 12) return ESP_ERR_INVALID_SIZE;
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;

    uint32_t data_len = segment_size - header_size;
    const uint32_t entry_size = 56u;
    uint32_t count = data_len / entry_size;
    if (count > ANLZ_MAX_CUES) count = ANLZ_MAX_CUES;
    out->cue_count = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t pcpt[56];
        if (fread(pcpt, 1, sizeof(pcpt), fp) != sizeof(pcpt)) return ESP_ERR_INVALID_SIZE;
        anlz_cue_t *cue = &out->cues[out->cue_count++];
        cue->type = pcpt[0] == 0x02 ? ANLZ_CUE_LOOP : ANLZ_CUE_SINGLE;
        cue->index = pcpt[1];
        cue->start_ms = ((uint32_t)pcpt[4] << 24) | ((uint32_t)pcpt[5] << 16) |
                        ((uint32_t)pcpt[6] << 8) | pcpt[7];
        cue->end_ms = ((uint32_t)pcpt[8] << 24) | ((uint32_t)pcpt[9] << 16) |
                      ((uint32_t)pcpt[10] << 8) | pcpt[11];
    }
    return ESP_OK;
}

static esp_err_t parse_pwv3(FILE *fp, anlz_metadata_t *out)
{
    uint32_t header_size  = read_be32(fp);
    uint32_t segment_size = read_be32(fp);
    if (segment_size < header_size || header_size < 12) return ESP_ERR_INVALID_SIZE;
    uint32_t skip = header_size - 12u;
    if (fseek(fp, (long)skip, SEEK_CUR) != 0) return ESP_ERR_INVALID_ARG;
    uint32_t data_len = segment_size - header_size;
    uint8_t *wave = malloc(data_len);
    if (!wave && data_len > 0u) return ESP_ERR_NO_MEM;
    if (data_len > 0u && fread(wave, 1, data_len, fp) != data_len) {
        free(wave);
        return ESP_ERR_INVALID_SIZE;
    }
    free(out->waveform_high);
    out->waveform_high = wave;
    out->waveform_high_len = data_len;
    return ESP_OK;
}

esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *dst)
{
    if (!src || !dst) return ESP_ERR_INVALID_ARG;
    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->beats = NULL;
    dst->waveform_high = NULL;
    if (src->beat_count > 0u && src->beats) {
        dst->beats = malloc((size_t)src->beat_count * sizeof(*src->beats));
        if (!dst->beats) { memset(dst, 0, sizeof(*dst)); return ESP_ERR_NO_MEM; }
        memcpy(dst->beats, src->beats, (size_t)src->beat_count * sizeof(*src->beats));
    }
    if (src->waveform_high_len > 0u && src->waveform_high) {
        dst->waveform_high = malloc(src->waveform_high_len);
        if (!dst->waveform_high) {
            free(dst->beats);
            memset(dst, 0, sizeof(*dst));
            return ESP_ERR_NO_MEM;
        }
        memcpy(dst->waveform_high, src->waveform_high, src->waveform_high_len);
    }
    return ESP_OK;
}

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) return;
    free(meta->beats);
    free(meta->waveform_high);
    memset(meta, 0, sizeof(*meta));
}

esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out)
{
    if (!dat_path || !out) return ESP_ERR_INVALID_ARG;
    FILE *fp = fopen(dat_path, "rb");
    if (!fp) return ESP_ERR_NOT_FOUND;
    memset(out, 0, sizeof(*out));
    if (find_tag(fp, ANLZ_TAG_PPTH)) (void)parse_ppth(fp, out);
    if (find_tag(fp, ANLZ_TAG_PVBR)) (void)parse_pvbr(fp, out);
    if (find_tag(fp, ANLZ_TAG_PQTZ)) (void)parse_pqtz(fp, out);
    if (find_tag(fp, ANLZ_TAG_PWAV)) (void)parse_pwav(fp, out);
    if (find_tag(fp, ANLZ_TAG_PCOB)) (void)parse_pcob(fp, out);
    fclose(fp);
    return out->audio_path[0] ? ESP_OK : ESP_ERR_NOT_FOUND;
}

esp_err_t anlz_parse_ext(const char *ext_path, anlz_metadata_t *meta)
{
    if (!ext_path || !meta) return ESP_ERR_INVALID_ARG;
    FILE *fp = fopen(ext_path, "rb");
    if (!fp) return ESP_ERR_NOT_FOUND;
    esp_err_t result = find_tag(fp, ANLZ_TAG_PWV3) ? parse_pwv3(fp, meta)
                                                   : ESP_ERR_NOT_FOUND;
    fclose(fp);
    return result;
}
