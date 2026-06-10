#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_DECK_ANLZ_STORE_DECK_COUNT 2u

typedef struct {
    anlz_metadata_t meta[UI_DECK_ANLZ_STORE_DECK_COUNT];
    bool valid[UI_DECK_ANLZ_STORE_DECK_COUNT];
} ui_deck_anlz_store_t;

void ui_deck_anlz_store_init(ui_deck_anlz_store_t *store);
void ui_deck_anlz_store_clear_all(ui_deck_anlz_store_t *store);
void ui_deck_anlz_store_clear(ui_deck_anlz_store_t *store, uint8_t deck);
bool ui_deck_anlz_store_set(ui_deck_anlz_store_t *store,
                            uint8_t deck,
                            const anlz_metadata_t *meta);
const anlz_metadata_t *ui_deck_anlz_store_get(const ui_deck_anlz_store_t *store,
                                               uint8_t deck);

#ifdef __cplusplus
}
#endif
