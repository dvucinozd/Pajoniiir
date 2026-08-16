#include "s3_ota_upload_guard.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

static void test_one_byte_slow_client_is_bounded(void)
{
    s3_ota_upload_guard_t guard;
    s3_ota_upload_guard_init(&guard, 100u);
    for (uint32_t second = 1u; second < 10u; second++) {
        s3_ota_upload_guard_note_bytes(&guard, 1u);
        assert(s3_ota_upload_guard_check(&guard, 100u + second * 1000u) ==
               S3_OTA_UPLOAD_GUARD_OK);
    }
    s3_ota_upload_guard_note_bytes(&guard, 1u);
    assert(s3_ota_upload_guard_check(&guard, 10100u) ==
           S3_OTA_UPLOAD_GUARD_TOO_SLOW);
}

static void test_progress_windows_and_absolute_deadline(void)
{
    s3_ota_upload_guard_t guard;
    s3_ota_upload_guard_init(&guard, 0u);
    s3_ota_upload_guard_note_bytes(&guard, S3_OTA_UPLOAD_MIN_WINDOW_BYTES);
    assert(s3_ota_upload_guard_check(&guard, 10000u) ==
           S3_OTA_UPLOAD_GUARD_OK);
    s3_ota_upload_guard_note_bytes(&guard, S3_OTA_UPLOAD_MIN_WINDOW_BYTES);
    assert(s3_ota_upload_guard_check(&guard, 20000u) ==
           S3_OTA_UPLOAD_GUARD_OK);
    assert(s3_ota_upload_guard_check(&guard, S3_OTA_UPLOAD_DEADLINE_MS) ==
           S3_OTA_UPLOAD_GUARD_DEADLINE);
}

static void test_tick_wrap_is_safe(void)
{
    s3_ota_upload_guard_t guard;
    uint32_t start = UINT32_MAX - 5000u;
    s3_ota_upload_guard_init(&guard, start);
    s3_ota_upload_guard_note_bytes(&guard, S3_OTA_UPLOAD_MIN_WINDOW_BYTES);
    assert(s3_ota_upload_guard_check(&guard, start + 10000u) ==
           S3_OTA_UPLOAD_GUARD_OK);
}

int main(void)
{
    test_one_byte_slow_client_is_bounded();
    test_progress_windows_and_absolute_deadline();
    test_tick_wrap_is_safe();
    puts("s3_ota_upload_guard tests passed");
    return 0;
}
