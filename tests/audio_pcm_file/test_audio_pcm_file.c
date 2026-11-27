/* PCM file parser: WAV 16/24/32/float/extensible, AIFF, AIFC. */
#include "audio_pcm_file.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint8_t s_file[4096];
static size_t s_len;

static bool rd(void *ctx, size_t off, void *dst, size_t n)
{
    (void)ctx;
    if (off > s_len || n > s_len - off) return false;
    memcpy(dst, s_file + off, n);
    return true;
}

static void put(const void *p, size_t n) { memcpy(s_file + s_len, p, n); s_len += n; }
static void le16(uint32_t v) { uint8_t b[2] = { (uint8_t)v, (uint8_t)(v >> 8) }; put(b, 2); }
static void le32(uint32_t v) { uint8_t b[4] = { (uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24) }; put(b, 4); }
static void be16(uint32_t v) { uint8_t b[2] = { (uint8_t)(v >> 8), (uint8_t)v }; put(b, 2); }
static void be32(uint32_t v) { uint8_t b[4] = { (uint8_t)(v >> 24), (uint8_t)(v >> 16), (uint8_t)(v >> 8), (uint8_t)v }; put(b, 4); }

static void wav(uint16_t tag, uint16_t ch, uint32_t rate, uint16_t bits, bool extensible,
                const uint8_t *data, size_t data_len)
{
    s_len = 0;
    put("RIFF", 4); le32(0); put("WAVE", 4);
    put("LIST", 4); le32(3); put("abc", 3); put("\0", 1);   /* odd chunk + pad */
    put("fmt ", 4); le32(extensible ? 40 : 16);
    le16(extensible ? 0xfffe : tag); le16(ch); le32(rate);
    le32(rate * ch * bits / 8); le16(ch * bits / 8); le16(bits);
    if (extensible) { le16(22); le16(bits); le32(3); le16(tag); put("\0\0\0\0\x10\0\x80\0\0\xaa\0\x38\x9b\x71", 14); }
    put("data", 4); le32((uint32_t)data_len); put(data, data_len);
}

static void aiff(bool aifc, const char *comp, uint16_t ch, uint16_t bits,
                 const uint8_t rate80[10], const uint8_t *data, size_t data_len)
{
    s_len = 0;
    put("FORM", 4); be32(0); put(aifc ? "AIFC" : "AIFF", 4);
    put("COMM", 4); be32(aifc ? 22 + 6 : 18);
    be16(ch); be32((uint32_t)(data_len / (ch * bits / 8))); be16(bits); put(rate80, 10);
    if (aifc) { put(comp, 4); put("\x04none\0", 6); }
    put("SSND", 4); be32((uint32_t)(8 + 4 + data_len)); be32(4); be32(0);
    put("pad!", 4); put(data, data_len);
}

static const uint8_t k_rate_44100[10] = { 0x40, 0x0e, 0xac, 0x44, 0, 0, 0, 0, 0, 0 };
static const uint8_t k_rate_96000[10] = { 0x40, 0x0f, 0xbb, 0x80, 0, 0, 0, 0, 0, 0 };

int main(void)
{
    audio_pcm_format_t f;
    int16_t out[16];

    /* 16-bit stereo WAV, after an odd-sized chunk. */
    const uint8_t d16[] = { 0x34, 0x12, 0x00, 0x80, 0xff, 0x7f, 0x01, 0x00 };
    wav(1, 2, 44100, 16, false, d16, sizeof(d16));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK);
    assert(f.sample_rate == 44100 && f.channels == 2 && f.bytes_per_sample == 2 &&
           f.block_align == 4 && !f.big_endian && f.data_size == 8);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 2, out);
    assert(out[0] == 0x1234 && out[1] == -32768 && out[2] == 32767 && out[3] == 1);

    /* 24-bit mono, rounded to 16 bits: 0x123480 -> 0x1235, -1 -> 0, max. */
    const uint8_t d24[] = { 0x80, 0x34, 0x12, 0xff, 0xff, 0xff, 0xff, 0xff, 0x7f };
    wav(1, 1, 96000, 24, false, d24, sizeof(d24));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK);
    assert(f.bytes_per_sample == 3 && f.block_align == 3 && f.sample_rate == 96000);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 3, out);
    assert(out[0] == 0x1235 && out[1] == 0x1235);   /* mono duplicated */
    assert(out[2] == 0 && out[4] == 32767);

    /* EXTENSIBLE 32-bit integer and float. */
    const uint8_t d32[] = { 0x00, 0x80, 0x34, 0x12, 0x00, 0x00, 0x00, 0x80 };
    wav(1, 1, 48000, 32, true, d32, sizeof(d32));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && !f.is_float);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 2, out);
    assert(out[0] == 0x1235 && out[2] == -32768);
    float fl[2] = { 0.5f, -2.0f };
    wav(3, 1, 48000, 32, true, (const uint8_t *)fl, sizeof(fl));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && f.is_float);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 2, out);
    assert(out[0] == 16384 && out[2] == -32768);

    /* Refused: ADPCM, 3 channels, 8-bit, float 24. */
    wav(2, 2, 44100, 16, false, d16, sizeof(d16));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_UNSUPPORTED);
    wav(1, 3, 44100, 16, false, d16, 6);
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_UNSUPPORTED);
    wav(1, 1, 44100, 8, false, d16, 4);
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_UNSUPPORTED);

    /* A data chunk longer than the file keeps the whole frames present. */
    wav(1, 2, 44100, 16, false, d16, sizeof(d16));
    s_len -= 3;
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && f.data_size == 4);

    /* AIFF 16-bit stereo big-endian, SSND offset 4. */
    const uint8_t a16[] = { 0x12, 0x34, 0x80, 0x00 };
    aiff(false, NULL, 2, 16, k_rate_44100, a16, sizeof(a16));
    assert(audio_pcm_is_aiff_header(s_file, s_len));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK);
    assert(f.aiff && f.big_endian && f.sample_rate == 44100 && f.channels == 2);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 1, out);
    assert(out[0] == 0x1234 && out[1] == -32768);

    /* AIFF 24-bit at 96 kHz. */
    const uint8_t a24[] = { 0x12, 0x34, 0x80, 0xff, 0xff, 0xff };
    aiff(false, NULL, 2, 24, k_rate_96000, a24, sizeof(a24));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && f.sample_rate == 96000);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 1, out);
    assert(out[0] == 0x1235 && out[1] == 0);

    /* AIFC sowt = little-endian; NONE = big-endian; other codecs refused. */
    const uint8_t s16[] = { 0x34, 0x12, 0x00, 0x80 };
    aiff(true, "sowt", 2, 16, k_rate_44100, s16, sizeof(s16));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && !f.big_endian);
    audio_pcm_to_s16(&f, s_file + f.data_offset, 1, out);
    assert(out[0] == 0x1234 && out[1] == -32768);
    aiff(true, "NONE", 2, 16, k_rate_44100, a16, sizeof(a16));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_OK && f.big_endian);
    aiff(true, "ima4", 2, 16, k_rate_44100, a16, sizeof(a16));
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_UNSUPPORTED);

    /* Neither. */
    memcpy(s_file, "ID3\4\0\0\0\0\0\0\0\0", 12);
    s_len = 12;
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_NOT_PCM);

    /* Reject non-audio chunks that extend beyond the available bytes. */
    s_len = 0;
    put("RIFF", 4); le32(12); put("WAVELIST", 8); le32(UINT32_MAX);
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_BAD_FILE);
    s_len = 0;
    put("FORM", 4); be32(12); put("AIFFANNO", 8); be32(UINT32_MAX);
    assert(audio_pcm_parse(rd, NULL, s_len, &f) == AUDIO_PCM_BAD_FILE);
    puts("audio_pcm_file tests passed");
    return 0;
}
