#include "ui_deck_anlz_store.h"

#include <string.h>

static bool deck_index(uint8_t deck, uint8_t *out_idx)
{
    if (!out_idx || deck >= UI_DECK_ANLZ_STORE_DECK_COUNT) {
        return false;
    }
    *out_idx = deck;
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
    if (anlz_clone(meta, &next) != ESP_OK) {
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
