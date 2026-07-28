/*
 * Strict production ANLZ parser wrapper.
 *
 * The legacy section parsers are retained, but public DAT/EXT entry points use
 * only the bounded section walk and publish a temporary metadata object after
 * complete validation. Short reads can no longer become zero-valued fields or
 * persistent partial cache entries.
 */
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Per-thread, not global. Two decks can resolve ANLZ concurrently (each track
 * load runs on its own worker task), and with one shared flag a reset in one
 * parse erases a short read the other has just detected — publishing a truncated
 * DAT as valid and caching it. FreeRTOS/ESP-IDF places `_Thread_local` in the
 * per-task TLS block, and the PC host tests are single-threaded either way. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ANLZ_PARSE_LOCAL _Thread_local
#else
#define ANLZ_PARSE_LOCAL __thread
#endif

static ANLZ_PARSE_LOCAL bool s_anlz_short_read;

static size_t anlz_checked_fread(void *ptr, size_t size, size_t count, FILE *stream)
{
    const size_t got = fread(ptr, size, count, stream);
    if (got != count) s_anlz_short_read = true;
    return got;
}

static int anlz_checked_fgetc(FILE *stream)
{
    const int value = fgetc(stream);
    if (value == EOF) s_anlz_short_read = true;
    return value;
}

#define fread                    anlz_checked_fread
#define fgetc                    anlz_checked_fgetc
#define anlz_parse_dat           anlz_parse_dat_legacy_partial
#define anlz_parse_ext           anlz_parse_ext_legacy_partial
#define walk_sections_for_tag    walk_sections_for_tag_legacy
#include "rekordbox_anlz.c"
#undef walk_sections_for_tag
#undef anlz_parse_dat
#undef anlz_parse_ext
#undef fread
#undef fgetc

/* Production-only walker. The legacy parser keeps its byte-scan fallback,
 * while the published parser accepts only a complete, bounded section chain. */
static tag_walk_result_t walk_sections_for_tag(FILE *fp, uint32_t target)
{
    if (fseek(fp, 0, SEEK_END) != 0) return TAG_WALK_MALFORMED;
    long fsz = ftell(fp);
    if (fsz < 12 || (unsigned long)fsz > UINT32_MAX) {
        return TAG_WALK_MALFORMED;
    }

    const uint32_t file_len = (uint32_t)fsz;
    uint32_t pos = 0u;

    while (pos + 12u <= file_len) {
        if (fseek(fp, (long)pos, SEEK_SET) != 0) return TAG_WALK_MALFORMED;
        const uint32_t tag = read_be32(fp);
        const uint32_t header_size = read_be32(fp);
        const uint32_t segment_size = read_be32(fp);
        if (s_anlz_short_read) return TAG_WALK_MALFORMED;

        const uint32_t advance = tag == ANLZ_TAG_PMAI ? header_size : segment_size;
        if (header_size < 12u ||
            (tag != ANLZ_TAG_PMAI && segment_size < header_size) ||
            advance < 12u || advance > file_len - pos) {
            return TAG_WALK_MALFORMED;
        }

        if (tag == target) {
            if (fseek(fp, (long)(pos + 4u), SEEK_SET) != 0) {
                return TAG_WALK_MALFORMED;
            }
            return TAG_WALK_FOUND;
        }
        pos += advance;
    }

    /* One to eleven trailing bytes are a partial section header, not a clean
     * absence of an optional tag. */
    return pos == file_len ? TAG_WALK_ABSENT : TAG_WALK_MALFORMED;
}

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
    if (!fp) return ESP_ERR_NOT_FOUND;

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
    if (!fp) return ESP_ERR_NOT_FOUND;

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
