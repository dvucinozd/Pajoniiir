#pragma once

#include <stddef.h>
#include <stdint.h>

#include "audio_format.h"

#if defined(AUDIO_DECODER_PC_TEST)
#ifndef ESP_ERR_T_DEFINED
typedef int esp_err_t;
#define ESP_ERR_T_DEFINED
#endif
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 0x101
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_NOT_SUPPORTED 0x106
#else
#include "esp_err.h"
#endif

typedef struct {
    audio_format_t format;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
    uint64_t total_frames;
} audio_decoder_info_t;

typedef struct audio_decoder audio_decoder_t;

typedef struct {
    esp_err_t (*read_pcm_s16)(audio_decoder_t *dec,
                              int16_t *out_interleaved_stereo,
                              size_t frames_requested,
                              size_t *frames_read);
    esp_err_t (*seek_frame)(audio_decoder_t *dec, uint64_t frame_index);
    void (*close)(audio_decoder_t *dec);
} audio_decoder_ops_t;

struct audio_decoder {
    audio_decoder_info_t info;
    void *impl;
    const audio_decoder_ops_t *ops;
};

esp_err_t audio_decoder_open(audio_decoder_t *dec, const char *path);
esp_err_t audio_decoder_read_pcm_s16(audio_decoder_t *dec,
                                     int16_t *out_interleaved_stereo,
                                     size_t frames_requested,
                                     size_t *frames_read);
esp_err_t audio_decoder_seek_frame(audio_decoder_t *dec, uint64_t frame_index);
void audio_decoder_close(audio_decoder_t *dec);

esp_err_t audio_wav_decoder_open(audio_decoder_t *dec, const char *path);
esp_err_t audio_flac_decoder_open(audio_decoder_t *dec, const char *path);
