#include "audio_decoder.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void wr_u16(FILE *fp, uint16_t v)
{
    fputc((int)(v & 0xffu), fp);
    fputc((int)((v >> 8) & 0xffu), fp);
}

static void wr_u32(FILE *fp, uint32_t v)
{
    wr_u16(fp, (uint16_t)(v & 0xffffu));
    wr_u16(fp, (uint16_t)((v >> 16) & 0xffffu));
}

static void write_wav(const char *path,
                      uint16_t channels,
                      uint16_t bits_per_sample,
                      const int16_t *samples,
                      uint32_t frame_count)
{
    FILE *fp = fopen(path, "wb");
    assert(fp);
    uint16_t block_align = (uint16_t)(channels * (bits_per_sample / 8u));
    uint32_t data_bytes = frame_count * block_align;

    fwrite("RIFF", 1, 4, fp);
    wr_u32(fp, 36u + data_bytes);
    fwrite("WAVE", 1, 4, fp);
    fwrite("fmt ", 1, 4, fp);
    wr_u32(fp, 16);
    wr_u16(fp, 1);
    wr_u16(fp, channels);
    wr_u32(fp, 44100);
    wr_u32(fp, 44100u * block_align);
    wr_u16(fp, block_align);
    wr_u16(fp, bits_per_sample);
    fwrite("data", 1, 4, fp);
    wr_u32(fp, data_bytes);

    if (bits_per_sample == 16) {
        fwrite(samples, sizeof(int16_t), (size_t)frame_count * channels, fp);
    } else {
        for (uint32_t i = 0; i < frame_count * channels; ++i) {
            int32_t s24 = ((int32_t)samples[i]) << 8;
            fputc((int)(s24 & 0xff), fp);
            fputc((int)((s24 >> 8) & 0xff), fp);
            fputc((int)((s24 >> 16) & 0xff), fp);
        }
    }
    fclose(fp);
}

static void test_open_read_seek_stereo_wav(void)
{
    const int16_t samples[] = {
        100, -100,
        200, -200,
        300, -300,
    };
    write_wav("test_stereo.wav", 2, 16, samples, 3);

    audio_decoder_t dec;
    assert(audio_decoder_open(&dec, "test_stereo.wav") == ESP_OK);
    assert(dec.info.format == AUDIO_FORMAT_WAV);
    assert(dec.info.sample_rate == 44100);
    assert(dec.info.channels == 2);
    assert(dec.info.bits_per_sample == 16);
    assert(dec.info.total_frames == 3);

    int16_t out[8] = { 0 };
    size_t frames_read = 0;
    assert(audio_decoder_read_pcm_s16(&dec, out, 2, &frames_read) == ESP_OK);
    assert(frames_read == 2);
    assert(memcmp(out, samples, sizeof(int16_t) * 4) == 0);

    assert(audio_decoder_seek_frame(&dec, 1) == ESP_OK);
    memset(out, 0, sizeof(out));
    assert(audio_decoder_read_pcm_s16(&dec, out, 4, &frames_read) == ESP_OK);
    assert(frames_read == 2);
    assert(out[0] == 200 && out[1] == -200);
    assert(out[2] == 300 && out[3] == -300);

    audio_decoder_close(&dec);
}

static void test_mono_wav_duplicates_to_stereo(void)
{
    const int16_t samples[] = { 123, -456 };
    write_wav("test_mono.wav", 1, 16, samples, 2);

    audio_decoder_t dec;
    assert(audio_decoder_open(&dec, "test_mono.wav") == ESP_OK);
    assert(dec.info.channels == 1);

    int16_t out[4] = { 0 };
    size_t frames_read = 0;
    assert(audio_decoder_read_pcm_s16(&dec, out, 2, &frames_read) == ESP_OK);
    assert(frames_read == 2);
    assert(out[0] == 123 && out[1] == 123);
    assert(out[2] == -456 && out[3] == -456);
    audio_decoder_close(&dec);
}

static void test_24bit_wav_is_rejected_in_phase_1(void)
{
    const int16_t samples[] = { 100, -100 };
    write_wav("test_24bit.wav", 2, 24, samples, 1);

    audio_decoder_t dec;
    assert(audio_decoder_open(&dec, "test_24bit.wav") == ESP_ERR_NOT_SUPPORTED);
}

int main(void)
{
    test_open_read_seek_stereo_wav();
    test_mono_wav_duplicates_to_stereo();
    test_24bit_wav_is_rejected_in_phase_1();
    puts("audio_wav_decoder tests passed");
    return 0;
}
