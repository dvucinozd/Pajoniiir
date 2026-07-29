#pragma once

/* Counting NVS fake. The backlight tests care about *how many* commits reach the
 * flash, which is the whole point of the debounce, so writes are recorded rather
 * than discarded. Failure can be injected to prove a failed write does not get
 * reflected into the published settings snapshot. */

#include "esp_err.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int nvs_handle_t;

#define NVS_READONLY  0
#define NVS_READWRITE 1

#define ESP_ERR_NVS_NO_FREE_PAGES      0x1100
#define ESP_ERR_NVS_NEW_VERSION_FOUND  0x1101
#define ESP_ERR_NVS_NOT_FOUND          0x1102

typedef struct {
    uint32_t open_calls;
    uint32_t set_u8_calls;
    uint32_t commit_calls;
    char     last_key[32];
    uint8_t  last_value;
    /* Values the fake reports back from nvs_get_u8, keyed by name. */
    uint8_t  stored_backlight;
    int      has_stored_backlight;
    /* Injection: non-zero makes the next set_u8 fail. */
    int      fail_next_set;
} test_nvs_state_t;

extern test_nvs_state_t g_test_nvs;

static inline void test_nvs_reset(void)
{
    memset(&g_test_nvs, 0, sizeof(g_test_nvs));
}

static inline esp_err_t nvs_flash_init(void) { return ESP_OK; }
static inline esp_err_t nvs_flash_erase(void) { return ESP_OK; }

static inline esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *out)
{
    (void)ns;
    (void)mode;
    g_test_nvs.open_calls++;
    if (out) *out = 1;
    return ESP_OK;
}

static inline void nvs_close(nvs_handle_t handle) { (void)handle; }

static inline esp_err_t nvs_set_u8(nvs_handle_t handle, const char *key, uint8_t value)
{
    (void)handle;
    g_test_nvs.set_u8_calls++;
    snprintf(g_test_nvs.last_key, sizeof(g_test_nvs.last_key), "%s", key ? key : "");
    g_test_nvs.last_value = value;
    if (g_test_nvs.fail_next_set) {
        g_test_nvs.fail_next_set = 0;
        return ESP_FAIL;
    }
    if (key && strcmp(key, "backlight") == 0) {
        g_test_nvs.stored_backlight = value;
        g_test_nvs.has_stored_backlight = 1;
    }
    return ESP_OK;
}

static inline esp_err_t nvs_get_u8(nvs_handle_t handle, const char *key, uint8_t *out)
{
    (void)handle;
    if (key && out && strcmp(key, "backlight") == 0 && g_test_nvs.has_stored_backlight) {
        *out = g_test_nvs.stored_backlight;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t nvs_get_str(nvs_handle_t handle, const char *key,
                                    char *out, size_t *len)
{
    (void)handle;
    (void)key;
    (void)out;
    (void)len;
    return ESP_ERR_NVS_NOT_FOUND;
}

static inline esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value)
{
    (void)handle;
    (void)key;
    (void)value;
    return ESP_OK;
}

static inline esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    g_test_nvs.commit_calls++;
    return ESP_OK;
}

static inline esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    (void)key;
    return ESP_OK;
}
