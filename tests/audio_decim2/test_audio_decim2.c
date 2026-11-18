/* 2:1 half-band decimator (hi-res sources). */
#include "audio_decim2.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define IN_N 65536u
static int16_t s_buf[IN_N * 2u];

/* Level (dB re the input amplitude) of `probe_hz` in the decimated output of
 * a `tone_hz` sine sampled at `rate`. */
static double run_tone(double rate, double tone_hz, double probe_hz)
{
    const double amp = 16000.0;
    for (uint32_t n = 0; n < IN_N; n++) {
        int16_t v = (int16_t)lrint(amp * sin(2.0 * M_PI * tone_hz * n / rate));
        s_buf[n * 2u] = v;
        s_buf[n * 2u + 1u] = (int16_t)-v;
    }
    audio_decim2_t d;
    audio_decim2_reset(&d);
    size_t out = audio_decim2_process(&d, s_buf, IN_N, s_buf);   /* in place */
    assert(out == IN_N / 2u);
    const double out_rate = rate / 2.0;
    const uint32_t skip = 256u, n_an = 16384u;
    double re = 0.0, im = 0.0;
    for (uint32_t n = 0; n < n_an; n++) {
        double w = 0.5 - 0.5 * cos(2.0 * M_PI * n / (n_an - 1u));
        double v = s_buf[(skip + n) * 2u];
        re += v * w * cos(2.0 * M_PI * probe_hz * n / out_rate);
        im -= v * w * sin(2.0 * M_PI * probe_hz * n / out_rate);
    }
    return 20.0 * log10(sqrt(re * re + im * im) / (n_an / 4.0) / amp + 1e-12);
}

static void test_passband_and_alias_rejection(void)
{
    static const double rates[] = { 96000.0, 88200.0 };
    for (int r = 0; r < 2; r++) {
        const double fs = rates[r], fo = fs / 2.0;
        for (double f = 1000.0; f <= 0.20 * fs; f += 3000.0) {
            double lv = run_tone(fs, f, f);
            printf("  %5.1fk %6.0f Hz: %+.4f dB\n", fs / 1000.0, f, lv);
            assert(fabs(lv) < 0.01);
        }
        /* Content above 0.295 fs must not fold into the audible band. */
        static const double hi[] = { 30000.0, 35000.0, 40000.0 };
        for (int i = 0; i < 3; i++) {
            const double f = hi[i];
            if (f >= fs / 2.0) continue;
            double alias = fmod(f, fo);          /* where it lands at fo */
            if (alias > fo / 2.0) alias = fo - alias;
            double lv = run_tone(fs, f, alias);
            printf("  %5.1fk %6.0f Hz -> alias %6.0f Hz: %.1f dB\n", fs / 1000.0, f,
                   alias, lv);
            assert(lv < -80.0);
        }
    }
}

static void test_dc_and_rounding(void)
{
    for (uint32_t n = 0; n < 1024u; n++) {
        s_buf[n * 2u] = 12345;
        s_buf[n * 2u + 1u] = -32768;
    }
    audio_decim2_t d;
    audio_decim2_reset(&d);
    size_t out = audio_decim2_process(&d, s_buf, 1024u, s_buf);
    assert(out == 512u);
    for (size_t i = 64u; i < out; i++) {
        assert(abs(s_buf[i * 2u] - 12345) <= 1);
        assert(s_buf[i * 2u + 1u] <= -32767);   /* clamped, never wrapped */
    }
}

/* Odd chunk sizes keep the pairing across calls. */
static void test_chunking(void)
{
    audio_decim2_t a, b;
    static int16_t in[2048 * 2], out_a[2048 * 2], out_b[2048 * 2];
    for (uint32_t n = 0; n < 2048u; n++) {
        in[n * 2u] = (int16_t)(8000.0 * sin(n * 0.05));
        in[n * 2u + 1u] = (int16_t)(n * 7);
    }
    audio_decim2_reset(&a);
    audio_decim2_reset(&b);
    size_t na = audio_decim2_process(&a, in, 2048u, out_a);
    size_t nb = 0u, pos = 0u;
    static const size_t steps[] = { 1u, 7u, 300u, 33u, 1152u };
    for (size_t s = 0; pos < 2048u; s++) {
        size_t take = steps[s % 5u];
        if (take > 2048u - pos) take = 2048u - pos;
        nb += audio_decim2_process(&b, &in[pos * 2u], take, &out_b[nb * 2u]);
        pos += take;
    }
    assert(na == 1024u && nb == 1024u);
    for (size_t i = 0; i < na * 2u; i++) assert(out_a[i] == out_b[i]);
}

static void test_stages(void)
{
    assert(audio_decim2_stages(44100u) == 0 && audio_decim2_stages(48000u) == 0);
    assert(audio_decim2_stages(88200u) == 1 && audio_decim2_stages(96000u) == 1);
    assert(audio_decim2_stages(176400u) == 2 && audio_decim2_stages(192000u) == 2);
    assert(audio_decim2_stages(384000u) == -1);
}

int main(void)
{
    test_passband_and_alias_rejection();
    test_dc_and_rounding();
    test_chunking();
    test_stages();
    puts("audio_decim2 tests passed");
    return 0;
}
