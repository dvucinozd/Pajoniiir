#include "audio_recorder_wav.h"

#include <stdio.h>
#include <string.h>

static void store_u16le(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
}

static void store_u32le(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

void audio_recorder_wav_build_header(uint8_t out[AUDIO_RECORDER_WAV_HEADER_BYTES],
                                     uint32_t sample_rate,
                                     uint32_t data_bytes)
{
    if (!out) {
        return;
    }
    const uint16_t channels    = (uint16_t)AUDIO_RECORDER_WAV_CHANNELS;
    const uint16_t bits        = (uint16_t)AUDIO_RECORDER_WAV_BITS;
    const uint16_t block_align = (uint16_t)AUDIO_RECORDER_WAV_FRAME_BYTES;
    const uint32_t byte_rate   = sample_rate * channels * (bits / 8u);

    memcpy(out + 0, "RIFF", 4);
    store_u32le(out + 4, 36u + data_bytes);   /* RIFF chunk size */
    memcpy(out + 8, "WAVE", 4);
    memcpy(out + 12, "fmt ", 4);
    store_u32le(out + 16, 16u);               /* fmt sub-chunk size */
    store_u16le(out + 20, 1u);                /* audio format: PCM */
    store_u16le(out + 22, channels);
    store_u32le(out + 24, sample_rate);
    store_u32le(out + 28, byte_rate);
    store_u16le(out + 32, block_align);
    store_u16le(out + 34, bits);
    memcpy(out + 36, "data", 4);
    store_u32le(out + 40, data_bytes);        /* data sub-chunk size */
}

void audio_recorder_wav_patch_sizes(uint8_t header[AUDIO_RECORDER_WAV_HEADER_BYTES],
                                    uint32_t data_bytes)
{
    if (!header) {
        return;
    }
    store_u32le(header + 4, 36u + data_bytes);
    store_u32le(header + 40, data_bytes);
}

int audio_recorder_wav_format_segment(char *out, size_t out_len,
                                      uint32_t boot_id, uint32_t session,
                                      uint32_t segment, uint32_t sample_rate)
{
    if (!out || out_len == 0u) {
        return 0;
    }
    int n = snprintf(out, out_len, "REC_B%lu_%lu_%lu_%luHz.wav.part",
                     (unsigned long)boot_id, (unsigned long)session,
                     (unsigned long)segment, (unsigned long)sample_rate);
    if (n < 0 || (size_t)n >= out_len) {
        out[0] = '\0';
        return 0;
    }
    return n;
}

bool audio_recorder_wav_recover_data_bytes(uint64_t file_size,
                                           uint32_t *out_data_bytes)
{
    if (file_size <= AUDIO_RECORDER_WAV_HEADER_BYTES) {
        return false;
    }
    uint64_t data = file_size - AUDIO_RECORDER_WAV_HEADER_BYTES;
    data -= data % AUDIO_RECORDER_WAV_FRAME_BYTES;   /* whole stereo frames only */
    if (data == 0u || data > 0xFFFFFFFFull) {
        return false;
    }
    if (out_data_bytes) {
        *out_data_bytes = (uint32_t)data;
    }
    return true;
}
