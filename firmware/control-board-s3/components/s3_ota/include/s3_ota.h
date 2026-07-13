#pragma once

#include <stddef.h>

#include "esp_err.h"
#include "ota_manifest.h"

typedef enum {
    S3_OTA_IDLE = 0,
    S3_OTA_RECEIVING,
    S3_OTA_READY_TO_REBOOT,
    S3_OTA_FAILED,
} s3_ota_state_t;

typedef struct {
    s3_ota_state_t state;
    size_t expected_size;
    size_t received_size;
    char running_slot[17];
    char running_version[33];
    char target_slot[17];
    char target_version[33];
    char last_error[64];
} s3_ota_status_t;

esp_err_t s3_ota_init(void);
esp_err_t s3_ota_begin(const ddj_ota_manifest_t *manifest);
esp_err_t s3_ota_write(const void *data, size_t size);
esp_err_t s3_ota_finish(void);
void s3_ota_abort(const char *reason);
void s3_ota_get_status(s3_ota_status_t *out_status);
const char *s3_ota_state_name(s3_ota_state_t state);
