/* JC1060 Master Tempo WSOLA v2 (v326): contract tests, continuity, drift and
 * an objective A/B against v1 (audio_keylock.c) at tempo 1.00 / 0.94 / 1.06,
 * with and without the 44.1 -> 48 kHz rate conversion. */
#include "audio_keylock.h"
#include "audio_keylock_wsola2.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define OUT_RATE 48000.0
#define SRC_FRAMES (1u << 18)
#define FFT_N 16384u

typedef enum { SIG_CHORD, SIG_TONE_1K, SIG_TONE_12K, SIG_TONE_220, SIG_DRUM } signal_t;

static audio_mixer_frame_t s_src[SRC_FRAMES];
static uint64_t s_src_limit = SRC_FRAMES;   /* reads at or past it fail */
static uint64_t s_src_offset;               /* absolute seq of s_src[0] */
static double s_src_rate;
static signal_t s_signal;

static const double k_chord[] = { 220.0, 277.18, 329.63 };

static int16_t clamp16(double v)
{
    if (v > 32767.0) return 32767;
    if (v < -32768.0) return -32768;
    return (int16_t)lrint(v);
}

static void generate(signal_t sig, double rate)
{
    s_signal = sig;
    s_src_rate = rate;
    s_src_offset = 0u;
    s_src_limit = SRC_FRAMES;
    uint32_t rng = 12345u;
    for (uint32_t n = 0u; n < SRC_FRAMES; n++) {
        const double t = (double)n / rate;
        double l = 0.0, r = 0.0;
        switch (sig) {
        case SIG_CHORD:
            for (size_t c = 0u; c < 3u; c++) {
                for (int h = 1; h <= 40; h++) {
                    const double f = k_chord[c] * h;
                    if (f > 16000.0) break;
                    const double a = 2500.0 / h;
                    l += a * sin(2.0 * M_PI * f * t + 0.3 * h + c);
                    r += a * sin(2.0 * M_PI * f * t + 0.5 * h + c);
                }
            }
            break;
        case SIG_TONE_1K:
            l = r = 16000.0 * sin(2.0 * M_PI * 1000.0 * t);
            break;
        case SIG_TONE_12K:
            l = r = 16000.0 * sin(2.0 * M_PI * 12000.0 * t);
            break;
        case SIG_TONE_220:
            l = r = 12000.0 * sin(2.0 * M_PI * 220.0 * t);
            break;
        case SIG_DRUM: {
            /* A decaying noise hit every 250 ms over a quiet 110 Hz bass. */
            const double local = fmod(t, 0.25);
            rng = rng * 1664525u + 1013904223u;
            const double noise = (double)(rng >> 8) / 8388608.0 - 1.0;
            l = r = 20000.0 * noise * exp(-local / 0.008) +
                    1500.0 * sin(2.0 * M_PI * 110.0 * t);
            break;
        }
        }
        s_src[n].left = clamp16(l);
        s_src[n].right = clamp16(r);
    }
}

static bool read_src(void *ctx, uint64_t seq, audio_mixer_frame_t *out)
{
    (void)ctx;
    if (seq < s_src_offset || seq >= s_src_offset + s_src_limit) return false;
    *out = s_src[(seq - s_src_offset) % SRC_FRAMES];
    return true;
}

/* --- either algorithm behind one interface ------------------------------ */

typedef struct {
    int version;
    audio_keylock_t v1;
    audio_keylock2_t v2;
} kl_t;

static kl_t s_kl;   /* large: keep off the stack */

static void kl_start(kl_t *k, int version, uint64_t start, float tempo, float ratio)
{
    k->version = version;
    if (version == 1) {
        audio_keylock_reset(&k->v1, start);
        audio_keylock_configure(&k->v1, tempo, ratio);
    } else {
        audio_keylock2_reset(&k->v2, start);
        audio_keylock2_configure(&k->v2, tempo, ratio);
    }
}

static bool kl_next(kl_t *k, audio_mixer_frame_t *out, uint32_t *consumed,
                    uint64_t *play_seq)
{
    if (k->version == 1) {
        return audio_keylock_next(&k->v1, read_src, NULL, out, consumed, play_seq);
    }
    return audio_keylock2_next(&k->v2, read_src, NULL, out, consumed, play_seq);
}

/* --- analysis ------------------------------------------------------------ */

static double s_re[FFT_N], s_im[FFT_N];
static float s_out[FFT_N + 8192u];

static void fft(double *re, double *im, uint32_t n)
{
    for (uint32_t i = 1u, j = 0u; i < n; i++) {
        uint32_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) {
            double t = re[i]; re[i] = re[j]; re[j] = t;
            t = im[i]; im[i] = im[j]; im[j] = t;
        }
    }
    for (uint32_t len = 2u; len <= n; len <<= 1) {
        const double ang = -2.0 * M_PI / len;
        for (uint32_t i = 0u; i < n; i += len) {
            for (uint32_t k = 0u; k < len / 2u; k++) {
                const double wr = cos(ang * k), wi = sin(ang * k);
                const double xr = re[i + k + len / 2u] * wr - im[i + k + len / 2u] * wi;
                const double xi = re[i + k + len / 2u] * wi + im[i + k + len / 2u] * wr;
                re[i + k + len / 2u] = re[i + k] - xr;
                im[i + k + len / 2u] = im[i + k] - xi;
                re[i + k] += xr;
                im[i + k] += xi;
            }
        }
    }
}

/* Renders FFT_N output frames after a 4096-frame settle into s_out. */
static void render(int version, float tempo, float ratio)
{
    kl_start(&s_kl, version, 1000u, tempo, ratio);
    for (uint32_t n = 0u; n < 4096u + FFT_N; n++) {
        audio_mixer_frame_t f;
        uint32_t consumed = 0u;
        uint64_t play_seq = 0u;
        assert(kl_next(&s_kl, &f, &consumed, &play_seq));
        if (n >= 4096u) s_out[n - 4096u] = f.left;
    }
}

static void spectrum(void)
{
    for (uint32_t n = 0u; n < FFT_N; n++) {
        const double w = 0.5 - 0.5 * cos(2.0 * M_PI * n / (FFT_N - 1u));
        s_re[n] = s_out[n] * w;
        s_im[n] = 0.0;
    }
    fft(s_re, s_im, FFT_N);
}

static double bin_power(uint32_t b)
{
    return s_re[b] * s_re[b] + s_im[b] * s_im[b];
}

/* Energy outside the source partials (pitch must be preserved, so the
 * partials are at the source frequencies), dB relative to the total. The
 * metallic/comb artefacts and the interpolation images land there. */
static double chord_off_partial_db(void)
{
    spectrum();
    static bool on_partial[FFT_N / 2u];
    memset(on_partial, 0, sizeof(on_partial));
    for (size_t c = 0u; c < 3u; c++) {
        for (int h = 1; h <= 40; h++) {
            const double f = k_chord[c] * h;
            if (f > 16000.0) break;
            const long centre = lrint(f * FFT_N / OUT_RATE);
            for (long b = centre - 4; b <= centre + 4; b++) {
                if (b > 0 && b < (long)(FFT_N / 2u)) on_partial[b] = true;
            }
        }
    }
    double total = 0.0, off = 0.0;
    for (uint32_t b = 1u; b < FFT_N / 2u; b++) {
        const double p = bin_power(b);
        total += p;
        if (!on_partial[b]) off += p;
    }
    return 10.0 * log10(off / total + 1e-15);
}

static double peak_hz(void)
{
    spectrum();
    uint32_t best = 1u;
    for (uint32_t b = 1u; b < FFT_N / 2u; b++) {
        if (bin_power(b) > bin_power(best)) best = b;
    }
    /* Parabolic interpolation on log power. */
    const double a = log(bin_power(best - 1u) + 1e-12);
    const double m = log(bin_power(best) + 1e-12);
    const double c = log(bin_power(best + 1u) + 1e-12);
    const double d = 0.5 * (a - c) / (a - 2.0 * m + c);
    return (best + d) * OUT_RATE / FFT_N;
}

/* Level of the 12 kHz tone relative to the input (16000), dB. */
static double tone_level_db(double freq)
{
    double re = 0.0, im = 0.0;
    for (uint32_t n = 0u; n < FFT_N; n++) {
        const double w = 0.5 - 0.5 * cos(2.0 * M_PI * n / (FFT_N - 1u));
        re += s_out[n] * w * cos(2.0 * M_PI * freq * n / OUT_RATE);
        im -= s_out[n] * w * sin(2.0 * M_PI * freq * n / OUT_RATE);
    }
    return 20.0 * log10(sqrt(re * re + im * im) / (FFT_N / 4.0) / 16000.0);
}

/* Steady 1 kHz tone: depth of the 1-ms RMS envelope ripple, (max - min) /
 * mean. Grain joins that cancel show up as periodic dips (phasiness). */
static double envelope_ripple(void)
{
    double lo = 1e30, hi = 0.0, sum = 0.0;
    uint32_t windows = 0u;
    for (uint32_t w = 0u; w + 48u <= FFT_N; w += 48u) {
        double e = 0.0;
        for (uint32_t n = 0u; n < 48u; n++) e += (double)s_out[w + n] * s_out[w + n];
        e = sqrt(e / 48.0);
        if (e < lo) lo = e;
        if (e > hi) hi = e;
        sum += e;
        windows++;
    }
    return (hi - lo) / (sum / windows);
}

/* Hits heard in DRUM_SECONDS of output (1-ms peak envelope rising through
 * 8000 after staying below 3000), and the hits the consumed source held.
 * Grain repetition at slow tempo shows up as doubled hits. */
#define DRUM_FRAMES (2u * 48000u)
static float s_long[DRUM_FRAMES];

static void count_hits(int version, float tempo, float ratio, int *heard, int *expected)
{
    const double rate = ratio == 1.0f ? 48000.0 : 44100.0;
    generate(SIG_DRUM, rate);
    const uint64_t start = 1000u;
    kl_start(&s_kl, version, start, tempo, ratio);
    uint64_t play_seq = start;
    for (uint32_t n = 0u; n < 4096u + DRUM_FRAMES; n++) {
        audio_mixer_frame_t f;
        uint32_t consumed = 0u;
        assert(kl_next(&s_kl, &f, &consumed, &play_seq));
        if (n >= 4096u) s_long[n - 4096u] = f.left;
    }
    int hits = 0;
    bool armed = true;
    for (uint32_t w = 0u; w + 48u <= DRUM_FRAMES; w += 48u) {
        float peak = 0.0f;
        for (uint32_t n = 0u; n < 48u; n++) {
            const float v = fabsf(s_long[w + n]);
            if (v > peak) peak = v;
        }
        if (armed && peak > 8000.0f) {
            hits++;
            armed = false;
        } else if (peak < 3000.0f) {
            armed = true;
        }
    }
    *heard = hits;
    /* The output window covers DRUM_FRAMES * tempo * ratio source frames. */
    *expected = (int)lrint((double)DRUM_FRAMES * tempo * ratio / rate / 0.25);
}

/* --- A/B ----------------------------------------------------------------- */

typedef struct {
    double off_partial_db;
    double ripple;
    double pitch_err_pct;
    double high_db;
    int hits;
    int hits_expected;
} ab_result_t;

static ab_result_t measure(int version, float tempo, float ratio)
{
    const double rate = ratio == 1.0f ? 48000.0 : 44100.0;
    ab_result_t r;
    generate(SIG_CHORD, rate);
    render(version, tempo, ratio);
    r.off_partial_db = chord_off_partial_db();
    generate(SIG_TONE_1K, rate);
    render(version, tempo, ratio);
    r.ripple = envelope_ripple();
    r.pitch_err_pct = 100.0 * (peak_hz() - 1000.0) / 1000.0;
    generate(SIG_TONE_12K, rate);
    render(version, tempo, ratio);
    r.high_db = tone_level_db(12000.0);
    count_hits(version, tempo, ratio, &r.hits, &r.hits_expected);
    return r;
}

static void test_ab_against_v1(void)
{
    static const float tempos[] = { 1.00f, 0.94f, 1.06f };
    static const float ratios[] = { 1.0f, 44100.0f / 48000.0f };
    printf("  tempo ratio   | v1 off-partial ripple pitch  12k  hits | v2 off-partial ripple pitch  12k  hits\n");
    for (size_t ti = 0u; ti < 3u; ti++) {
        for (size_t ri = 0u; ri < 2u; ri++) {
            const ab_result_t a = measure(1, tempos[ti], ratios[ri]);
            const ab_result_t b = measure(2, tempos[ti], ratios[ri]);
            printf("  %.2f  %.4f | %7.1f dB %6.3f %+5.2f%% %5.1f %2d/%2d | %7.1f dB %6.3f %+5.2f%% %5.1f %2d/%2d\n",
                   tempos[ti], ratios[ri],
                   a.off_partial_db, a.ripple, a.pitch_err_pct, a.high_db,
                   a.hits, a.hits_expected,
                   b.off_partial_db, b.ripple, b.pitch_err_pct, b.high_db,
                   b.hits, b.hits_expected);
            if (getenv("AB_REPORT_ONLY")) continue;
            /* Absolute gates for v2 (measured: off-partial -27..-28.5 dB at
             * 0.94/1.06, -50 dB at 1.00, the analysis floor; signalsmith
             * -27..-32 dB; v1 -3..-4 dB). */
            assert(fabs(b.pitch_err_pct) < 0.05);
            assert(b.high_db > -0.5);
            assert(b.off_partial_db < (tempos[ti] == 1.0f ? -45.0 : -24.0));
            assert(b.ripple < 0.01);
            assert(abs(b.hits - b.hits_expected) <= 1);   /* no doubled hits */
            /* Never worse than v1 where v1 does any processing. */
            if (tempos[ti] != 1.0f || ratios[ri] != 1.0f) {
                assert(b.off_partial_db <= a.off_partial_db);
                assert(b.ripple <= a.ripple + 0.001);
                assert(b.high_db >= a.high_db - 0.1);
                assert(b.hits <= a.hits);
            }
        }
    }
}

/* --- contract ------------------------------------------------------------ */

static void test_unity_is_bit_exact_on_a_long_track(void)
{
    generate(SIG_CHORD, 48000.0);
    const uint64_t start = 100000000u;
    s_src_offset = start - 5000u;
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, start);
    audio_keylock2_configure(k, 1.0f, 1.0f);
    uint64_t play_seq = 0u;
    for (uint32_t i = 0u; i < 20000u; i++) {
        audio_mixer_frame_t f;
        uint32_t consumed = 0u;
        assert(audio_keylock2_next(k, read_src, NULL, &f, &consumed, &play_seq));
        assert(consumed == 1u);
        const audio_mixer_frame_t want = s_src[(start + i - s_src_offset) % SRC_FRAMES];
        assert(f.left == want.left && f.right == want.right);
    }
    assert(play_seq == start + 20000u);
    assert(k->last_candidates == 0u);
}

static int count_positive_crossings(const float *x, int n)
{
    int c = 0;
    for (int i = 1; i < n; i++) {
        if (x[i - 1] <= 0.0f && x[i] > 0.0f) c++;
    }
    return c;
}

/* Same contract as the v1 test: +10 % tempo consumes ~4506 frames for 4096
 * outputs and keeps a 1 kHz tone at 1 kHz. */
static void test_tempo_consumption_and_pitch(void)
{
    generate(SIG_TONE_1K, 48000.0);
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, 0u);
    audio_keylock2_configure(k, 1.10f, 1.0f);
    uint32_t total = 0u;
    for (int i = 0; i < 4096; i++) {
        audio_mixer_frame_t f;
        uint32_t consumed = 0u;
        assert(audio_keylock2_next(k, read_src, NULL, &f, &consumed, NULL));
        s_out[i] = f.left;
        total += consumed;
    }
    assert(total >= 4504u && total <= 4507u);
    const int crossings = count_positive_crossings(s_out + 512, 3072);
    assert(crossings >= 62 && crossings <= 66);
    /* A full search ran, bounded: coarse 2 * SEEK / DEC + 1, fine 7. */
    assert(k->last_candidates > 0u &&
           k->last_candidates <= 2u * AUDIO_KEYLOCK2_SEEK / AUDIO_KEYLOCK2_DECIMATION + 8u);
}

static void test_non_finite_configuration_is_sanitized(void)
{
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, 0u);
    audio_keylock2_configure(k, NAN, INFINITY);
    assert(k->tempo_factor == 1.0f);
    assert(k->rate_ratio == 1.0f);
    audio_keylock2_configure(k, 10.0f, 0.001f);
    assert(k->tempo_factor == 2.0f);
    assert(k->rate_ratio == 0.25f);
}

/* Near the end of the decoded PCM the reads fail: next() must return false
 * (the mixer then drains through the plain resampler), never read past it. */
static void test_end_of_source_returns_false(void)
{
    generate(SIG_CHORD, 48000.0);
    s_src_limit = 6000u;
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, 0u);
    audio_keylock2_configure(k, 1.06f, 1.0f);
    bool failed = false;
    for (uint32_t i = 0u; i < 8000u && !failed; i++) {
        audio_mixer_frame_t f;
        failed = !audio_keylock2_next(k, read_src, NULL, &f, NULL, NULL);
    }
    assert(failed);
    s_src_limit = SRC_FRAMES;
}

/* --- continuity ---------------------------------------------------------- */

/* Seek / cue / hot cue: the engine resets the key lock at the new position.
 * The first hop then plays the source from exactly there. */
static void test_reset_mid_stream_restarts_exactly(void)
{
    generate(SIG_CHORD, 48000.0);
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, 0u);
    audio_keylock2_configure(k, 0.94f, 1.0f);
    for (uint32_t i = 0u; i < 3000u; i++) {
        audio_mixer_frame_t f;
        assert(audio_keylock2_next(k, read_src, NULL, &f, NULL, NULL));
    }
    const uint64_t target = 50000u;
    audio_keylock2_reset(k, target);
    audio_keylock2_configure(k, 0.94f, 1.0f);
    for (uint32_t j = 0u; j < AUDIO_KEYLOCK2_HOP; j++) {
        audio_mixer_frame_t f;
        uint64_t play_seq = 0u;
        assert(audio_keylock2_next(k, read_src, NULL, &f, NULL, &play_seq));
        assert(f.left == s_src[target + j].left);
        assert(f.right == s_src[target + j].right);
    }
}

/* Tempo moves (fader, sync nudges) on a 220 Hz tone: no output step larger
 * than the tone's own slope allows (12000 * 2 pi 220 / 48000 = 346). */
static void test_tempo_moves_do_not_click(void)
{
    static const float ratios[] = { 1.0f, 44100.0f / 48000.0f };
    for (size_t ri = 0u; ri < 2u; ri++) {
        const double rate = ratios[ri] == 1.0f ? 48000.0 : 44100.0;
        generate(SIG_TONE_220, rate);
        audio_keylock2_t *k = &s_kl.v2;
        audio_keylock2_reset(k, 0u);
        int prev = 0;
        int worst = 0;
        for (uint32_t i = 0u; i < 96000u; i++) {
            float tempo = 1.0f;
            if (i >= 20000u && i < 40000u) tempo = 0.94f;
            else if (i >= 40000u && i < 60000u) tempo = 1.06f;
            else if (i >= 60000u) tempo = 1.0f + 0.04f * sinf((float)i * 0.0005f);
            audio_keylock2_configure(k, tempo, ratios[ri]);
            audio_mixer_frame_t f;
            assert(audio_keylock2_next(k, read_src, NULL, &f, NULL, NULL));
            if (i > 32u) {
                const int step = abs((int)f.left - prev);
                if (step > worst) worst = step;
            }
            prev = f.left;
        }
        printf("  tempo moves, ratio %.4f: largest step %d (tone slope 346)\n",
               ratios[ri], worst);
        assert(worst <= 450);
    }
}

/* --- drift --------------------------------------------------------------- */

/* Five virtual minutes at tempo 0.94 through the 44.1 -> 48 kHz conversion:
 * the reported consumption is the exact integral of tempo * ratio, play_seq
 * equals start + consumption, and the grains never leave the search window
 * around the transport. */
static void test_five_minute_drift(void)
{
    generate(SIG_CHORD, 44100.0);
    s_src_limit = UINT64_MAX / 2u;   /* the fixture repeats */
    const float tempo = 0.94f, ratio = 44100.0f / 48000.0f;
    audio_keylock2_t *k = &s_kl.v2;
    audio_keylock2_reset(k, 0u);
    audio_keylock2_configure(k, tempo, ratio);
    const uint32_t frames = 5u * 60u * 48000u;
    uint64_t total = 0u, play_seq = 0u;
    int64_t worst_gap = 0;
    const clock_t t0 = clock();
    for (uint32_t i = 0u; i < frames; i++) {
        audio_mixer_frame_t f;
        uint32_t consumed = 0u;
        assert(audio_keylock2_next(k, read_src, NULL, &f, &consumed, &play_seq));
        total += consumed;
        const int64_t gap = (int64_t)k->grain_in - (int64_t)k->logical_seq;
        if (llabs(gap) > worst_gap) worst_gap = llabs(gap);
    }
    const double host_us_per_frame = (double)(clock() - t0) * 1e6 / CLOCKS_PER_SEC / frames;
    const double expected = (double)frames * tempo * ratio;
    printf("  5 min drift: consumed %llu expected %.1f, play_seq %llu, worst grain gap %lld, host %.3f us/frame\n",
           (unsigned long long)total, expected, (unsigned long long)play_seq,
           (long long)worst_gap, host_us_per_frame);
    /* The stretched stream runs AUDIO_RESAMPLER_TAPS / 2 frames ahead of
     * the output; the float transport fraction adds at most a few frames. */
    assert(fabs((double)total - expected) <= 16.0);
    assert(play_seq == total);
    assert(worst_gap <= (int64_t)(AUDIO_KEYLOCK2_SEEK + 2u * AUDIO_KEYLOCK2_HOP));
    s_src_limit = SRC_FRAMES;
}

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    test_unity_is_bit_exact_on_a_long_track();
    test_tempo_consumption_and_pitch();
    test_non_finite_configuration_is_sanitized();
    test_end_of_source_returns_false();
    test_reset_mid_stream_restarts_exactly();
    test_tempo_moves_do_not_click();
    test_five_minute_drift();
    test_ab_against_v1();
    puts("audio_keylock_wsola2_jc1060 tests passed");
    return 0;
}
