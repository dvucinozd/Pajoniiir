#include "ui_deck_anlz_store.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

/*
 * The firmware has one UI ANLZ store. The short gate protects only pointer
 * publication and retain; readers keep immutable data alive with a reference,
 * so writers never wait for waveform rendering or other reader work.
 */
static bool s_store_lock;

static void yield_cpu(void)
{
#ifdef ESP_PLATFORM
    taskYIELD();
#endif
}

static void store_lock(void)
{
    while (__atomic_exchange_n(&s_store_lock, true, __ATOMIC_ACQ_REL)) {
        yield_cpu();
    }
}

static void store_unlock(void)
{
    __atomic_store_n(&s_store_lock, false, __ATOMIC_RELEASE);
}

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
    store_lock();
    memset(store, 0, sizeof(*store));
    store_unlock();
}

void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) {
        return;
    }

    store_lock();
    anlz_snapshot_t *old = store->snapshot[idx];
    store->snapshot[idx] = NULL;
    store_unlock();
    anlz_snapshot_release(old);
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

    anlz_snapshot_t *next =
        anlz_snapshot_create(meta, ANLZ_SNAPSHOT_FULL);
    if (!next) {
        return false;
    }

    store_lock();
    anlz_snapshot_t *old = store->snapshot[idx];
    store->snapshot[idx] = next;
    store_unlock();
    anlz_snapshot_release(old);
    return true;
}

anlz_snapshot_t *ui_deck_anlz_store_acquire(
    const ui_deck_anlz_store_t *store,
    uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) {
        return NULL;
    }

    store_lock();
    anlz_snapshot_t *snapshot =
        anlz_snapshot_retain(store->snapshot[idx]);
    store_unlock();
    return snapshot;
}
