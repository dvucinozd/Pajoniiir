#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "ota_manifest.h"

typedef enum {
    P4_OTA_IDLE = 0,
    P4_OTA_RECEIVING,
    P4_OTA_READY_TO_REBOOT,
    P4_OTA_FAILED,
} p4_ota_state_t;

typedef struct {
    p4_ota_state_t state;
    size_t expected_size;
    size_t received_size;
    char running_slot[17];
    char running_version[33];
    char target_slot[17];
    char target_version[33];
    char last_error[64];
} p4_ota_status_t;

esp_err_t p4_ota_init(void);
esp_err_t p4_ota_begin(const ddj_ota_manifest_t *manifest);
esp_err_t p4_ota_write(const void *data, size_t size);
esp_err_t p4_ota_finish(void);
void p4_ota_abort(const char *reason);
void p4_ota_get_status(p4_ota_status_t *out_status);
const char *p4_ota_state_name(p4_ota_state_t state);
