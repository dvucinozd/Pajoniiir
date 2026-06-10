#include "ui_deck_anlz_store.h"

#include <stdlib.h>
#include <string.h>

static bool deck_index(uint8_t deck, uint8_t *out_idx)
{
    if (!out_idx || deck >= UI_DECK_ANLZ_STORE_DECK_COUNT) {
        return false;
    }
    *out_idx = deck;
    return true;
}

static bool clone_anlz_metadata(anlz_metadata_t *dst, const anlz_metadata_t *src)
{
    if (!dst || !src) {
        return false;
    }

    memset(dst, 0, sizeof(*dst));
    *dst = *src;
    dst->beats = NULL;
    dst->waveform_high = NULL;

    if (src->beat_count > 0) {
        if (!src->beats) {
            memset(dst, 0, sizeof(*dst));
            return false;
        }
        dst->beats = malloc((size_t)src->beat_count * sizeof(*dst->beats));
        if (!dst->beats) {
            memset(dst, 0, sizeof(*dst));
            return false;
        }
        memcpy(dst->beats, src->beats, (size_t)src->beat_count * sizeof(*dst->beats));
    }

    if (src->waveform_high_len > 0) {
        if (!src->waveform_high) {
            anlz_free(dst);
            memset(dst, 0, sizeof(*dst));
            return false;
        }
        dst->waveform_high = malloc(src->waveform_high_len);
        if (!dst->waveform_high) {
            anlz_free(dst);
            memset(dst, 0, sizeof(*dst));
            return false;
        }
        memcpy(dst->waveform_high, src->waveform_high, src->waveform_high_len);
    }

    return true;
}

void ui_deck_anlz_store_init(ui_deck_anlz_store_t *store)
{
    if (!store) {
        return;
    }
    memset(store, 0, sizeof(*store));
}

void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) {
        return;
    }

    if (store->valid[idx]) {
        anlz_free(&store->meta[idx]);
    }
    memset(&store->meta[idx], 0, sizeof(store->meta[idx]));
    store->valid[idx] = false;
}

void ui_deck_anlz_store_clear_all(ui_deck_anlz_store_t *store)
{
    if (!store) {
        return;
    }
    for (uint8_t deck = 0; deck < UI_DECK_ANLZ_STORE_DECK_COUNT; ++deck) {
        ui_deck_anlz_store_clear(store, deck);
    }
}

bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,
                            uint8_t deck,
                            const anlz_metadata_t *meta)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx) || !meta) {
        return false;
    }

    anlz_metadata_t next;
    if (!clone_anlz_metadata(&next, meta)) {
        return false;
    }

    ui_deck_anlz_store_clear(store, idx);
    store->meta[idx] = next;
    store->valid[idx] = true;
    return true;
}

const anlz_metadata_t *ui_deck_anlz_store_get(const ui_deck_anlz_store_t *store,
                                               uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx) || !store->valid[idx]) {
        return NULL;
    }
    return &store->meta[idx];
}
