#pragma once

#include <stdint.h>
#include "../../media_identity/include/media_identity.h"

#if defined(HOT_CUE_STORE_STANDALONE_TEST)
typedef int esp_err_t;
#define ESP_OK               0
#define ESP_FAIL            -1
#define ESP_ERR_INVALID_ARG  0x102
#define ESP_ERR_NOT_FOUND    0x105
#define ESP_ERR_INVALID_SIZE 0x104
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_INVALID_CRC  0x10A
#else
#include "esp_err.h"
#endif

#define HOT_CUE_STORE_SLOT_COUNT 8u
#define HOT_CUE_STORE_TYPE_SINGLE 1u
#define HOT_CUE_STORE_TYPE_LOOP   2u

typedef struct {
    uint32_t pos_ms;
    uint32_t end_ms;
    uint8_t type;
    uint8_t reserved[3];
} hot_cue_store_slot_t;

typedef struct {
    uint32_t version;
    uint32_t valid_mask;
    hot_cue_store_slot_t slots[HOT_CUE_STORE_SLOT_COUNT];
} hot_cue_store_blob_t;

esp_err_t hot_cue_store_load(const media_persistent_id_t *id, hot_cue_store_blob_t *out_blob);
esp_err_t hot_cue_store_save(const media_persistent_id_t *id, const hot_cue_store_blob_t *blob);
esp_err_t hot_cue_store_clear(const media_persistent_id_t *id);
