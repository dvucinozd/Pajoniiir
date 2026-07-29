#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_deck_anlz_store.h"

extern uint32_t anlz_clone_stub_calls;
extern bool anlz_clone_stub_fail;

static anlz_metadata_t make_meta(uint16_t bpm,
                                 uint32_t first_beat_ms,
                                 uint8_t waveform_seed)
{
    anlz_metadata_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.bpm = bpm;
    meta.beat_count = 2;
    meta.beats = calloc(meta.beat_count, sizeof(*meta.beats));
    assert(meta.beats);
    meta.beats[0] = (anlz_beat_t){.beat_phase = 0, .bpm_x100 = bpm * 100u, .time_ms = first_beat_ms};
    meta.beats[1] = (anlz_beat_t){.beat_phase = 1, .bpm_x100 = bpm * 100u, .time_ms = first_beat_ms + 500u};
    meta.has_waveform_low = true;
    for (size_t i = 0; i < ANLZ_WAVEFORM_LOW_LEN; ++i) {
        meta.waveform_low[i] = (uint8_t)(waveform_seed + i);
    }
    return meta;
}

static void test_deck_slots_keep_independent_deep_copies(void)
{
    ui_deck_anlz_store_t store;
    ui_deck_anlz_store_init(&store);

    anlz_metadata_t deck1 = make_meta(120, 1000, 1);
    anlz_metadata_t deck2 = make_meta(128, 2000, 9);

    assert(ui_deck_anlz_store_set(&store, 0, &deck1));
    assert(ui_deck_anlz_store_set(&store, 1, &deck2));

    deck1.beats[0].time_ms = 9999;
    deck1.waveform_low[0] = 77;
    deck2.beats[0].time_ms = 8888;
    deck2.waveform_low[0] = 88;

    anlz_snapshot_t *snapshot1 =
        ui_deck_anlz_store_acquire(&store, 0);
    anlz_snapshot_t *snapshot2 =
        ui_deck_anlz_store_acquire(&store, 1);
    const anlz_metadata_t *stored1 =
        anlz_snapshot_metadata(snapshot1);
    const anlz_metadata_t *stored2 =
        anlz_snapshot_metadata(snapshot2);

    assert(stored1);
    assert(stored2);
    assert(stored1->bpm == 120);
    assert(stored2->bpm == 128);
    assert(stored1->beats[0].time_ms == 1000);
    assert(stored2->beats[0].time_ms == 2000);
    assert(stored1->waveform_low[0] == 1);
    assert(stored2->waveform_low[0] == 9);
    assert(stored1->beats != deck1.beats);
    assert(stored2->beats != deck2.beats);

    anlz_snapshot_release(snapshot1);
    anlz_snapshot_release(snapshot2);
    anlz_free(&deck1);
    anlz_free(&deck2);
    ui_deck_anlz_store_clear_all(&store);
}

static void test_clearing_one_deck_preserves_other_deck(void)
{
    ui_deck_anlz_store_t store;
    ui_deck_anlz_store_init(&store);

    anlz_metadata_t deck1 = make_meta(122, 3000, 3);
    anlz_metadata_t deck2 = make_meta(130, 4000, 4);

    assert(ui_deck_anlz_store_set(&store, 0, &deck1));
    assert(ui_deck_anlz_store_set(&store, 1, &deck2));

    ui_deck_anlz_store_clear(&store, 0);

    assert(ui_deck_anlz_store_acquire(&store, 0) == NULL);
    anlz_snapshot_t *snapshot2 =
        ui_deck_anlz_store_acquire(&store, 1);
    const anlz_metadata_t *stored2 =
        anlz_snapshot_metadata(snapshot2);
    assert(stored2);
    assert(stored2->bpm == 130);
    assert(stored2->beats[0].time_ms == 4000);

    anlz_snapshot_release(snapshot2);
    anlz_free(&deck1);
    anlz_free(&deck2);
    ui_deck_anlz_store_clear_all(&store);
}

static void test_acquire_is_allocation_free_and_old_snapshot_survives_swap(void)
{
    ui_deck_anlz_store_t store;
    ui_deck_anlz_store_init(&store);
    anlz_metadata_t first = make_meta(120, 1000, 1);
    anlz_metadata_t second = make_meta(135, 2000, 2);
    uint8_t first_high[] = {9, 8, 7, 6};
    uint8_t second_high[] = {1, 2, 3};
    first.waveform_high = first_high;
    first.waveform_high_len = sizeof(first_high);
    second.waveform_high = second_high;
    second.waveform_high_len = sizeof(second_high);

    anlz_clone_stub_calls = 0u;
    assert(ui_deck_anlz_store_set(&store, 0, &first));
    assert(anlz_clone_stub_calls == 1u);
    anlz_snapshot_t *old = ui_deck_anlz_store_acquire(&store, 0);
    anlz_snapshot_t *second_reader =
        ui_deck_anlz_store_acquire(&store, 0);
    assert(old == second_reader);
    assert(anlz_clone_stub_calls == 1u);
    uint32_t old_version = anlz_snapshot_version(old);

    assert(ui_deck_anlz_store_set(&store, 0, &second));
    assert(anlz_clone_stub_calls == 2u);
    assert(anlz_snapshot_metadata(old)->beats[0].time_ms == 1000u);
    assert(anlz_snapshot_metadata(old)->waveform_high[0] == 9u);

    anlz_snapshot_t *current =
        ui_deck_anlz_store_acquire(&store, 0);
    assert(anlz_clone_stub_calls == 2u);
    assert(anlz_snapshot_metadata(current)->beats[0].time_ms == 2000u);
    assert(anlz_snapshot_metadata(current)->waveform_high[0] == 1u);
    assert(anlz_snapshot_version(current) != old_version);

    anlz_snapshot_release(old);
    anlz_snapshot_release(second_reader);
    anlz_snapshot_release(current);
    ui_deck_anlz_store_clear_all(&store);
    free(first.beats);
    free(second.beats);
}

static void test_oom_preserves_current_snapshot(void)
{
    ui_deck_anlz_store_t store;
    ui_deck_anlz_store_init(&store);
    anlz_metadata_t first = make_meta(120, 1000, 1);
    anlz_metadata_t failed = make_meta(140, 2000, 2);

    assert(ui_deck_anlz_store_set(&store, 0, &first));
    anlz_snapshot_t *before =
        ui_deck_anlz_store_acquire(&store, 0);
    uint32_t before_version = anlz_snapshot_version(before);
    anlz_snapshot_release(before);

    anlz_clone_stub_fail = true;
    assert(!ui_deck_anlz_store_set(&store, 0, &failed));
    anlz_clone_stub_fail = false;

    anlz_snapshot_t *after =
        ui_deck_anlz_store_acquire(&store, 0);
    assert(after);
    assert(anlz_snapshot_version(after) == before_version);
    assert(anlz_snapshot_metadata(after)->bpm == 120u);
    assert(anlz_snapshot_metadata(after)->beats[0].time_ms == 1000u);

    anlz_snapshot_release(after);
    ui_deck_anlz_store_clear_all(&store);
    anlz_free(&first);
    anlz_free(&failed);
}

static void test_max_payload_and_ten_thousand_acquires_do_not_clone(void)
{
    ui_deck_anlz_store_t store;
    ui_deck_anlz_store_init(&store);
    anlz_metadata_t maximum = {0};
    maximum.beat_count = UINT16_MAX;
    maximum.beats = calloc(maximum.beat_count, sizeof(*maximum.beats));
    maximum.waveform_high_len = ANLZ_WAVEFORM_HIGH_MAX;
    maximum.waveform_high = malloc(maximum.waveform_high_len);
    assert(maximum.beats);
    assert(maximum.waveform_high);
    maximum.beats[maximum.beat_count - 1u].time_ms = 0x12345678u;
    maximum.waveform_high[maximum.waveform_high_len - 1u] = 0xA5u;

    anlz_clone_stub_calls = 0u;
    assert(ui_deck_anlz_store_set(&store, 0, &maximum));
    assert(anlz_clone_stub_calls == 1u);
    for (uint32_t i = 0u; i < 10000u; ++i) {
        anlz_snapshot_t *snapshot =
            ui_deck_anlz_store_acquire(&store, 0);
        assert(snapshot);
        const anlz_metadata_t *meta =
            anlz_snapshot_metadata(snapshot);
        assert(meta->beat_count == UINT16_MAX);
        assert(meta->beats[meta->beat_count - 1u].time_ms == 0x12345678u);
        assert(meta->waveform_high_len == ANLZ_WAVEFORM_HIGH_MAX);
        assert(meta->waveform_high[meta->waveform_high_len - 1u] == 0xA5u);
        anlz_snapshot_release(snapshot);
    }
    assert(anlz_clone_stub_calls == 1u);

    ui_deck_anlz_store_clear_all(&store);
    anlz_free(&maximum);
}

int main(void)
{
    test_deck_slots_keep_independent_deep_copies();
    test_clearing_one_deck_preserves_other_deck();
    test_acquire_is_allocation_free_and_old_snapshot_survives_swap();
    test_oom_preserves_current_snapshot();
    test_max_payload_and_ten_thousand_acquires_do_not_clone();

    puts("ui_deck_anlz_store tests passed");
    return 0;
}
