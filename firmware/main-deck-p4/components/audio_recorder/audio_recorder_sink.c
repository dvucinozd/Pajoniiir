#include "audio_recorder_sink.h"
#include "audio_recorder_wav.h"
#include "audio_recorder_finalize.h"
#include "sd_io_gate.h"
#include "service_log.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "esp_timer.h"

#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <dirent.h>

/* Write-cost split, writer-task owned. Reset by audio_recorder_sink_open(). */
static uint32_t s_gate_wait_max_us = 0u;
static uint32_t s_fwrite_max_us    = 0u;
static _Atomic uint32_t s_fsync_max_us;
uint32_t audio_recorder_sink_fsync_max_us(void) { return s_fsync_max_us; }
static bool sync_file(FILE *fp)
{
    int64_t started = esp_timer_get_time();
    bool ok = fflush(fp) == 0 && fsync(fileno(fp)) == 0;
    int64_t duration = esp_timer_get_time() - started;
    uint32_t us = duration <= 0 ? 0u : ((uint64_t)duration > UINT32_MAX ? UINT32_MAX : (uint32_t)duration);
    if (us > s_fsync_max_us) s_fsync_max_us = us;
    return ok;
}

void audio_recorder_sink_write_cost(uint32_t *out_gate_max_us,
                                    uint32_t *out_fwrite_max_us)
{
    if (out_gate_max_us)   *out_gate_max_us = s_gate_wait_max_us;
    if (out_fwrite_max_us) *out_fwrite_max_us = s_fwrite_max_us;
}

static const char *TAG = "rec_sink";

/* Patch the 44-byte header at the file head to describe `data_bytes`, restoring
 * the append position. The caller holds sd_io_gate. */
static esp_err_t patch_header_locked(FILE *fp, uint32_t sample_rate, uint32_t data_bytes)
{
    uint8_t hdr[AUDIO_RECORDER_WAV_HEADER_BYTES];
    audio_recorder_wav_build_header(hdr, sample_rate, data_bytes);
    if (fflush(fp) != 0 || fseek(fp, 0, SEEK_SET) != 0) {
        return ESP_FAIL;
    }
    size_t n = fwrite(hdr, 1, sizeof(hdr), fp);
    if (n != sizeof(hdr) || fflush(fp) != 0 || fseek(fp, 0, SEEK_END) != 0) {
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
    s_gate_wait_max_us = 0u;
    s_fwrite_max_us = 0u;
    s_fsync_max_us = 0u;
    if (!s || sample_rate == 0u) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(s, 0, sizeof(*s));
    s->boot_id = boot_id;
    s->session = session;
    s->segment = 0u;
    return open_segment(s, sample_rate);
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

    /* Roll to a new segment on a rate change or when the 1 GiB cap is reached.
     * Never place two PCM rates in one WAV file. */
    if (sample_rate != s->sample_rate ||
        s->data_bytes + len > AUDIO_RECORDER_SEGMENT_DATA_MAX) {
        esp_err_t rc = audio_recorder_sink_finalize(s);
        if (rc != ESP_OK) {
            return rc;
        }
        s->segment++;
        rc = open_segment(s, sample_rate);
        if (rc != ESP_OK) {
            return rc;
        }
    }

    /* Split the cost. Timing the whole call conflated three different things:
     * waiting for the SD gate (held by the journal writer, a diagnostic-log
     * download or this sink's own checkpoint), waiting for the FATFS volume lock
     * against writers that bypass the gate, and the card itself. That made a
     * 1 KiB write look like it took 492 ms and pointed the blame at the card —
     * which is hard to believe of a card that records 1080p60. */
    int64_t g0 = esp_timer_get_time();
    sd_io_gate_begin();
    int64_t g1 = esp_timer_get_time();
    size_t n = fwrite(samples, 1, len, s->fp);
    int64_t g2 = esp_timer_get_time();
    sd_io_gate_end();

    uint32_t gate_us = (uint32_t)(g1 - g0);
    uint32_t write_us = (uint32_t)(g2 - g1);
    if (gate_us > s_gate_wait_max_us)  s_gate_wait_max_us = gate_us;
    if (write_us > s_fwrite_max_us)    s_fwrite_max_us = write_us;

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
    /* Durability only — deliberately no header patch here.
     *
     * This used to seek to offset 0, write 44 bytes and seek back every 10 s,
     * which interrupts an otherwise strictly sequential append with a random
     * write at the far end of a file that is hundreds of MB long. That is the
     * access pattern flash controllers handle worst, and it is a plausible
     * trigger for the multi-hundred-millisecond stalls seen in the soaks.
     *
     * Nothing is lost by dropping it: audio_recorder_sink_recover_orphans()
     * already rebuilds the header of any `.part` at boot from the actual file
     * size, truncating to whole frames. The in-session patch only mattered if
     * the card were pulled and read elsewhere without this board ever booting
     * again, and finalize() still writes a correct header on the normal path. */
    sd_io_gate_begin();
    bool synced = sync_file(s->fp);
    sd_io_gate_end();
    return synced ? ESP_OK : ESP_FAIL;
}

typedef struct {
    audio_recorder_sink_t *sink;
    char final_path[AUDIO_RECORDER_SINK_PATH_MAX];
} recorder_finalize_ctx_t;

static bool recorder_finalize_patch(void *opaque)
{
    recorder_finalize_ctx_t *ctx = (recorder_finalize_ctx_t *)opaque;
    return patch_header_locked(ctx->sink->fp, ctx->sink->sample_rate,
                               (uint32_t)ctx->sink->data_bytes) == ESP_OK;
}

static bool recorder_finalize_sync(void *opaque)
{
    recorder_finalize_ctx_t *ctx = (recorder_finalize_ctx_t *)opaque;
    return sync_file(ctx->sink->fp);
}

static bool recorder_finalize_close(void *opaque)
{
    recorder_finalize_ctx_t *ctx = (recorder_finalize_ctx_t *)opaque;
    int rc = fclose(ctx->sink->fp);
    ctx->sink->fp = NULL;
    ctx->sink->is_open = false;
    return rc == 0;
}

static bool recorder_finalize_publish(void *opaque)
{
    recorder_finalize_ctx_t *ctx = (recorder_finalize_ctx_t *)opaque;
    return ctx->final_path[0] != '\0' &&
           rename(ctx->sink->part_path, ctx->final_path) == 0;
}

static esp_err_t audio_recorder_sink_close(audio_recorder_sink_t *s,
                                            bool allow_publish)
{
    if (!s || !s->is_open) return ESP_OK;

    recorder_finalize_ctx_t ctx = { .sink = s, .final_path = { 0 } };
    size_t plen = strlen(s->part_path);
    static const char suffix[] = ".part";
    size_t slen = sizeof(suffix) - 1u;
    if (allow_publish && plen > slen &&
        strcmp(s->part_path + plen - slen, suffix) == 0 &&
        plen - slen < sizeof(ctx.final_path)) {
        memcpy(ctx.final_path, s->part_path, plen - slen);
        ctx.final_path[plen - slen] = '\0';
    }

    sd_io_gate_begin();
    audio_recorder_finalize_result_t result = audio_recorder_finalize_run(
        &ctx,
        recorder_finalize_patch,
        recorder_finalize_sync,
        recorder_finalize_close,
        recorder_finalize_publish,
        allow_publish);
    sd_io_gate_end();

    if (result.failed_stage != AUDIO_RECORDER_FINALIZE_STAGE_NONE) {
        ESP_LOGE(TAG, "segment finalize failed at stage %d; keeping %s",
                 (int)result.failed_stage, s->part_path);
        return ESP_FAIL;
    }
    if (result.published) {
        ESP_LOGI(TAG, "segment finalized: %s (%llu B)", ctx.final_path,
                 (unsigned long long)s->data_bytes);
    } else {
        ESP_LOGW(TAG, "segment closed as recoverable partial: %s", s->part_path);
    }
    return ESP_OK;
}

esp_err_t audio_recorder_sink_finalize(audio_recorder_sink_t *s)
{
    return audio_recorder_sink_close(s, true);
}

esp_err_t audio_recorder_sink_abort(audio_recorder_sink_t *s)
{
    return audio_recorder_sink_close(s, false);
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
static bool recover_one(const char *path)
{
    struct stat st;
    if (stat(path, &st) != 0) {
        return false;
    }
    uint32_t data_bytes = 0u;
    if (!audio_recorder_wav_recover_data_bytes((uint64_t)st.st_size, &data_bytes)) {
        remove(path);   /* no complete frame -> drop the empty placeholder */
        return false;
    }

    FILE *f = fopen(path, "r+b");
    if (!f) {
        return false;
    }
    uint8_t hdr[AUDIO_RECORDER_WAV_HEADER_BYTES];
    if (fread(hdr, 1, sizeof(hdr), f) != sizeof(hdr) ||
        memcmp(hdr, "RIFF", 4) != 0 || memcmp(hdr + 8, "WAVE", 4) != 0) {
        fclose(f);   /* not one of our placeholders; leave it alone */
        return false;
    }
    /* Reject foreign/corrupt PCM headers before rebuilding sizes. */
    if (memcmp(hdr + 12, "fmt ", 4) != 0 || memcmp(hdr + 36, "data", 4) != 0 ||
        hdr[16] != 16 || hdr[17] || hdr[18] || hdr[19] ||
        hdr[20] != 1 || hdr[21] != 0 || hdr[22] != 2 || hdr[23] != 0 ||
        hdr[32] != 4 || hdr[33] != 0 || hdr[34] != 16 || hdr[35] != 0) {
        fclose(f); return false;
    }
    uint32_t rate = (uint32_t)hdr[24] | (uint32_t)hdr[25] << 8 |
                    (uint32_t)hdr[26] << 16 | (uint32_t)hdr[27] << 24;
    const char *suffix = ".wav.part";
    size_t plen = strlen(path), slen = strlen(suffix);
    recorder_finalize_ctx_t ctx = {0};
    if (!rate || !ends_with(path, suffix) ||
        plen - slen + strlen(".recovered.wav") >= sizeof(ctx.final_path)) {
        fclose(f); return false;
    }
    memcpy(ctx.final_path, path, plen - slen);
    strcat(ctx.final_path, ".recovered.wav");
    if (stat(ctx.final_path, &st) == 0 || fflush(f) != 0 ||
        ftruncate(fileno(f), (off_t)(AUDIO_RECORDER_WAV_HEADER_BYTES + data_bytes)) != 0) {
        fclose(f); return false; /* No overwrite or publication after IO failure. */
    }
    audio_recorder_sink_t recovered = {
        .fp = f, .sample_rate = rate, .data_bytes = data_bytes, .is_open = true
    };
    if (strlen(path) >= sizeof(recovered.part_path)) { fclose(f); return false; }
    strcpy(recovered.part_path, path);
    ctx.sink = &recovered;
    audio_recorder_finalize_result_t result = audio_recorder_finalize_run(
        &ctx, recorder_finalize_patch, recorder_finalize_sync,
        recorder_finalize_close, recorder_finalize_publish, true);
    if (result.published)
        ESP_LOGI(TAG, "recovered %s (%u B)", ctx.final_path, (unsigned)data_bytes);
    else
        ESP_LOGW(TAG, "recovery failed; partial retained: %s", path);
    return result.published;
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
        bool published = recover_one(path);
        sd_io_gate_end();
        if (published) recovered++;
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
