#pragma once

#include <stddef.h>
#include <stdint.h>

#define S3_OTA_UPLOAD_DEADLINE_MS 180000u
#define S3_OTA_UPLOAD_PROGRESS_WINDOW_MS 10000u
#define S3_OTA_UPLOAD_MIN_WINDOW_BYTES 4096u

typedef enum {
    S3_OTA_UPLOAD_GUARD_OK = 0,
    S3_OTA_UPLOAD_GUARD_DEADLINE,
    S3_OTA_UPLOAD_GUARD_TOO_SLOW,
} s3_ota_upload_guard_result_t;

typedef struct {
    uint32_t started_ms;
    uint32_t window_started_ms;
    size_t total_bytes;
    size_t window_started_bytes;
} s3_ota_upload_guard_t;

void s3_ota_upload_guard_init(s3_ota_upload_guard_t *guard, uint32_t now_ms);
void s3_ota_upload_guard_note_bytes(s3_ota_upload_guard_t *guard, size_t bytes);
s3_ota_upload_guard_result_t s3_ota_upload_guard_check(
    s3_ota_upload_guard_t *guard, uint32_t now_ms);
const char *s3_ota_upload_guard_result_name(s3_ota_upload_guard_result_t result);
