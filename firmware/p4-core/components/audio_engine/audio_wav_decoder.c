#include "audio_decoder.h"
#include "audio_pcm_file.h"

#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

#include "media_io_gate.h"

#define WAV_READ_BYTES 2048u

typedef struct {
    FILE *fp;
    audio_pcm_format_t format;
    uint64_t current_frame;
    uint64_t total_frames;
} wav_decoder_impl_t;

static bool read_exact(FILE *fp, void *dst, size_t len)
{
    media_io_gate_begin();
    size_t got = fread(dst, 1, len, fp);
    media_io_gate_end();
    return got == len;
}

static bool seek_file(FILE *fp, long off, int whence)
{
    media_io_gate_begin();
    int rc = fseek(fp, off, whence);
    media_io_gate_end();
    return rc == 0;
}

static long tell_file(FILE *fp)
{
    media_io_gate_begin();
    long pos = ftell(fp);
    media_io_gate_end();
    return pos;
}

static bool read_at(void *ctx, size_t offset, void *dst, size_t len)
{
    FILE *fp = ctx;
    return offset <= LONG_MAX && seek_file(fp, (long)offset, SEEK_SET) &&
           read_exact(fp, dst, len);
}

static esp_err_t wav_parse(audio_decoder_t *dec, wav_decoder_impl_t *w)
{
    if (!seek_file(w->fp, 0, SEEK_END)) return ESP_FAIL;
    const long file_size = tell_file(w->fp);
    if (file_size < 0) return ESP_FAIL;
    audio_pcm_format_t *format = &w->format;
    const audio_pcm_result_t rc = audio_pcm_parse(read_at, w->fp, (size_t)file_size, format);
    if (rc != AUDIO_PCM_OK) return rc == AUDIO_PCM_BAD_FILE ? ESP_FAIL : ESP_ERR_NOT_SUPPORTED;

    w->total_frames = format->data_size / format->block_align;
    dec->info.format = AUDIO_FORMAT_WAV;
    dec->info.sample_rate = format->sample_rate;
    dec->info.channels = (uint8_t)format->channels;
    dec->info.bits_per_sample = (uint8_t)(format->bytes_per_sample * 8u);
    dec->info.total_frames = w->total_frames;
    return seek_file(w->fp, (long)format->data_offset, SEEK_SET) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wav_read_pcm_s16(audio_decoder_t *dec,
                                  int16_t *out,
                                  size_t frames_requested,
                                  size_t *frames_read)
{
    wav_decoder_impl_t *w = (wav_decoder_impl_t *)dec->impl;
    uint8_t blk[WAV_READ_BYTES];
    const size_t block_align = w->format.block_align;
    const size_t max_batch = sizeof(blk) / block_align;
    size_t produced = 0;

    while (produced < frames_requested && w->current_frame < w->total_frames) {
        size_t remaining = frames_requested - produced;
        size_t batch = remaining < max_batch ? remaining : max_batch;
        uint64_t frames_left = w->total_frames - w->current_frame;
        if ((uint64_t)batch > frames_left) {
            batch = (size_t)frames_left;
        }

        size_t want = batch * block_align;
        media_io_gate_begin();
        size_t got = fread(blk, 1, want, w->fp);
        media_io_gate_end();
        size_t got_frames = got / block_align;
        if (got_frames == 0) {
            break;
        }

        audio_pcm_to_s16(&w->format, blk, got_frames, out + produced * 2u);
        produced += got_frames;
        w->current_frame += got_frames;
    }

    *frames_read = produced;
    return produced > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t wav_seek_frame(audio_decoder_t *dec, uint64_t frame_index)
{
    wav_decoder_impl_t *w = (wav_decoder_impl_t *)dec->impl;
    if (frame_index > w->total_frames) {
        frame_index = w->total_frames;
    }
    uint64_t off64 = w->format.data_offset + frame_index * w->format.block_align;
    if (off64 > (uint64_t)LONG_MAX) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (!seek_file(w->fp, (long)off64, SEEK_SET)) {
        return ESP_FAIL;
    }
    w->current_frame = frame_index;
    return ESP_OK;
}

static void wav_close(audio_decoder_t *dec)
{
    wav_decoder_impl_t *w = (wav_decoder_impl_t *)dec->impl;
    if (!w) {
        return;
    }
    if (w->fp) {
        media_io_gate_begin();
        fclose(w->fp);
        media_io_gate_end();
    }
    free(w);
    dec->impl = NULL;
}

static const audio_decoder_ops_t s_wav_ops = {
    .read_pcm_s16 = wav_read_pcm_s16,
    .seek_frame = wav_seek_frame,
    .close = wav_close,
};

esp_err_t audio_wav_decoder_open(audio_decoder_t *dec, const char *path)
{
    if (!dec || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    wav_decoder_impl_t *w = (wav_decoder_impl_t *)calloc(1, sizeof(*w));
    if (!w) {
        return ESP_ERR_NO_MEM;
    }

    media_io_gate_begin();
    w->fp = fopen(path, "rb");
    media_io_gate_end();
    if (!w->fp) {
        free(w);
        return ESP_ERR_NOT_FOUND;
    }

    dec->impl = w;
    dec->ops = &s_wav_ops;
    esp_err_t rc = wav_parse(dec, w);
    if (rc != ESP_OK) {
        audio_decoder_close(dec);
        return rc;
    }
    return ESP_OK;
}
