#include "ui_deck_anlz_store.h"

#include <string.h>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#endif

#define ANLZ_READER_SLOTS 8u
#define ANLZ_READER_BANKS 2u

typedef struct {
    void *owner;
    anlz_metadata_t meta[UI_DECK_ANLZ_STORE_DECK_COUNT][ANLZ_READER_BANKS];
    bool valid[UI_DECK_ANLZ_STORE_DECK_COUNT][ANLZ_READER_BANKS];
    uint8_t next_bank[UI_DECK_ANLZ_STORE_DECK_COUNT];
} anlz_reader_slot_t;

/* One UI ANLZ store exists in firmware. Readers clone under an RCU-style guard;
 * writers set the gate, wait for in-flight clones to finish, then swap/free the
 * active object. Returned pointers refer to task-owned double-buffered clones,
 * never to the active object that a track change can release. */
static bool s_writer_active;
static uint32_t s_reader_count;
static bool s_slot_lock;
static anlz_reader_slot_t s_reader_slots[ANLZ_READER_SLOTS];
#ifndef ESP_PLATFORM
static int s_host_owner_token;
#endif

static void yield_cpu(void)
{
#ifdef ESP_PLATFORM
    taskYIELD();
#endif
}

static void writer_enter(void)
{
    while (__atomic_exchange_n(&s_writer_active, true, __ATOMIC_ACQ_REL)) {
        yield_cpu();
    }
    while (__atomic_load_n(&s_reader_count, __ATOMIC_ACQUIRE) != 0u) {
        yield_cpu();
    }
}

static void writer_leave(void)
{
    __atomic_store_n(&s_writer_active, false, __ATOMIC_RELEASE);
}

static void reader_enter(void)
{
    for (;;) {
        while (__atomic_load_n(&s_writer_active, __ATOMIC_ACQUIRE)) {
            yield_cpu();
        }
        (void)__atomic_add_fetch(&s_reader_count, 1u, __ATOMIC_ACQUIRE);
        if (!__atomic_load_n(&s_writer_active, __ATOMIC_ACQUIRE)) {
            return;
        }
        (void)__atomic_sub_fetch(&s_reader_count, 1u, __ATOMIC_RELEASE);
    }
}

static void reader_leave(void)
{
    (void)__atomic_sub_fetch(&s_reader_count, 1u, __ATOMIC_RELEASE);
}

static void slots_lock(void)
{
    while (__atomic_exchange_n(&s_slot_lock, true, __ATOMIC_ACQ_REL)) {
        yield_cpu();
    }
}

static void slots_unlock(void)
{
    __atomic_store_n(&s_slot_lock, false, __ATOMIC_RELEASE);
}

static void *current_owner(void)
{
#ifdef ESP_PLATFORM
    return (void *)xTaskGetCurrentTaskHandle();
#else
    return &s_host_owner_token;
#endif
}

static bool deck_index(uint8_t deck, uint8_t *out_idx)
{
    if (!out_idx || deck >= UI_DECK_ANLZ_STORE_DECK_COUNT) {
        return false;
    }
    *out_idx = deck;
    return true;
}

static anlz_reader_slot_t *reader_slot_for_owner(void *owner)
{
    anlz_reader_slot_t *empty = NULL;
    for (size_t i = 0; i < ANLZ_READER_SLOTS; ++i) {
        if (s_reader_slots[i].owner == owner) return &s_reader_slots[i];
        if (!s_reader_slots[i].owner && !empty) empty = &s_reader_slots[i];
    }
    if (empty) empty->owner = owner;
    return empty;
}

static void clear_reader_slots(void)
{
    slots_lock();
    for (size_t slot = 0; slot < ANLZ_READER_SLOTS; ++slot) {
        for (uint8_t deck = 0; deck < UI_DECK_ANLZ_STORE_DECK_COUNT; ++deck) {
            for (uint8_t bank = 0; bank < ANLZ_READER_BANKS; ++bank) {
                if (s_reader_slots[slot].valid[deck][bank]) {
                    anlz_free(&s_reader_slots[slot].meta[deck][bank]);
                }
            }
        }
        memset(&s_reader_slots[slot], 0, sizeof(s_reader_slots[slot]));
    }
    slots_unlock();
}

void ui_deck_anlz_store_init(ui_deck_anlz_store_t *store)
{
    if (!store) return;
    writer_enter();
    memset(store, 0, sizeof(*store));
    writer_leave();
    clear_reader_slots();
}

void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) return;

    anlz_metadata_t old = {0};
    bool had_old = false;
    writer_enter();
    if (store->valid[idx]) {
        old = store->meta[idx];
        had_old = true;
    }
    memset(&store->meta[idx], 0, sizeof(store->meta[idx]));
    store->valid[idx] = false;
    writer_leave();

    if (had_old) anlz_free(&old);
}

void ui_deck_anlz_store_clear_all(ui_deck_anlz_store_t *store)
{
    if (!store) return;
    for (uint8_t deck = 0; deck < UI_DECK_ANLZ_STORE_DECK_COUNT; ++deck) {
        ui_deck_anlz_store_clear(store, deck);
    }
}

bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,
                            uint8_t deck,
                            const anlz_metadata_t *meta)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx) || !meta) return false;

    anlz_metadata_t next = {0};
    if (anlz_clone(meta, &next) != ESP_OK) return false;

    anlz_metadata_t old = {0};
    bool had_old = false;
    writer_enter();
    if (store->valid[idx]) {
        old = store->meta[idx];
        had_old = true;
    }
    store->meta[idx] = next;
    store->valid[idx] = true;
    writer_leave();

    if (had_old) anlz_free(&old);
    return true;
}

const anlz_metadata_t *ui_deck_anlz_store_get(const ui_deck_anlz_store_t *store,
                                               uint8_t deck)
{
    uint8_t idx = 0;
    if (!store || !deck_index(deck, &idx)) return NULL;

    anlz_metadata_t next = {0};
    reader_enter();
    const bool valid = store->valid[idx];
    const esp_err_t clone_rc = valid ? anlz_clone(&store->meta[idx], &next)
                                     : ESP_ERR_NOT_FOUND;
    reader_leave();
    if (clone_rc != ESP_OK) return NULL;

    slots_lock();
    anlz_reader_slot_t *slot = reader_slot_for_owner(current_owner());
    if (!slot) {
        slots_unlock();
        anlz_free(&next);
        return NULL;
    }
    const uint8_t bank = slot->next_bank[idx] % ANLZ_READER_BANKS;
    slot->next_bank[idx] = (uint8_t)((bank + 1u) % ANLZ_READER_BANKS);
    if (slot->valid[idx][bank]) {
        anlz_free(&slot->meta[idx][bank]);
    }
    slot->meta[idx][bank] = next;
    slot->valid[idx][bank] = true;
    const anlz_metadata_t *result = &slot->meta[idx][bank];
    slots_unlock();
    return result;
}
