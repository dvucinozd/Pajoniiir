#include "deck_loaded_track_store.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

static void writer_enter(deck_loaded_track_store_t *store)
{
#ifdef ESP_PLATFORM
    (void)xSemaphoreTake(store->mutex, portMAX_DELAY);
#else
    while (__atomic_exchange_n(&store->writer_active, true,
                               __ATOMIC_ACQ_REL)) {
    }
    while (__atomic_load_n(&store->reader_count,
                           __ATOMIC_ACQUIRE) != 0u) {
    }
#endif
}

static void writer_leave(deck_loaded_track_store_t *store)
{
#ifdef ESP_PLATFORM
    (void)xSemaphoreGive(store->mutex);
#else
    __atomic_store_n(&store->writer_active, false, __ATOMIC_RELEASE);
#endif
}

static void reader_enter(const deck_loaded_track_store_t *store)
{
    deck_loaded_track_store_t *mutable_store =
        (deck_loaded_track_store_t *)store;
#ifdef ESP_PLATFORM
    (void)xSemaphoreTake(mutable_store->mutex, portMAX_DELAY);
#else
    for (;;) {
        while (__atomic_load_n(&store->writer_active, __ATOMIC_ACQUIRE)) {
        }
        (void)__atomic_add_fetch(&mutable_store->reader_count, 1u,
                                 __ATOMIC_ACQUIRE);
        if (!__atomic_load_n(&store->writer_active, __ATOMIC_ACQUIRE)) {
            return;
        }
        (void)__atomic_sub_fetch(&mutable_store->reader_count, 1u,
                                 __ATOMIC_RELEASE);
    }
#endif
}

static void reader_leave(const deck_loaded_track_store_t *store)
{
    deck_loaded_track_store_t *mutable_store =
        (deck_loaded_track_store_t *)store;
#ifdef ESP_PLATFORM
    (void)xSemaphoreGive(mutable_store->mutex);
#else
    (void)__atomic_sub_fetch(&mutable_store->reader_count, 1u,
                             __ATOMIC_RELEASE);
#endif
}

static bool valid_deck(uint8_t deck)
{
    return deck < DECK_LOADED_TRACK_COUNT;
}

static uint32_t next_generation(deck_loaded_track_store_t *store)
{
    store->next_generation++;
    if (store->next_generation == 0u) {
        store->next_generation++;
    }
    return store->next_generation;
}

static bool snapshot_newer_than(const deck_loaded_track_store_t *store,
                                uint32_t media_generation)
{
    for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
        if (store->summary[deck].media_generation > media_generation) {
            return true;
        }
    }
    return false;
}

void deck_loaded_track_store_reset(deck_loaded_track_store_t *store)
{
    if (!store) {
        return;
    }

#ifdef ESP_PLATFORM
    if (!store->mutex) {
        store->mutex =
            xSemaphoreCreateMutexStatic(&store->mutex_storage);
        configASSERT(store->mutex);
    }
#endif

    anlz_metadata_t old[DECK_LOADED_TRACK_COUNT] = {0};
    bool old_valid[DECK_LOADED_TRACK_COUNT] = {0};
    writer_enter(store);
    for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
        if (store->meta_valid[deck]) {
            old[deck] = store->meta[deck];
            old_valid[deck] = true;
        }
        memset(&store->summary[deck], 0, sizeof(store->summary[deck]));
        memset(&store->meta[deck], 0, sizeof(store->meta[deck]));
        store->meta_valid[deck] = false;
    }
    store->next_generation = 0u;
    store->media_floor = 0u;
    writer_leave(store);

    for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
        if (old_valid[deck]) {
            anlz_free(&old[deck]);
        }
    }
}

deck_loaded_track_result_t deck_loaded_track_store_publish(
    deck_loaded_track_store_t *store,
    uint8_t deck,
    const deck_loaded_track_payload_t *payload)
{
    if (!store || !valid_deck(deck) || !payload ||
        payload->track_key == 0u) {
        return DECK_LOADED_TRACK_INVALID;
    }

    anlz_metadata_t next_meta = {0};
    bool next_meta_valid = false;
    if (payload->anlz) {
        /* deck_core consumes the beat grid and BPM only. Never duplicate the
         * potentially 128 KiB high-resolution UI waveform into this ownership
         * store; keeping it NULL also prevents per-action reader clones from
         * copying data they cannot use. */
        anlz_metadata_t deck_meta = *payload->anlz;
        deck_meta.waveform_high = NULL;
        deck_meta.waveform_high_len = 0u;
        if (anlz_clone(&deck_meta, &next_meta) != ESP_OK) {
            return DECK_LOADED_TRACK_NO_MEMORY;
        }
        next_meta_valid = true;
    }

    anlz_metadata_t old_meta = {0};
    bool old_meta_valid = false;
    deck_loaded_track_result_t result = DECK_LOADED_TRACK_OK;

    writer_enter(store);
    if (payload->media_generation < store->media_floor ||
        payload->media_generation <
            store->summary[deck].media_generation) {
        result = DECK_LOADED_TRACK_STALE;
    } else {
        if (store->meta_valid[deck]) {
            old_meta = store->meta[deck];
            old_meta_valid = true;
        }
        memset(&store->meta[deck], 0, sizeof(store->meta[deck]));
        if (next_meta_valid) {
            store->meta[deck] = next_meta;
        }
        store->meta_valid[deck] = next_meta_valid;

        uint32_t bpm_x100 = (uint32_t)payload->bpm * 100u;
        if (next_meta_valid &&
            next_meta.beats &&
            next_meta.beat_count > 0u &&
            next_meta.beats[0].bpm_x100 > 0u) {
            bpm_x100 = next_meta.beats[0].bpm_x100;
        }
        store->summary[deck] = (deck_loaded_track_summary_t) {
            .generation = next_generation(store),
            .media_generation = payload->media_generation,
            .track_key = payload->track_key,
            .duration_ms = payload->duration_ms,
            .bpm_x100 = bpm_x100,
            .bpm = payload->bpm,
            .deck = deck,
            .valid = true,
            .has_anlz = next_meta_valid,
        };
    }
    writer_leave(store);

    if (result != DECK_LOADED_TRACK_OK && next_meta_valid) {
        anlz_free(&next_meta);
    }
    if (old_meta_valid) {
        anlz_free(&old_meta);
    }
    return result;
}

deck_loaded_track_result_t deck_loaded_track_store_clear(
    deck_loaded_track_store_t *store,
    uint8_t deck,
    uint32_t media_generation)
{
    if (!store || !valid_deck(deck)) {
        return DECK_LOADED_TRACK_INVALID;
    }

    anlz_metadata_t old_meta = {0};
    bool old_meta_valid = false;
    deck_loaded_track_result_t result = DECK_LOADED_TRACK_OK;
    writer_enter(store);
    if (media_generation < store->media_floor ||
        media_generation < store->summary[deck].media_generation) {
        result = DECK_LOADED_TRACK_STALE;
    } else {
        if (store->meta_valid[deck]) {
            old_meta = store->meta[deck];
            old_meta_valid = true;
        }
        memset(&store->meta[deck], 0, sizeof(store->meta[deck]));
        store->meta_valid[deck] = false;
        store->summary[deck] = (deck_loaded_track_summary_t) {
            .generation = next_generation(store),
            .media_generation = media_generation,
            .deck = deck,
        };
    }
    writer_leave(store);

    if (old_meta_valid) {
        anlz_free(&old_meta);
    }
    return result;
}

deck_loaded_track_result_t deck_loaded_track_store_clear_all(
    deck_loaded_track_store_t *store,
    uint32_t media_generation)
{
    if (!store) {
        return DECK_LOADED_TRACK_INVALID;
    }

    anlz_metadata_t old[DECK_LOADED_TRACK_COUNT] = {0};
    bool old_valid[DECK_LOADED_TRACK_COUNT] = {0};
    deck_loaded_track_result_t result = DECK_LOADED_TRACK_OK;
    writer_enter(store);
    if (media_generation < store->media_floor ||
        snapshot_newer_than(store, media_generation)) {
        result = DECK_LOADED_TRACK_STALE;
    } else {
        store->media_floor = media_generation;
        for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
            if (store->meta_valid[deck]) {
                old[deck] = store->meta[deck];
                old_valid[deck] = true;
            }
            memset(&store->meta[deck], 0, sizeof(store->meta[deck]));
            store->meta_valid[deck] = false;
            store->summary[deck] = (deck_loaded_track_summary_t) {
                .generation = next_generation(store),
                .media_generation = media_generation,
                .deck = deck,
            };
        }
    }
    writer_leave(store);

    for (uint8_t deck = 0u; deck < DECK_LOADED_TRACK_COUNT; ++deck) {
        if (old_valid[deck]) {
            anlz_free(&old[deck]);
        }
    }
    return result;
}

bool deck_loaded_track_store_get(
    const deck_loaded_track_store_t *store,
    uint8_t deck,
    deck_loaded_track_summary_t *out)
{
    if (!store || !valid_deck(deck) || !out) {
        return false;
    }

    reader_enter(store);
    *out = store->summary[deck];
    reader_leave(store);
    return true;
}

bool deck_loaded_track_store_clone(
    const deck_loaded_track_store_t *store,
    uint8_t deck,
    deck_loaded_track_summary_t *out_summary,
    anlz_metadata_t *out_anlz)
{
    if (!store || !valid_deck(deck) || !out_summary || !out_anlz) {
        return false;
    }

    memset(out_anlz, 0, sizeof(*out_anlz));
    reader_enter(store);
    *out_summary = store->summary[deck];
    esp_err_t clone_rc = ESP_OK;
    if (out_summary->valid && store->meta_valid[deck]) {
        clone_rc = anlz_clone(&store->meta[deck], out_anlz);
    }
    reader_leave(store);

    if (!out_summary->valid || clone_rc != ESP_OK) {
        memset(out_summary, 0, sizeof(*out_summary));
        anlz_free(out_anlz);
        memset(out_anlz, 0, sizeof(*out_anlz));
        return false;
    }
    return true;
}
