#pragma once
/*
 * v337 (JC1060): uncompressed PCM files beyond 16-bit WAV. Pure (host-tested):
 * the engine reads through its page cache with the read callback.
 *   - RIFF/WAVE: PCM 16 / 24 / 32-bit integer (format 1, or
 *     WAVE_FORMAT_EXTENSIBLE with the PCM sub-format), 32-bit float (format
 *     3, or EXTENSIBLE float);
 *   - FORM/AIFF: 16 / 24 / 32-bit big-endian integer;
 *   - FORM/AIFC: compression "NONE" (big-endian) or "sowt" (little-endian).
 * Mono or stereo. Samples convert to int16 with rounding (no dither); a
 * 24-bit source keeps its top 16 bits, rounded.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*audio_pcm_read_fn)(void *ctx, size_t offset, void *dst, size_t bytes);

typedef struct {
    uint32_t sample_rate;
    uint16_t channels;          /* 1 or 2 */
    uint16_t bytes_per_sample;  /* 2, 3 or 4 */
    uint16_t block_align;       /* channels * bytes_per_sample */
    bool     big_endian;
    bool     is_float;          /* 32-bit IEEE float */
    bool     aiff;
    size_t   data_offset;
    size_t   data_size;         /* whole frames only */
} audio_pcm_format_t;

typedef enum {
    AUDIO_PCM_OK = 0,
    AUDIO_PCM_NOT_PCM,          /* neither RIFF/WAVE nor FORM/AIFF(C) */
    AUDIO_PCM_UNSUPPORTED,      /* compressed, > 2 channels, other sizes */
    AUDIO_PCM_BAD_FILE,         /* truncated or inconsistent chunks */
} audio_pcm_result_t;

audio_pcm_result_t audio_pcm_parse(audio_pcm_read_fn read, void *ctx, size_t file_size,
                                   audio_pcm_format_t *out);
/* frames of raw samples to interleaved stereo int16 (mono duplicated). */
void audio_pcm_to_s16(const audio_pcm_format_t *fmt, const uint8_t *raw, size_t frames,
                      int16_t *out_stereo);
/* True for a FORM/AIFF or FORM/AIFC header (12 bytes). */
bool audio_pcm_is_aiff_header(const uint8_t *data, size_t len);
