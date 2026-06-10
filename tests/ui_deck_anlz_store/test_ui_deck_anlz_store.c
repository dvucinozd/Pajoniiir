#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_deck_anlz_store.h"

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

    const anlz_metadata_t *stored1 = ui_deck_anlz_store_get(&store, 0);
    const anlz_metadata_t *stored2 = ui_deck_anlz_store_get(&store, 1);

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

    assert(ui_deck_anlz_store_get(&store, 0) == NULL);
    const anlz_metadata_t *stored2 = ui_deck_anlz_store_get(&store, 1);
    assert(stored2);
    assert(stored2->bpm == 130);
    assert(stored2->beats[0].time_ms == 4000);

    anlz_free(&deck1);
    anlz_free(&deck2);
    ui_deck_anlz_store_clear_all(&store);
}

int main(void)
{
    test_deck_slots_keep_independent_deep_copies();
    test_clearing_one_deck_preserves_other_deck();

    puts("ui_deck_anlz_store tests passed");
    return 0;
}
