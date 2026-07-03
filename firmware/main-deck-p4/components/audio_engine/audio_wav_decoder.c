#include "audio_decoder.h"

#include <stdbool.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "media_io_gate.h"

#define WAV_READ_FRAMES 512u

typedef struct {
    FILE *fp;
    long data_offset;
    uint32_t data_size;
    uint16_t block_align;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint64_t current_frame;
    uint64_t total_frames;
} wav_decoder_impl_t;

static uint16_t rd_u16le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd_u32le(const uint8_t *p)
{
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

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

static esp_err_t wav_parse(audio_decoder_t *dec, wav_decoder_impl_t *w)
{
    uint8_t hdr[12];
    if (!read_exact(w->fp, hdr, sizeof(hdr))) {
        return ESP_FAIL;
    }
    if (audio_format_detect_header(hdr, sizeof(hdr)) != AUDIO_FORMAT_WAV) {
        return ESP_ERR_NOT_SUPPORTED;
    }

    bool have_fmt = false;
    bool have_data = false;
    uint16_t audio_format = 0;
    uint32_t sample_rate = 0;

    while (!have_data) {
        uint8_t chunk[8];
        if (!read_exact(w->fp, chunk, sizeof(chunk))) {
            break;
        }
        uint32_t chunk_size = rd_u32le(chunk + 4);
        long chunk_payload = tell_file(w->fp);
        if (chunk_payload < 0) {
            return ESP_FAIL;
        }

        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[16];
            if (chunk_size < sizeof(fmt) || !read_exact(w->fp, fmt, sizeof(fmt))) {
                return ESP_FAIL;
            }
            audio_format = rd_u16le(fmt + 0);
            w->channels = rd_u16le(fmt + 2);
            sample_rate = rd_u32le(fmt + 4);
            w->block_align = rd_u16le(fmt + 12);
            w->bits_per_sample = rd_u16le(fmt + 14);
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            w->data_offset = chunk_payload;
            w->data_size = chunk_size;
            have_data = true;
        }

        long next = chunk_payload + (long)chunk_size + (long)(chunk_size & 1u);
        if (!seek_file(w->fp, next, SEEK_SET)) {
            return ESP_FAIL;
        }
    }

    if (!have_fmt || !have_data || audio_format != 1) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if ((w->channels != 1u && w->channels != 2u) || w->bits_per_sample != 16u) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    if (w->block_align != (uint16_t)(w->channels * 2u) || w->block_align == 0) {
        return ESP_FAIL;
    }

    w->total_frames = w->data_size / w->block_align;
    dec->info.format = AUDIO_FORMAT_WAV;
    dec->info.sample_rate = sample_rate;
    dec->info.channels = (uint8_t)w->channels;
    dec->info.bits_per_sample = (uint8_t)w->bits_per_sample;
    dec->info.total_frames = w->total_frames;
    return seek_file(w->fp, w->data_offset, SEEK_SET) ? ESP_OK : ESP_FAIL;
}

static esp_err_t wav_read_pcm_s16(audio_decoder_t *dec,
                                  int16_t *out,
                                  size_t frames_requested,
                                  size_t *frames_read)
{
    wav_decoder_impl_t *w = (wav_decoder_impl_t *)dec->impl;
    uint8_t blk[WAV_READ_FRAMES * 4u];
    size_t produced = 0;

    while (produced < frames_requested && w->current_frame < w->total_frames) {
        size_t remaining = frames_requested - produced;
        size_t batch = remaining < WAV_READ_FRAMES ? remaining : WAV_READ_FRAMES;
        uint64_t frames_left = w->total_frames - w->current_frame;
        if ((uint64_t)batch > frames_left) {
            batch = (size_t)frames_left;
        }

        size_t want = batch * w->block_align;
        media_io_gate_begin();
        size_t got = fread(blk, 1, want, w->fp);
        media_io_gate_end();
        size_t got_frames = got / w->block_align;
        if (got_frames == 0) {
            break;
        }

        for (size_t i = 0; i < got_frames; ++i) {
            const uint8_t *p = blk + i * w->block_align;
            if (w->channels == 1u) {
                int16_t s = (int16_t)rd_u16le(p);
                out[(produced + i) * 2u + 0u] = s;
                out[(produced + i) * 2u + 1u] = s;
            } else {
                out[(produced + i) * 2u + 0u] = (int16_t)rd_u16le(p + 0);
                out[(produced + i) * 2u + 1u] = (int16_t)rd_u16le(p + 2);
            }
        }
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
    uint64_t off64 = (uint64_t)w->data_offset + frame_index * (uint64_t)w->block_align;
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
