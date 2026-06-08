#include "hot_cue_store.h"

#include <stdio.h>
#include <string.h>

#define HOT_CUE_STORE_VERSION 1u

#if !defined(HOT_CUE_STORE_STANDALONE_TEST)
static esp_err_t make_key(uint32_t track_key, char out[16])
{
    if (track_key == 0 || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    snprintf(out, 16, "hc%08lx", (unsigned long)track_key);
    return ESP_OK;
}
#endif

static void normalize_blob(hot_cue_store_blob_t *blob)
{
    blob->version = HOT_CUE_STORE_VERSION;
    blob->valid_mask &= 0xFFu;
    for (uint32_t i = 0; i < HOT_CUE_STORE_SLOT_COUNT; i++) {
        if ((blob->valid_mask & (1u << i)) == 0) {
            memset(&blob->slots[i], 0, sizeof(blob->slots[i]));
            continue;
        }
        if (blob->slots[i].type != HOT_CUE_STORE_TYPE_LOOP) {
            blob->slots[i].type = HOT_CUE_STORE_TYPE_SINGLE;
            blob->slots[i].end_ms = 0;
        }
    }
}

#if defined(HOT_CUE_STORE_STANDALONE_TEST)

typedef struct {
    uint32_t key;
    hot_cue_store_blob_t blob;
    int valid;
} test_entry_t;

static test_entry_t s_entries[16];

esp_err_t hot_cue_store_load(uint32_t track_key, hot_cue_store_blob_t *out_blob)
{
    if (track_key == 0 || !out_blob) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); i++) {
        if (s_entries[i].valid && s_entries[i].key == track_key) {
            *out_blob = s_entries[i].blob;
            return ESP_OK;
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t hot_cue_store_save(uint32_t track_key, const hot_cue_store_blob_t *blob)
{
    if (track_key == 0 || !blob) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); i++) {
        if (!s_entries[i].valid || s_entries[i].key == track_key) {
            s_entries[i].key = track_key;
            s_entries[i].blob = *blob;
            normalize_blob(&s_entries[i].blob);
            s_entries[i].valid = 1;
            return ESP_OK;
        }
    }
    return ESP_FAIL;
}

esp_err_t hot_cue_store_clear(uint32_t track_key)
{
    if (track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < sizeof(s_entries) / sizeof(s_entries[0]); i++) {
        if (s_entries[i].valid && s_entries[i].key == track_key) {
            memset(&s_entries[i], 0, sizeof(s_entries[i]));
            return ESP_OK;
        }
    }
    return ESP_OK;
}

#else

#include "nvs.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "hot_cue_store";
static const char *NS = "hotcue";

esp_err_t hot_cue_store_load(uint32_t track_key, hot_cue_store_blob_t *out_blob)
{
    if (track_key == 0 || !out_blob) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    ESP_RETURN_ON_ERROR(make_key(track_key, key), TAG, "key");

    nvs_handle_t h;
    esp_err_t rc = nvs_open(NS, NVS_READONLY, &h);
    if (rc != ESP_OK) {
        return rc == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : rc;
    }
    size_t len = sizeof(*out_blob);
    rc = nvs_get_blob(h, key, out_blob, &len);
    nvs_close(h);
    if (rc != ESP_OK) {
        return rc == ESP_ERR_NVS_NOT_FOUND ? ESP_ERR_NOT_FOUND : rc;
    }
    if (len != sizeof(*out_blob) || out_blob->version != HOT_CUE_STORE_VERSION) {
        return ESP_ERR_INVALID_SIZE;
    }
    normalize_blob(out_blob);
    return ESP_OK;
}

esp_err_t hot_cue_store_save(uint32_t track_key, const hot_cue_store_blob_t *blob)
{
    if (track_key == 0 || !blob) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    ESP_RETURN_ON_ERROR(make_key(track_key, key), TAG, "key");

    hot_cue_store_blob_t normalized = *blob;
    normalize_blob(&normalized);

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "nvs_open");
    esp_err_t rc = nvs_set_blob(h, key, &normalized, sizeof(normalized));
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    return rc;
}

esp_err_t hot_cue_store_clear(uint32_t track_key)
{
    if (track_key == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    char key[16];
    ESP_RETURN_ON_ERROR(make_key(track_key, key), TAG, "key");

    nvs_handle_t h;
    ESP_RETURN_ON_ERROR(nvs_open(NS, NVS_READWRITE, &h), TAG, "nvs_open");
    esp_err_t rc = nvs_erase_key(h, key);
    if (rc == ESP_ERR_NVS_NOT_FOUND) {
        rc = ESP_OK;
    }
    if (rc == ESP_OK) {
        rc = nvs_commit(h);
    }
    nvs_close(h);
    return rc;
}

#endif
