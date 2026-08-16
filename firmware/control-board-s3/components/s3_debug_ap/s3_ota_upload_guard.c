#include "s3_ota_upload_guard.h"

#include <string.h>

void s3_ota_upload_guard_init(s3_ota_upload_guard_t *guard, uint32_t now_ms)
{
    if (!guard) return;
    memset(guard, 0, sizeof(*guard));
    guard->started_ms = now_ms;
    guard->window_started_ms = now_ms;
}

void s3_ota_upload_guard_note_bytes(s3_ota_upload_guard_t *guard, size_t bytes)
{
    if (guard) guard->total_bytes += bytes;
}

s3_ota_upload_guard_result_t s3_ota_upload_guard_check(
    s3_ota_upload_guard_t *guard, uint32_t now_ms)
{
    if (!guard) return S3_OTA_UPLOAD_GUARD_DEADLINE;

    if ((uint32_t)(now_ms - guard->started_ms) >=
        S3_OTA_UPLOAD_DEADLINE_MS) {
        return S3_OTA_UPLOAD_GUARD_DEADLINE;
    }

    if ((uint32_t)(now_ms - guard->window_started_ms) >=
        S3_OTA_UPLOAD_PROGRESS_WINDOW_MS) {
        size_t progress = guard->total_bytes - guard->window_started_bytes;
        if (progress < S3_OTA_UPLOAD_MIN_WINDOW_BYTES) {
            return S3_OTA_UPLOAD_GUARD_TOO_SLOW;
        }
        guard->window_started_ms = now_ms;
        guard->window_started_bytes = guard->total_bytes;
    }
    return S3_OTA_UPLOAD_GUARD_OK;
}

const char *s3_ota_upload_guard_result_name(s3_ota_upload_guard_result_t result)
{
    switch (result) {
    case S3_OTA_UPLOAD_GUARD_OK: return "ok";
    case S3_OTA_UPLOAD_GUARD_DEADLINE: return "deadline";
    case S3_OTA_UPLOAD_GUARD_TOO_SLOW: return "too-slow";
    default: return "unknown";
    }
}
