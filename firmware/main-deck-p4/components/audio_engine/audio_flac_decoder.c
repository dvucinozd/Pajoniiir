/*
 * audio_flac_decoder.c — FLAC decode via dr_flac (single-header, public domain).
 *
 * This translation unit holds the one DR_FLAC_IMPLEMENTATION for the whole
 * component: the PC/simulator build reaches FLAC through the audio_decoder
 * abstraction here (drflac_open_file), while the firmware decode task in
 * audio_engine.c uses drflac_open callbacks over the bounded compressed-page
 * cache and links against this same implementation.
 */

/* Native FLAC only — DJ media is never Ogg-encapsulated; drop it to save flash. */
#define DR_FLAC_NO_OGG
#define DR_FLAC_IMPLEMENTATION
#include "dr_flac.h"

#include "audio_decoder.h"

#include <stdlib.h>
#include <string.h>

/* Decode in bounded batches so the interleaved scratch fits on the stack even
 * for the maximum per-call request. */
#define FLAC_READ_BATCH_FRAMES 512u

typedef struct {
    drflac *flac;
} flac_decoder_impl_t;

/* Map dr_flac's native-channel interleaved output onto a stereo pair:
 * mono duplicates, stereo passes through, >2 channels take the first two. */
static void flac_pack_stereo(const int16_t *src, uint8_t channels,
                             int16_t *dst_stereo, size_t frames)
{
    for (size_t i = 0; i < frames; ++i) {
        if (channels == 1u) {
            int16_t s = src[i];
            dst_stereo[i * 2u + 0u] = s;
            dst_stereo[i * 2u + 1u] = s;
        } else {
            dst_stereo[i * 2u + 0u] = src[i * channels + 0u];
            dst_stereo[i * 2u + 1u] = src[i * channels + 1u];
        }
    }
}

static esp_err_t flac_read_pcm_s16(audio_decoder_t *dec,
                                   int16_t *out,
                                   size_t frames_requested,
                                   size_t *frames_read)
{
    flac_decoder_impl_t *f = (flac_decoder_impl_t *)dec->impl;
    const uint8_t channels = dec->info.channels;
    int16_t batch[FLAC_READ_BATCH_FRAMES * 2u];
    size_t produced = 0;

    while (produced < frames_requested) {
        size_t remaining = frames_requested - produced;
        drflac_uint64 want = remaining < FLAC_READ_BATCH_FRAMES
                                 ? remaining : FLAC_READ_BATCH_FRAMES;
        drflac_uint64 got = drflac_read_pcm_frames_s16(f->flac, want, batch);
        if (got == 0u) {
            break;
        }
        flac_pack_stereo(batch, channels, out + produced * 2u, (size_t)got);
        produced += (size_t)got;
    }

    *frames_read = produced;
    return produced > 0 ? ESP_OK : ESP_ERR_NOT_FOUND;
}

static esp_err_t flac_seek_frame(audio_decoder_t *dec, uint64_t frame_index)
{
    flac_decoder_impl_t *f = (flac_decoder_impl_t *)dec->impl;
    return drflac_seek_to_pcm_frame(f->flac, (drflac_uint64)frame_index)
               ? ESP_OK : ESP_FAIL;
}

static void flac_close(audio_decoder_t *dec)
{
    flac_decoder_impl_t *f = (flac_decoder_impl_t *)dec->impl;
    if (!f) {
        return;
    }
    if (f->flac) {
        drflac_close(f->flac);
    }
    free(f);
    dec->impl = NULL;
}

static const audio_decoder_ops_t s_flac_ops = {
    .read_pcm_s16 = flac_read_pcm_s16,
    .seek_frame = flac_seek_frame,
    .close = flac_close,
};

esp_err_t audio_flac_decoder_open(audio_decoder_t *dec, const char *path)
{
    if (!dec || !path) {
        return ESP_ERR_INVALID_ARG;
    }

    flac_decoder_impl_t *f = (flac_decoder_impl_t *)calloc(1, sizeof(*f));
    if (!f) {
        return ESP_ERR_NO_MEM;
    }

    f->flac = drflac_open_file(path, NULL);
    if (!f->flac) {
        free(f);
        return ESP_ERR_NOT_FOUND;
    }
    if (f->flac->channels != 1u && f->flac->channels != 2u) {
        drflac_close(f->flac);
        free(f);
        return ESP_ERR_NOT_SUPPORTED;
    }

    dec->impl = f;
    dec->ops = &s_flac_ops;
    dec->info.format = AUDIO_FORMAT_FLAC;
    dec->info.sample_rate = f->flac->sampleRate;
    dec->info.channels = (uint8_t)f->flac->channels;
    dec->info.bits_per_sample = 16u;
    dec->info.total_frames = f->flac->totalPCMFrameCount;
    return ESP_OK;
}
