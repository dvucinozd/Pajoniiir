#include "audio_recorder_sink.h"
#include "audio_recorder_wav.h"
#include "sd_io_gate.h"
#include "service_log.h"

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_vfs_fat.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

static const char *TAG = "rec_sink";

static void stage_release(audio_recorder_sink_t *s);

/* Patch the 44-byte header at the file head to describe `data_bytes`, restoring
 * the append position. The caller holds sd_io_gate. */
static esp_err_t patch_header_locked(FILE *fp, uint32_t sample_rate, uint32_t data_bytes)
{
    uint8_t hdr[AUDIO_RECORDER_WAV_HEADER_BYTES];
    audio_recorder_wav_build_header(hdr, sample_rate, data_bytes);
    fflush(fp);
    if (fseek(fp, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    size_t n = fwrite(hdr, 1, sizeof(hdr), fp);
    fflush(fp);
    if (fseek(fp, 0, SEEK_END) != 0 || n != sizeof(hdr)) {
        return ESP_FAIL;
    }
    return ESP_OK;
}

static esp_err_t open_segment(audio_recorder_sink_t *s, uint32_t sample_rate)
{
    char name[AUDIO_RECORDER_SINK_PATH_MAX];
    if (audio_recorder_wav_format_segment(name, sizeof(name), s->boot_id,
                                          s->session, s->segment, sample_rate) == 0) {
        return ESP_ERR_INVALID_SIZE;
    }
    int len = snprintf(s->part_path, sizeof(s->part_path), "%s/%s",
                       AUDIO_RECORDER_SINK_DIR, name);
    if (len < 0 || (size_t)len >= sizeof(s->part_path)) {
        return ESP_ERR_INVALID_SIZE;
    }

    sd_io_gate_begin();
    FILE *fp = fopen(s->part_path, "wb");
    esp_err_t rc = ESP_OK;
    if (!fp) {
        rc = ESP_FAIL;
    } else {
        uint8_t hdr[AUDIO_RECORDER_WAV_HEADER_BYTES];
        audio_recorder_wav_build_header(hdr, sample_rate, 0u);
        if (fwrite(hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
            fclose(fp);
            fp = NULL;
            rc = ESP_FAIL;
        }
    }
    sd_io_gate_end();
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "open segment failed: %s", s->part_path);
        return rc;
    }

    s->fp = fp;
    s->sample_rate = sample_rate;
    s->data_bytes = 0u;
    s->is_open = true;
    ESP_LOGI(TAG, "segment open: %s", s->part_path);
    return ESP_OK;
}

esp_err_t audio_recorder_sink_prepare(uint64_t *out_free_bytes)
{
    struct stat st;
    if (stat("/sd", &st) != 0 || !S_ISDIR(st.st_mode)) {
        return ESP_ERR_NOT_FOUND;
    }
    if (mkdir(AUDIO_RECORDER_SINK_DIR, 0775) != 0 && errno != EEXIST) {
        ESP_LOGE(TAG, "mkdir %s failed (errno=%d)", AUDIO_RECORDER_SINK_DIR, errno);
        return ESP_FAIL;
    }
    uint64_t total = 0, freeb = 0;
    esp_err_t rc = esp_vfs_fat_info("/sd", &total, &freeb);
    if (rc != ESP_OK) {
        return rc;
    }
    if (out_free_bytes) {
        *out_free_bytes = freeb;
    }
    return ESP_OK;
}

esp_err_t audio_recorder_sink_open(audio_recorder_sink_t *s, uint32_t sample_rate,
                                   uint32_t boot_id, uint32_t session)
{
    if (!s || sample_rate == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    stage_release(s);            /* never leak a buffer across sessions */
    memset(s, 0, sizeof(*s));
    s->boot_id = boot_id;
    s->session = session;
    s->segment = 0u;
    return open_segment(s, sample_rate);
}

/* Hand the staged bytes to the card in one sequential write.
 *
 * Must be called with the SD gate NOT held: sd_io_gate is a plain mutex, not a
 * recursive one, so nesting it would deadlock. */
static esp_err_t flush_stage(audio_recorder_sink_t *s)
{
    if (!s || !s->fp || !s->stage || s->stage_len == 0u) {
        return ESP_OK;
    }
    size_t len = s->stage_len;
    sd_io_gate_begin();
    size_t n = fwrite(s->stage, 1, len, s->fp);
    sd_io_gate_end();
    s->stage_len = 0u;
    if (n != len) {
        ESP_LOGE(TAG, "short staged write %u/%u", (unsigned)n, (unsigned)len);
        return ESP_FAIL;
    }
    return ESP_OK;
}

static void stage_release(audio_recorder_sink_t *s)
{
    if (s && s->stage) {
        heap_caps_free(s->stage);
        s->stage = NULL;
        s->stage_len = 0u;
        s->stage_cap = 0u;
    }
}

esp_err_t audio_recorder_sink_write_block(audio_recorder_sink_t *s,
                                          const int16_t *samples, uint32_t frames,
                                          uint32_t sample_rate)
{
    if (!s || !s->is_open || !samples) {
        return ESP_ERR_INVALID_STATE;
    }
    size_t len = (size_t)frames * AUDIO_RECORDER_WAV_FRAME_BYTES;
    if (len == 0u) {
        return ESP_OK;
    }

    /* Allocated lazily so a rollover that released it simply takes a new one.
     * PSRAM: this is a throughput buffer, not something the audio task touches,
     * and internal RAM is far too scarce to spend 64 KiB on it. */
    if (!s->stage) {
        s->stage = heap_caps_malloc(AUDIO_RECORDER_SINK_STAGE_BYTES,
                                    MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (s->stage) {
            s->stage_cap = AUDIO_RECORDER_SINK_STAGE_BYTES;
            s->stage_len = 0u;
        }
        /* No buffer is not fatal — fall through to the direct write below. */
    }

    /* Roll to a new segment on a rate change or when the 1 GiB cap is reached.
     * Never place two PCM rates in one WAV file. */
    if (sample_rate != s->sample_rate ||
        s->data_bytes + len > AUDIO_RECORDER_SEGMENT_DATA_MAX) {
        /* finalize() flushes, but do it here too so the ordering is obvious. */
        esp_err_t rc = flush_stage(s);
        if (rc != ESP_OK) {
            return rc;
        }
        rc = audio_recorder_sink_finalize(s);
        if (rc != ESP_OK) {
            return rc;
        }
        s->segment++;
        rc = open_segment(s, sample_rate);
        if (rc != ESP_OK) {
            return rc;
        }
    }

    if (s->stage && len <= s->stage_cap) {
        if (s->stage_len + len > s->stage_cap) {
            esp_err_t rc = flush_stage(s);
            if (rc != ESP_OK) {
                return rc;
            }
        }
        memcpy(s->stage + s->stage_len, samples, len);
        s->stage_len += len;
        /* Counted on staging, not on flush: every header patch flushes first,
         * so the size written into the WAV always matches the file on disk. */
        s->data_bytes += len;
        return ESP_OK;
    }

    sd_io_gate_begin();
    size_t n = fwrite(samples, 1, len, s->fp);
    sd_io_gate_end();
    if (n != len) {
        ESP_LOGE(TAG, "short write %u/%u", (unsigned)n, (unsigned)len);
        return ESP_FAIL;
    }
    s->data_bytes += len;
    return ESP_OK;
}

esp_err_t audio_recorder_sink_checkpoint(audio_recorder_sink_t *s)
{
    if (!s || !s->is_open) {
        return ESP_OK;
    }
    /* Outside the gate: it is a plain mutex and the section below takes it. */
    esp_err_t frc = flush_stage(s);
    if (frc != ESP_OK) {
        return frc;
    }
    sd_io_gate_begin();
    esp_err_t rc = patch_header_locked(s->fp, s->sample_rate, (uint32_t)s->data_bytes);
    if (rc == ESP_OK) {
        fsync(fileno(s->fp));
    }
    sd_io_gate_end();
    return rc;
}

esp_err_t audio_recorder_sink_finalize(audio_recorder_sink_t *s)
{
    if (!s || !s->is_open) {
        stage_release(s);
        return ESP_OK;
    }

    esp_err_t frc = flush_stage(s);
    if (frc != ESP_OK) {
        ESP_LOGE(TAG, "final staged flush failed; segment may be short");
    }

    sd_io_gate_begin();
    esp_err_t rc = patch_header_locked(s->fp, s->sample_rate, (uint32_t)s->data_bytes);
    if (rc == ESP_OK) {
        fsync(fileno(s->fp));
    }
    fclose(s->fp);
    sd_io_gate_end();
    s->fp = NULL;
    s->is_open = false;

    /* Atomically publish the finished file: strip the ".part" suffix. */
    char final_path[AUDIO_RECORDER_SINK_PATH_MAX];
    size_t plen = strlen(s->part_path);
    const char *suffix = ".part";
    size_t slen = strlen(suffix);
    if (plen > slen && strcmp(s->part_path + plen - slen, suffix) == 0 &&
        plen - slen < sizeof(final_path)) {
        memcpy(final_path, s->part_path, plen - slen);
        final_path[plen - slen] = '\0';
        sd_io_gate_begin();
        int rrc = rename(s->part_path, final_path);
        sd_io_gate_end();
        if (rrc != 0) {
            ESP_LOGE(TAG, "rename %s failed", s->part_path);
            stage_release(s);
            return ESP_FAIL;
        }
        ESP_LOGI(TAG, "segment finalized: %s (%llu B)", final_path,
                 (unsigned long long)s->data_bytes);
    }
    stage_release(s);
    return rc;
}

esp_err_t audio_recorder_sink_free_bytes(uint64_t *out_free_bytes)
{
    uint64_t total = 0, freeb = 0;
    esp_err_t rc = esp_vfs_fat_info("/sd", &total, &freeb);
    if (rc == ESP_OK && out_free_bytes) {
        *out_free_bytes = freeb;
    }
    return rc;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t ls = strlen(s), lf = strlen(suffix);
    return ls > lf && strcmp(s + ls - lf, suffix) == 0;
}

/* Recover a single orphan .wav.part. The caller holds sd_io_gate. */
static void recover_one(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return;
    }
    uint32_t data_bytes = 0u;
    if (!audio_recorder_wav_recover_data_bytes((uint64_t)st.st_size, &data_bytes)) {
        remove(path);   /* no complete frame -> drop the empty placeholder */
        return;
    }

    FILE *f = fopen(path, "r+b");
    if (!f) {
        return;
    }
    uint8_t hdr[AUDIO_RECORDER_WAV_HEADER_BYTES];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(f);   /* not one of our placeholders; leave it alone */
        return;
    }
    audio_recorder_wav_patch_sizes(hdr, data_bytes);
    fflush(f);
    if (fseek(f, 0, SEEK_SET) == 0) {
        (void)fwrite(hdr, 1, sizeof(hdr), f);
    }
    fflush(f);
    (void)ftruncate(fileno(f), (off_t)(AUDIO_RECORDER_WAV_HEADER_BYTES + data_bytes));
    fsync(fileno(f));
    fclose(f);

    /* Publish as *.recovered.wav (never overwrite an existing final .wav). */
    char final_path[AUDIO_RECORDER_SINK_PATH_MAX];
    const char *suffix = ".wav.part";
    size_t plen = strlen(path);
    size_t slen = strlen(suffix);
    if (ends_with(path, suffix) &&
        (plen - slen) + strlen(".recovered.wav") < sizeof(final_path)) {
        memcpy(final_path, path, plen - slen);
        final_path[plen - slen] = '\0';
        strncat(final_path, ".recovered.wav",
                sizeof(final_path) - strlen(final_path) - 1u);
        if (rename(path, final_path) == 0) {
            ESP_LOGI(TAG, "recovered %s (%u B)", final_path, (unsigned)data_bytes);
        }
    }
}

esp_err_t audio_recorder_sink_recover_orphans(void)
{
    struct stat st;
    if (stat(AUDIO_RECORDER_SINK_DIR, &st) != 0 || !S_ISDIR(st.st_mode)) {
        return ESP_OK;   /* no recordings dir yet */
    }
    DIR *d = opendir(AUDIO_RECORDER_SINK_DIR);
    if (!d) {
        return ESP_OK;
    }
    int recovered = 0;
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
        if (!ends_with(e->d_name, ".wav.part")) {
            continue;
        }
        char path[AUDIO_RECORDER_SINK_PATH_MAX];
        if (snprintf(path, sizeof(path), "%s/%s", AUDIO_RECORDER_SINK_DIR,
                     e->d_name) >= (int)sizeof(path)) {
            continue;
        }
        sd_io_gate_begin();
        recover_one(path);
        sd_io_gate_end();
        recovered++;
    }
    closedir(d);
    if (recovered > 0) {
        ESP_LOGI(TAG, "scanned %d orphan .part file(s)", recovered);
        /* Journalled so a power-loss recovery is provable after the fact,
         * without pulling the card. */
        service_log_event(SERVICE_LOG_RECORDING_RECOVERED, SERVICE_LOG_WARN,
                          1u, (uint32_t)recovered, 0u, 0u, 0u,
                          "orphan .part recovered");
    }
    return ESP_OK;
}
