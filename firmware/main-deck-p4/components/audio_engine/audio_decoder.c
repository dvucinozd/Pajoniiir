#include "audio_decoder.h"

#include <string.h>

esp_err_t audio_decoder_open(audio_decoder_t *dec, const char *path)
{
    if (!dec || !path) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(dec, 0, sizeof(*dec));

    audio_format_t format = audio_format_detect_path(path);
    switch (format) {
    case AUDIO_FORMAT_WAV:
        return audio_wav_decoder_open(dec, path);
    case AUDIO_FORMAT_FLAC:
        return audio_flac_decoder_open(dec, path);
    case AUDIO_FORMAT_MP3:
        return ESP_ERR_NOT_SUPPORTED;
    case AUDIO_FORMAT_UNKNOWN:
    default:
        return ESP_ERR_NOT_SUPPORTED;
    }
}

esp_err_t audio_decoder_read_pcm_s16(audio_decoder_t *dec,
                                     int16_t *out_interleaved_stereo,
                                     size_t frames_requested,
                                     size_t *frames_read)
{
    if (frames_read) {
        *frames_read = 0;
    }
    if (!dec || !out_interleaved_stereo || !frames_read || !dec->ops || !dec->ops->read_pcm_s16) {
        return ESP_ERR_INVALID_ARG;
    }
    if (frames_requested == 0) {
        return ESP_OK;
    }
    return dec->ops->read_pcm_s16(dec, out_interleaved_stereo, frames_requested, frames_read);
}

esp_err_t audio_decoder_seek_frame(audio_decoder_t *dec, uint64_t frame_index)
{
    if (!dec || !dec->ops || !dec->ops->seek_frame) {
        return ESP_ERR_INVALID_ARG;
    }
    return dec->ops->seek_frame(dec, frame_index);
}

void audio_decoder_close(audio_decoder_t *dec)
{
    if (!dec) {
        return;
    }
    if (dec->ops && dec->ops->close) {
        dec->ops->close(dec);
    }
    memset(dec, 0, sizeof(*dec));
}
