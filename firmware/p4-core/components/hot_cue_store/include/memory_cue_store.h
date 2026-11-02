#pragma once
#include "hot_cue_store.h"

#define MEMORY_CUE_STORE_CAPACITY 16u

/* A local record replaces the imported bank, including an intentionally empty
 * bank. Restore removes that override. Rekordbox files are never modified. */
typedef struct memory_cue_bank {
    uint8_t count;
    hot_cue_store_slot_t cues[MEMORY_CUE_STORE_CAPACITY];
} memory_cue_bank_t;

esp_err_t memory_cue_bank_put(memory_cue_bank_t *bank, hot_cue_store_slot_t cue);
esp_err_t memory_cue_bank_delete(memory_cue_bank_t *bank, uint32_t pos_ms);
esp_err_t memory_cue_store_load(const media_persistent_id_t *id, memory_cue_bank_t *out);
esp_err_t memory_cue_store_save(const media_persistent_id_t *id, const memory_cue_bank_t *bank);
esp_err_t memory_cue_store_restore(const media_persistent_id_t *id);
