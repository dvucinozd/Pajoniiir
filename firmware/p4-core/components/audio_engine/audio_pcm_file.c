#include "audio_pcm_file.h"

#include <math.h>
#include <string.h>

static uint16_t rd16le(const uint8_t *p) { return (uint16_t)(p[0] | (p[1] << 8)); }
static uint32_t rd32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}
static uint16_t rd16be(const uint8_t *p) { return (uint16_t)((p[0] << 8) | p[1]); }
static uint32_t rd32be(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

/* AIFF sample rate: 80-bit IEEE extended, big-endian. Integer rates only. */
static uint32_t extended_to_u32(const uint8_t *p)
{
    const int exponent = (int)(((p[0] & 0x7fu) << 8) | p[1]) - 16383;
    uint32_t mant = rd32be(p + 2);   /* top 32 bits of the 64-bit mantissa */
    if ((p[0] & 0x80u) || exponent < 0 || exponent > 31) {
        return 0u;
    }
    return mant >> (31 - exponent);
}

bool audio_pcm_is_aiff_header(const uint8_t *data, size_t len)
{
    return data && len >= 12u && memcmp(data, "FORM", 4) == 0 &&
           (memcmp(data + 8, "AIFF", 4) == 0 || memcmp(data + 8, "AIFC", 4) == 0);
}

static audio_pcm_result_t finish(audio_pcm_format_t *f, uint16_t bits, size_t file_size)
{
    if (f->channels != 1u && f->channels != 2u) return AUDIO_PCM_UNSUPPORTED;
    if (bits != 16u && bits != 24u && bits != 32u) return AUDIO_PCM_UNSUPPORTED;
    if (f->is_float && bits != 32u) return AUDIO_PCM_UNSUPPORTED;
    if (f->sample_rate == 0u) return AUDIO_PCM_BAD_FILE;
    f->bytes_per_sample = (uint16_t)(bits / 8u);
    f->block_align = (uint16_t)(f->channels * f->bytes_per_sample);
    if (f->data_offset > file_size) return AUDIO_PCM_BAD_FILE;
    if (f->data_size > file_size - f->data_offset) {
        f->data_size = file_size - f->data_offset;   /* truncated file: what is there */
    }
    f->data_size -= f->data_size % f->block_align;
    return AUDIO_PCM_OK;
}

static audio_pcm_result_t parse_wave(audio_pcm_read_fn read, void *ctx, size_t file_size,
                                     audio_pcm_format_t *f)
{
    bool have_fmt = false, have_data = false;
    uint16_t bits = 0u;
    size_t pos = 12u;
    while (pos + 8u <= file_size && !have_data) {
        uint8_t chunk[8];
        if (!read(ctx, pos, chunk, sizeof(chunk))) return AUDIO_PCM_BAD_FILE;
        const uint32_t size = rd32le(chunk + 4);
        const size_t payload = pos + 8u;
        if (memcmp(chunk, "fmt ", 4) == 0) {
            uint8_t fmt[40];
            const size_t want = size < sizeof(fmt) ? size : sizeof(fmt);
            if (size < 16u || payload + want > file_size || !read(ctx, payload, fmt, want)) {
                return AUDIO_PCM_BAD_FILE;
            }
            uint16_t tag = rd16le(fmt);
            f->channels = rd16le(fmt + 2);
            f->sample_rate = rd32le(fmt + 4);
            bits = rd16le(fmt + 14);
            if (tag == 0xfffeu) {   /* WAVE_FORMAT_EXTENSIBLE: sub-format GUID */
                if (size < 40u) return AUDIO_PCM_BAD_FILE;
                tag = rd16le(fmt + 24);
            }
            if (tag == 3u) {
                f->is_float = true;
            } else if (tag != 1u) {
                return AUDIO_PCM_UNSUPPORTED;
            }
            have_fmt = true;
        } else if (memcmp(chunk, "data", 4) == 0) {
            f->data_offset = payload;
            f->data_size = size;
            have_data = true;
        }
        pos = payload + (size_t)size + (size_t)(size & 1u);
    }
    if (!have_fmt || !have_data) return AUDIO_PCM_BAD_FILE;
    return finish(f, bits, file_size);
}

static audio_pcm_result_t parse_aiff(audio_pcm_read_fn read, void *ctx, size_t file_size,
                                     bool aifc, audio_pcm_format_t *f)
{
    bool have_comm = false, have_ssnd = false;
    uint16_t bits = 0u;
    f->aiff = true;
    f->big_endian = true;
    size_t pos = 12u;
    while (pos + 8u <= file_size && !(have_comm && have_ssnd)) {
        uint8_t chunk[8];
        if (!read(ctx, pos, chunk, sizeof(chunk))) return AUDIO_PCM_BAD_FILE;
        const uint32_t size = rd32be(chunk + 4);
        const size_t payload = pos + 8u;
        if (memcmp(chunk, "COMM", 4) == 0) {
            uint8_t comm[22];
            const size_t want = aifc ? 22u : 18u;
            if (size < want || payload + want > file_size || !read(ctx, payload, comm, want)) {
                return AUDIO_PCM_BAD_FILE;
            }
            f->channels = rd16be(comm);
            bits = rd16be(comm + 6);
            f->sample_rate = extended_to_u32(comm + 8);
            if (aifc) {
                if (memcmp(comm + 18, "sowt", 4) == 0) {
                    f->big_endian = false;
                } else if (memcmp(comm + 18, "NONE", 4) != 0) {
                    return AUDIO_PCM_UNSUPPORTED;
                }
            }
            have_comm = true;
        } else if (memcmp(chunk, "SSND", 4) == 0) {
            uint8_t head[8];
            if (size < 8u || payload + 8u > file_size || !read(ctx, payload, head, 8u)) {
                return AUDIO_PCM_BAD_FILE;
            }
            const uint32_t offset = rd32be(head);
            if (offset > size - 8u) return AUDIO_PCM_BAD_FILE;
            f->data_offset = payload + 8u + offset;
            f->data_size = size - 8u - offset;
            have_ssnd = true;
        }
        pos = payload + (size_t)size + (size_t)(size & 1u);
    }
    if (!have_comm || !have_ssnd) return AUDIO_PCM_BAD_FILE;
    return finish(f, bits, file_size);
}

audio_pcm_result_t audio_pcm_parse(audio_pcm_read_fn read, void *ctx, size_t file_size,
                                   audio_pcm_format_t *out)
{
    uint8_t h[12];
    if (!read || !out) return AUDIO_PCM_BAD_FILE;
    memset(out, 0, sizeof(*out));
    if (file_size < sizeof(h) || !read(ctx, 0u, h, sizeof(h))) return AUDIO_PCM_NOT_PCM;
    if (memcmp(h, "RIFF", 4) == 0 && memcmp(h + 8, "WAVE", 4) == 0) {
        return parse_wave(read, ctx, file_size, out);
    }
    if (audio_pcm_is_aiff_header(h, sizeof(h))) {
        return parse_aiff(read, ctx, file_size, memcmp(h + 8, "AIFC", 4) == 0, out);
    }
    return AUDIO_PCM_NOT_PCM;
}

static int16_t round_s16(float v)
{
    if (v >= 32767.0f) return 32767;
    if (v <= -32768.0f) return -32768;
    return (int16_t)lrintf(v);
}

static int16_t sample_s16(const audio_pcm_format_t *f, const uint8_t *p)
{
    const bool be = f->big_endian;
    switch (f->bytes_per_sample) {
    case 2:
        return (int16_t)(be ? rd16be(p) : rd16le(p));
    case 3: {
        const uint32_t u = be ? ((uint32_t)p[0] << 16) | ((uint32_t)p[1] << 8) | p[2]
                              : ((uint32_t)p[2] << 16) | ((uint32_t)p[1] << 8) | p[0];
        const int32_t v = (int32_t)(u << 8) >> 8;   /* sign-extend 24 bits */
        const int32_t r = (v + 128) >> 8;            /* round to 16 bits */
        return (int16_t)(r > 32767 ? 32767 : r);
    }
    case 4: {
        const uint32_t u = be ? rd32be(p) : rd32le(p);
        if (f->is_float) {
            float x;
            memcpy(&x, &u, sizeof(x));
            return isfinite(x) ? round_s16(x * 32768.0f) : 0;
        }
        const int64_t r = ((int64_t)(int32_t)u + 32768) >> 16;
        return (int16_t)(r > 32767 ? 32767 : r);
    }
    default:
        return 0;
    }
}

void audio_pcm_to_s16(const audio_pcm_format_t *fmt, const uint8_t *raw, size_t frames,
                      int16_t *out_stereo)
{
    if (!fmt || !raw || !out_stereo) return;
    for (size_t i = 0; i < frames; i++) {
        const uint8_t *s = raw + i * fmt->block_align;
        const int16_t l = sample_s16(fmt, s);
        out_stereo[i * 2u] = l;
        out_stereo[i * 2u + 1u] = fmt->channels == 2u ? sample_s16(fmt, s + fmt->bytes_per_sample)
                                                      : l;
    }
}
