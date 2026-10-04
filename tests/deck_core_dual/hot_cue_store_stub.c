#include "hot_cue_store.h"
#include <stdbool.h>
#include <string.h>

typedef struct {
    media_persistent_id_t id;
    hot_cue_store_blob_t blob;
    bool valid;
} test_hot_cue_entry_t;

static test_hot_cue_entry_t s_entries[8];

static void normalize_blob(hot_cue_store_blob_t *blob)
{
    blob->version = 3;
    blob->valid_mask &= 0xFFu;
    blob->override_mask &= 0xFFu;
    blob->valid_mask &= blob->override_mask;
    for (uint8_t i = 0; i < HOT_CUE_STORE_SLOT_COUNT; i++) {
        if ((blob->valid_mask & (1u << i)) == 0) {
            memset(&blob->slots[i], 0, sizeof(blob->slots[i]));
        } else if (blob->slots[i].type != HOT_CUE_STORE_TYPE_LOOP) {
            blob->slots[i].type = HOT_CUE_STORE_TYPE_SINGLE;
            blob->slots[i].end_ms = 0;
        }
    }
}

void hot_cue_store_merge(const hot_cue_store_blob_t *source,
                         const hot_cue_store_blob_t *local,
                         hot_cue_store_blob_t *out)
{
    if (!out) return;
    hot_cue_store_blob_t merged = {0};
    for (uint8_t i = 0; i < HOT_CUE_STORE_SLOT_COUNT; ++i) {
        const uint32_t bit = 1u << i;
        const hot_cue_store_blob_t *selected =
            local && (local->override_mask & bit) ? local : source;
        if (selected && (selected->valid_mask & bit)) {
            merged.valid_mask |= bit;
            merged.slots[i] = selected->slots[i];
        }
    }
    *out = merged;
}

esp_err_t hot_cue_store_load(const media_persistent_id_t *id, hot_cue_store_blob_t *out_blob)
{
    if (!id || !id->valid || !out_blob) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (s_entries[i].valid && media_persistent_id_equal(&s_entries[i].id, id)) {
            *out_blob = s_entries[i].blob;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t hot_cue_store_save(const media_persistent_id_t *id, const hot_cue_store_blob_t *blob)
{
    if (!id || !id->valid || !blob) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (!s_entries[i].valid || media_persistent_id_equal(&s_entries[i].id, id)) {
            s_entries[i].id = *id;
            s_entries[i].blob = *blob;
            normalize_blob(&s_entries[i].blob);
            s_entries[i].valid = true;
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t hot_cue_store_clear(const media_persistent_id_t *id)
{
    if (!id || !id->valid) {
        return ESP_ERR_INVALID_ARG;
    }
    for (uint8_t i = 0; i < 8; i++) {
        if (s_entries[i].valid && media_persistent_id_equal(&s_entries[i].id, id)) {
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t hot_cue_store_reset_to_source(const media_persistent_id_t *id)
{
    hot_cue_store_blob_t empty = {0};
    return hot_cue_store_save(id, &empty);
}
