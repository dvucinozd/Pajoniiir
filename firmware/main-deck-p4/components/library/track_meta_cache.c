#include "track_meta_cache.h"

#include "esp_log.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *TAG = "track_meta_cache";
static const char *CACHE_ROOT = "/sd/trackcache";

#define TRACK_META_CACHE_MAGIC   0x31434D54u /* "TMC1" */
#define TRACK_META_CACHE_VERSION 2u
#define TRACK_META_CACHE_FLAGS_LOW  0x01u
#define TRACK_META_CACHE_FLAGS_VBR  0x02u
#define TRACK_META_CACHE_FLAGS_HIGH 0x04u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint16_t version;
    uint16_t header_size;
    uint32_t track_key;
    uint64_t dat_size;
    int64_t dat_mtime;
    uint64_t ext_size;
    int64_t ext_mtime;
    uint16_t bpm;
    uint16_t beat_count;
    uint8_t cue_count;
    uint8_t flags;
    uint16_t reserved;
    uint32_t waveform_high_len;
} track_meta_cache_header_t;
#pragma pack(pop)

static esp_err_t file_signature(const char *path, uint64_t *out_size, int64_t *out_mtime)
{
    if (out_size) *out_size = 0;
    if (out_mtime) *out_mtime = 0;
    if (!path || !path[0]) {
        return ESP_ERR_NOT_FOUND;
    }

    struct stat st;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (out_size) {
        *out_size = st.st_size > 0 ? (uint64_t)st.st_size : 0;
    }
    if (out_mtime) {
        *out_mtime = (int64_t)st.st_mtime;
    }
    return ESP_OK;
}

static esp_err_t sd_available(void)
{
    struct stat st;
    return stat("/sd", &st) == 0 && S_ISDIR(st.st_mode) ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t mkdir_if_missing(const char *path)
{
    if (mkdir(path, 0775) == 0 || errno == EEXIST) {
        return ESP_OK;
    }
    return ESP_FAIL;
}

static esp_err_t cache_paths(uint32_t track_key, char *dir, size_t dir_len,
                             char *path, size_t path_len)
{
    if (sd_available() != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    if (mkdir_if_missing(CACHE_ROOT) != ESP_OK) {
        return ESP_FAIL;
    }
    int written = snprintf(dir, dir_len, "%s/%08lx", CACHE_ROOT, (unsigned long)track_key);
    if (written < 0 || written >= (int)dir_len) {
        return ESP_ERR_INVALID_SIZE;
    }
    if (mkdir_if_missing(dir) != ESP_OK) {
        return ESP_FAIL;
    }
    written = snprintf(path, path_len, "%s/meta.bin", dir);
    return (written < 0 || written >= (int)path_len) ? ESP_ERR_INVALID_SIZE : ESP_OK;
}

static bool header_matches(const track_meta_cache_header_t *header,
                           uint32_t track_key,
                           uint64_t dat_size,
                           int64_t dat_mtime,
                           uint64_t ext_size,
                           int64_t ext_mtime)
{
    return header &&
           header->magic == TRACK_META_CACHE_MAGIC &&
           header->version == TRACK_META_CACHE_VERSION &&
           header->header_size == sizeof(*header) &&
           header->track_key == track_key &&
           header->dat_size == dat_size &&
           header->dat_mtime == dat_mtime &&
           header->ext_size == ext_size &&
           header->ext_mtime == ext_mtime &&
           header->beat_count <= 0x4000u &&
           header->cue_count <= ANLZ_MAX_CUES &&
           header->waveform_high_len <= ANLZ_WAVEFORM_HIGH_MAX;
}

static bool read_exact(FILE *fp, void *dst, size_t len)
{
    return len == 0 || fread(dst, 1, len, fp) == len;
}

static bool write_exact(FILE *fp, const void *src, size_t len)
{
    return len == 0 || fwrite(src, 1, len, fp) == len;
}

esp_err_t track_meta_cache_load(uint32_t track_key,
                                const char *dat_path,
                                const char *ext_path,
                                bool include_high_waveform,
                                anlz_metadata_t *out_meta)
{
    if (!out_meta || track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out_meta, 0, sizeof(*out_meta));

    uint64_t dat_size = 0;
    int64_t dat_mtime = 0;
    if (file_signature(dat_path, &dat_size, &dat_mtime) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    uint64_t ext_size = 0;
    int64_t ext_mtime = 0;
    file_signature(ext_path, &ext_size, &ext_mtime);

    char dir[64];
    char path[96];
    esp_err_t rc = cache_paths(track_key, dir, sizeof(dir), path, sizeof(path));
    if (rc != ESP_OK) {
        return rc;
    }

    FILE *fp = fopen(path, "rb");
    if (!fp) {
        return ESP_ERR_NOT_FOUND;
    }

    track_meta_cache_header_t header;
    bool ok = read_exact(fp, &header, sizeof(header)) &&
              header_matches(&header, track_key, dat_size, dat_mtime, ext_size, ext_mtime);
    if (!ok) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (include_high_waveform && ext_size > 0 &&
        !(header.flags & TRACK_META_CACHE_FLAGS_HIGH)) {
        fclose(fp);
        return ESP_ERR_INVALID_RESPONSE;
    }

    out_meta->bpm = header.bpm;
    out_meta->beat_count = header.beat_count;
    out_meta->cue_count = header.cue_count;
    out_meta->has_waveform_low = (header.flags & TRACK_META_CACHE_FLAGS_LOW) != 0;
    out_meta->has_vbr = (header.flags & TRACK_META_CACHE_FLAGS_VBR) != 0;

    ok = read_exact(fp, out_meta->waveform_low, sizeof(out_meta->waveform_low)) &&
         read_exact(fp, out_meta->vbr, sizeof(out_meta->vbr)) &&
         read_exact(fp, out_meta->cues, sizeof(out_meta->cues));
    if (ok && header.beat_count > 0) {
        out_meta->beats = calloc(header.beat_count, sizeof(anlz_beat_t));
        ok = out_meta->beats && read_exact(fp, out_meta->beats,
                                           (size_t)header.beat_count * sizeof(anlz_beat_t));
    }
    if (ok && header.waveform_high_len > 0) {
        if (include_high_waveform && (header.flags & TRACK_META_CACHE_FLAGS_HIGH)) {
            out_meta->waveform_high = malloc(header.waveform_high_len);
            ok = out_meta->waveform_high &&
                 read_exact(fp, out_meta->waveform_high, header.waveform_high_len);
            if (ok) {
                out_meta->waveform_high_len = header.waveform_high_len;
            }
        } else {
            ok = fseek(fp, (long)header.waveform_high_len, SEEK_CUR) == 0;
        }
    }

    fclose(fp);
    if (!ok) {
        anlz_free(out_meta);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "cache hit: track_key=%lu", (unsigned long)track_key);
    return ESP_OK;
}

esp_err_t track_meta_cache_save(uint32_t track_key,
                                const char *dat_path,
                                const char *ext_path,
                                const anlz_metadata_t *meta)
{
    if (!meta || track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if ((meta->beat_count > 0 && !meta->beats) ||
        (meta->waveform_high_len > 0 && !meta->waveform_high) ||
        meta->cue_count > ANLZ_MAX_CUES ||
        meta->waveform_high_len > ANLZ_WAVEFORM_HIGH_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    uint64_t dat_size = 0;
    int64_t dat_mtime = 0;
    if (file_signature(dat_path, &dat_size, &dat_mtime) != ESP_OK) {
        return ESP_ERR_NOT_FOUND;
    }
    uint64_t ext_size = 0;
    int64_t ext_mtime = 0;
    file_signature(ext_path, &ext_size, &ext_mtime);

    char dir[64];
    char path[96];
    esp_err_t rc = cache_paths(track_key, dir, sizeof(dir), path, sizeof(path));
    if (rc != ESP_OK) {
        return rc;
    }

    char part[112];
    int written = snprintf(part, sizeof(part), "%s.part", path);
    if (written < 0 || written >= (int)sizeof(part)) {
        return ESP_ERR_INVALID_SIZE;
    }

    FILE *fp = fopen(part, "wb");
    if (!fp) {
        return ESP_FAIL;
    }

    track_meta_cache_header_t header = {
        .magic = TRACK_META_CACHE_MAGIC,
        .version = TRACK_META_CACHE_VERSION,
        .header_size = sizeof(track_meta_cache_header_t),
        .track_key = track_key,
        .dat_size = dat_size,
        .dat_mtime = dat_mtime,
        .ext_size = ext_size,
        .ext_mtime = ext_mtime,
        .bpm = meta->bpm,
        .beat_count = meta->beat_count,
        .cue_count = meta->cue_count,
        .flags = (meta->has_waveform_low ? TRACK_META_CACHE_FLAGS_LOW : 0u) |
                 (meta->has_vbr ? TRACK_META_CACHE_FLAGS_VBR : 0u) |
                 (meta->waveform_high && meta->waveform_high_len > 0 ? TRACK_META_CACHE_FLAGS_HIGH : 0u),
        .waveform_high_len = meta->waveform_high && meta->waveform_high_len > 0 ? meta->waveform_high_len : 0,
    };

    bool ok = write_exact(fp, &header, sizeof(header)) &&
              write_exact(fp, meta->waveform_low, sizeof(meta->waveform_low)) &&
              write_exact(fp, meta->vbr, sizeof(meta->vbr)) &&
              write_exact(fp, meta->cues, sizeof(meta->cues)) &&
              write_exact(fp, meta->beats, (size_t)meta->beat_count * sizeof(anlz_beat_t)) &&
              write_exact(fp, meta->waveform_high, header.waveform_high_len);
    fclose(fp);

    if (!ok) {
        unlink(part);
        return ESP_FAIL;
    }
    unlink(path);
    if (rename(part, path) != 0) {
        unlink(part);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "cache write: track_key=%lu", (unsigned long)track_key);
    return ESP_OK;
}
