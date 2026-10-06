#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "anlz_snapshot.h"
#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DECK_ANLZ_STORE_DECK_COUNT 2u

typedef struct {
    anlz_snapshot_t *snapshot[UI_DECK_ANLZ_STORE_DECK_COUNT];
} ui_deck_anlz_store_t;

void ui_deck_anlz_store_init(ui_deck_anlz_store_t *store);
void ui_deck_anlz_store_clear_all(ui_deck_anlz_store_t *store);
void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck);
bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,
                            uint8_t deck,
                            const anlz_metadata_t *meta);
anlz_snapshot_t *ui_deck_anlz_store_acquire(
    const ui_deck_anlz_store_t *store,
    uint8_t deck);

#ifdef __cplusplus
}
#endif
