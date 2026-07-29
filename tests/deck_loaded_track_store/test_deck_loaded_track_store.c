#include "deck_loaded_track_store.h"

#include <pthread.h>
#include <stdio.h>
#include <string.h>

extern bool anlz_clone_stub_fail;

static int s_failures;
static unsigned s_checks;

#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);               \
        s_failures++;                                                        \
    }                                                                        \
} while (0)

static anlz_metadata_t make_meta(anlz_beat_t *beats,
                                 uint16_t count,
                                 uint16_t bpm)
{
    return (anlz_metadata_t) {
        .beats = beats,
        .beat_count = count,
        .bpm = bpm,
    };
}

static deck_loaded_track_payload_t payload(uint32_t media_generation,
                                           uint32_t track_key,
                                           uint16_t bpm,
                                           const anlz_metadata_t *meta)
{
    return (deck_loaded_track_payload_t) {
        .media_generation = media_generation,
        .track_key = track_key,
        .duration_ms = track_key * 10u,
        .bpm = bpm,
        .anlz = meta,
    };
}

static void test_reset_publishes_empty_coherent_snapshots(void)
{
    puts("== reset publishes empty coherent snapshots ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_store_reset(&store);

    for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
        deck_loaded_track_summary_t summary = {.track_key = 99u};
        CHECK(deck_loaded_track_store_get(&store, deck, &summary));
        CHECK(!summary.valid);
        CHECK(summary.track_key == 0u);
        CHECK(summary.generation == 0u);
        CHECK(summary.deck == 0u);
    }
    CHECK(!deck_loaded_track_store_get(&store, 2u, NULL));
}

static void test_publish_clones_one_complete_track_generation(void)
{
    puts("== publish clones one complete track generation ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_store_reset(&store);
    anlz_beat_t beats[] = {
        {.time_ms = 100u, .bpm_x100 = 12850u},
        {.time_ms = 567u, .bpm_x100 = 12850u},
    };
    uint8_t waveform_high[] = {1u, 2u, 3u, 4u};
    anlz_metadata_t meta = make_meta(beats, 2u, 129u);
    meta.waveform_high = waveform_high;
    meta.waveform_high_len = sizeof(waveform_high);
    deck_loaded_track_payload_t input = payload(7u, 101u, 129u, &meta);

    CHECK(deck_loaded_track_store_publish(&store, 0u, &input) ==
          DECK_LOADED_TRACK_OK);
    deck_loaded_track_summary_t summary = {0};
    anlz_metadata_t snapshot = {0};
    CHECK(deck_loaded_track_store_clone(
        &store, 0u, &summary, &snapshot));
    CHECK(summary.valid);
    CHECK(summary.deck == 0u);
    CHECK(summary.media_generation == 7u);
    CHECK(summary.track_key == 101u);
    CHECK(summary.duration_ms == 1010u);
    CHECK(summary.bpm == 129u);
    CHECK(summary.bpm_x100 == 12850u);
    CHECK(summary.has_anlz);
    CHECK(summary.generation == 1u);
    CHECK(snapshot.beat_count == 2u);
    CHECK(snapshot.beats != beats);
    CHECK(snapshot.beats[0].time_ms == 100u);
    CHECK(snapshot.waveform_high == NULL);
    CHECK(snapshot.waveform_high_len == 0u);

    beats[0].time_ms = 9999u;
    CHECK(snapshot.beats[0].time_ms == 100u);
    anlz_free(&snapshot);
    deck_loaded_track_store_reset(&store);
}

static void test_load_a_then_b_never_mixes_key_bpm_and_anlz(void)
{
    puts("== load A then B never mixes key, bpm and ANLZ ==");
    deck_loaded_track_store_t store = {0};
    anlz_beat_t beats_a[] = {{.time_ms = 111u, .bpm_x100 = 12000u}};
    anlz_beat_t beats_b[] = {{.time_ms = 222u, .bpm_x100 = 13500u}};
    anlz_metadata_t meta_a = make_meta(beats_a, 1u, 120u);
    anlz_metadata_t meta_b = make_meta(beats_b, 1u, 135u);
    deck_loaded_track_payload_t a = payload(3u, 0xAAu, 120u, &meta_a);
    deck_loaded_track_payload_t b = payload(3u, 0xBBu, 135u, &meta_b);

    CHECK(deck_loaded_track_store_publish(&store, 0u, &a) ==
          DECK_LOADED_TRACK_OK);
    deck_loaded_track_summary_t first = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &first));
    CHECK(deck_loaded_track_store_publish(&store, 0u, &b) ==
          DECK_LOADED_TRACK_OK);

    deck_loaded_track_summary_t second = {0};
    anlz_metadata_t snapshot = {0};
    CHECK(deck_loaded_track_store_clone(
        &store, 0u, &second, &snapshot));
    CHECK(second.generation > first.generation);
    CHECK(second.track_key == 0xBBu);
    CHECK(second.bpm == 135u);
    CHECK(second.bpm_x100 == 13500u);
    CHECK(snapshot.bpm == 135u);
    CHECK(snapshot.beats[0].time_ms == 222u);
    CHECK(snapshot.beats[0].bpm_x100 == second.bpm_x100);
    anlz_free(&snapshot);
    deck_loaded_track_store_reset(&store);
}

static void test_usb_clear_rejects_late_old_load(void)
{
    puts("== USB clear rejects late old load ==");
    deck_loaded_track_store_t store = {0};
    anlz_beat_t beats[] = {{.time_ms = 10u, .bpm_x100 = 12000u}};
    anlz_metadata_t meta = make_meta(beats, 1u, 120u);
    deck_loaded_track_payload_t old = payload(8u, 88u, 120u, &meta);

    CHECK(deck_loaded_track_store_clear_all(&store, 9u) ==
          DECK_LOADED_TRACK_OK);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &old) ==
          DECK_LOADED_TRACK_STALE);
    deck_loaded_track_summary_t summary = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &summary));
    CHECK(!summary.valid);
    CHECK(summary.media_generation == 9u);
    CHECK(summary.track_key == 0u);
}

static void test_stale_clear_cannot_remove_newer_track(void)
{
    puts("== stale clear cannot remove newer track ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_payload_t newer = payload(12u, 120u, 124u, NULL);
    CHECK(deck_loaded_track_store_publish(&store, 1u, &newer) ==
          DECK_LOADED_TRACK_OK);
    CHECK(deck_loaded_track_store_clear(&store, 1u, 11u) ==
          DECK_LOADED_TRACK_STALE);
    CHECK(deck_loaded_track_store_clear_all(&store, 11u) ==
          DECK_LOADED_TRACK_STALE);

    deck_loaded_track_summary_t summary = {0};
    CHECK(deck_loaded_track_store_get(&store, 1u, &summary));
    CHECK(summary.valid);
    CHECK(summary.track_key == 120u);
    CHECK(summary.media_generation == 12u);
}

static void test_per_deck_clear_rejects_late_old_load(void)
{
    puts("== per-deck clear rejects late old load ==");
    deck_loaded_track_store_t store = {0};
    CHECK(deck_loaded_track_store_clear(&store, 0u, 12u) ==
          DECK_LOADED_TRACK_OK);

    deck_loaded_track_payload_t old = payload(11u, 110u, 121u, NULL);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &old) ==
          DECK_LOADED_TRACK_STALE);
    CHECK(deck_loaded_track_store_clear_all(&store, 11u) ==
          DECK_LOADED_TRACK_STALE);

    deck_loaded_track_summary_t summary = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &summary));
    CHECK(!summary.valid);
    CHECK(summary.media_generation == 12u);
    CHECK(summary.track_key == 0u);
}

static void test_clear_one_deck_preserves_the_other(void)
{
    puts("== clear one deck preserves the other ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_payload_t d1 = payload(4u, 41u, 121u, NULL);
    deck_loaded_track_payload_t d2 = payload(4u, 42u, 122u, NULL);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &d1) ==
          DECK_LOADED_TRACK_OK);
    CHECK(deck_loaded_track_store_publish(&store, 1u, &d2) ==
          DECK_LOADED_TRACK_OK);
    CHECK(deck_loaded_track_store_clear(&store, 0u, 4u) ==
          DECK_LOADED_TRACK_OK);

    deck_loaded_track_summary_t first = {0};
    deck_loaded_track_summary_t second = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &first));
    CHECK(deck_loaded_track_store_get(&store, 1u, &second));
    CHECK(!first.valid);
    CHECK(first.media_generation == 4u);
    CHECK(second.valid);
    CHECK(second.track_key == 42u);
    CHECK(second.bpm == 122u);
    CHECK(second.generation < first.generation);
}

static void test_clone_failure_never_partially_replaces_current(void)
{
    puts("== clone failure never partially replaces current ==");
    deck_loaded_track_store_t store = {0};
    anlz_beat_t beats[] = {{.time_ms = 321u, .bpm_x100 = 12300u}};
    anlz_metadata_t meta = make_meta(beats, 1u, 123u);
    deck_loaded_track_payload_t first = payload(5u, 51u, 123u, &meta);
    deck_loaded_track_payload_t failed = payload(5u, 52u, 140u, &meta);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &first) ==
          DECK_LOADED_TRACK_OK);
    deck_loaded_track_summary_t before = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &before));

    anlz_clone_stub_fail = true;
    CHECK(deck_loaded_track_store_publish(&store, 0u, &failed) ==
          DECK_LOADED_TRACK_NO_MEMORY);
    anlz_clone_stub_fail = false;

    deck_loaded_track_summary_t after = {0};
    anlz_metadata_t snapshot = {0};
    CHECK(deck_loaded_track_store_clone(
        &store, 0u, &after, &snapshot));
    CHECK(after.generation == before.generation);
    CHECK(after.track_key == 51u);
    CHECK(after.bpm == 123u);
    CHECK(snapshot.beats[0].time_ms == 321u);
    anlz_free(&snapshot);
    deck_loaded_track_store_reset(&store);
}

static void test_valid_track_without_anlz_uses_coherent_bpm_fallback(void)
{
    puts("== valid track without ANLZ uses coherent BPM fallback ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_payload_t input = payload(2u, 21u, 119u, NULL);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &input) ==
          DECK_LOADED_TRACK_OK);

    deck_loaded_track_summary_t summary = {0};
    anlz_metadata_t snapshot = {.beat_count = 99u};
    CHECK(deck_loaded_track_store_clone(
        &store, 0u, &summary, &snapshot));
    CHECK(summary.valid);
    CHECK(!summary.has_anlz);
    CHECK(summary.bpm == 119u);
    CHECK(summary.bpm_x100 == 11900u);
    CHECK(snapshot.beat_count == 0u);
}

static void test_invalid_inputs_leave_store_unchanged(void)
{
    puts("== invalid inputs leave store unchanged ==");
    deck_loaded_track_store_t store = {0};
    deck_loaded_track_payload_t invalid = payload(1u, 0u, 120u, NULL);
    CHECK(deck_loaded_track_store_publish(NULL, 0u, &invalid) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_publish(&store, 2u, &invalid) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_publish(&store, 0u, NULL) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_publish(&store, 0u, &invalid) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_clear(NULL, 0u, 0u) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_clear(&store, 2u, 0u) ==
          DECK_LOADED_TRACK_INVALID);
    CHECK(deck_loaded_track_store_clear_all(NULL, 0u) ==
          DECK_LOADED_TRACK_INVALID);
}

typedef struct {
    deck_loaded_track_store_t *store;
    bool start;
    uint32_t failures;
    uint32_t reads;
} concurrent_test_context_t;

static void *concurrent_writer(void *arg)
{
    concurrent_test_context_t *ctx = arg;
    anlz_beat_t beat_a = {.time_ms = 111u, .bpm_x100 = 12000u};
    anlz_beat_t beat_b = {.time_ms = 222u, .bpm_x100 = 13500u};
    anlz_metadata_t meta_a = make_meta(&beat_a, 1u, 120u);
    anlz_metadata_t meta_b = make_meta(&beat_b, 1u, 135u);
    deck_loaded_track_payload_t a = payload(7u, 0xAAu, 120u, &meta_a);
    deck_loaded_track_payload_t b = payload(7u, 0xBBu, 135u, &meta_b);

    __atomic_store_n(&ctx->start, true, __ATOMIC_RELEASE);
    for (uint32_t i = 0u; i < 20000u; ++i) {
        const deck_loaded_track_payload_t *next = (i & 1u) ? &b : &a;
        if (deck_loaded_track_store_publish(ctx->store, 0u, next) !=
            DECK_LOADED_TRACK_OK) {
            (void)__atomic_add_fetch(&ctx->failures, 1u, __ATOMIC_RELAXED);
        }
    }
    return NULL;
}

static void *concurrent_reader(void *arg)
{
    concurrent_test_context_t *ctx = arg;
    while (!__atomic_load_n(&ctx->start, __ATOMIC_ACQUIRE)) {
    }

    for (uint32_t i = 0u; i < 20000u; ++i) {
        deck_loaded_track_summary_t summary = {0};
        anlz_metadata_t meta = {0};
        if (!deck_loaded_track_store_clone(
                ctx->store, 0u, &summary, &meta)) {
            continue;
        }
        (void)__atomic_add_fetch(&ctx->reads, 1u, __ATOMIC_RELAXED);

        bool coherent_a =
            summary.track_key == 0xAAu &&
            summary.bpm == 120u &&
            summary.bpm_x100 == 12000u &&
            meta.beat_count == 1u &&
            meta.beats &&
            meta.beats[0].time_ms == 111u;
        bool coherent_b =
            summary.track_key == 0xBBu &&
            summary.bpm == 135u &&
            summary.bpm_x100 == 13500u &&
            meta.beat_count == 1u &&
            meta.beats &&
            meta.beats[0].time_ms == 222u;
        if (!coherent_a && !coherent_b) {
            (void)__atomic_add_fetch(&ctx->failures, 1u, __ATOMIC_RELAXED);
        }
        anlz_free(&meta);
    }
    return NULL;
}

static void test_concurrent_replace_and_clone_never_mix_generations(void)
{
    puts("== concurrent replace and clone never mix generations ==");
    deck_loaded_track_store_t store = {0};
    concurrent_test_context_t ctx = {.store = &store};
    pthread_t writer;
    pthread_t readers[2];

    CHECK(pthread_create(&writer, NULL, concurrent_writer, &ctx) == 0);
    CHECK(pthread_create(&readers[0], NULL, concurrent_reader, &ctx) == 0);
    CHECK(pthread_create(&readers[1], NULL, concurrent_reader, &ctx) == 0);
    CHECK(pthread_join(writer, NULL) == 0);
    CHECK(pthread_join(readers[0], NULL) == 0);
    CHECK(pthread_join(readers[1], NULL) == 0);
    CHECK(__atomic_load_n(&ctx.failures, __ATOMIC_RELAXED) == 0u);
    CHECK(__atomic_load_n(&ctx.reads, __ATOMIC_RELAXED) >= 1000u);

    deck_loaded_track_summary_t final = {0};
    CHECK(deck_loaded_track_store_get(&store, 0u, &final));
    CHECK(final.valid);
    CHECK(final.generation == 20000u);
    deck_loaded_track_store_reset(&store);
}

int main(void)
{
    test_reset_publishes_empty_coherent_snapshots();
    test_publish_clones_one_complete_track_generation();
    test_load_a_then_b_never_mixes_key_bpm_and_anlz();
    test_usb_clear_rejects_late_old_load();
    test_stale_clear_cannot_remove_newer_track();
    test_per_deck_clear_rejects_late_old_load();
    test_clear_one_deck_preserves_the_other();
    test_clone_failure_never_partially_replaces_current();
    test_valid_track_without_anlz_uses_coherent_bpm_fallback();
    test_invalid_inputs_leave_store_unchanged();
    test_concurrent_replace_and_clone_never_mix_generations();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("deck_loaded_track_store tests passed");
        return 0;
    }
    printf("deck_loaded_track_store tests FAILED (%d)\n", s_failures);
    return 1;
}
