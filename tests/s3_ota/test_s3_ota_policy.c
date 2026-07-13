#include "s3_ota_policy.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void make_header(uint8_t header[S3_OTA_IMAGE_HEADER_SIZE], uint16_t chip_id)
{
    memset(header, 0, S3_OTA_IMAGE_HEADER_SIZE);
    header[0] = S3_OTA_ESP_IMAGE_MAGIC;
    header[S3_OTA_CHIP_ID_OFFSET] = (uint8_t)chip_id;
    header[S3_OTA_CHIP_ID_OFFSET + 1u] = (uint8_t)(chip_id >> 8);
}

int main(void)
{
    assert(!s3_ota_policy_size_valid(0, 0x1e0000));
    assert(!s3_ota_policy_size_valid(S3_OTA_IMAGE_HEADER_SIZE - 1u, 0x1e0000));
    assert(s3_ota_policy_size_valid(S3_OTA_IMAGE_HEADER_SIZE, 0x1e0000));
    assert(s3_ota_policy_size_valid(0x1e0000, 0x1e0000));
    assert(!s3_ota_policy_size_valid(0x1e0001, 0x1e0000));

    uint8_t header[S3_OTA_IMAGE_HEADER_SIZE];
    make_header(header, S3_OTA_ESP32S3_CHIP_ID);
    assert(s3_ota_policy_header_valid(header, sizeof(header)));
    assert(!s3_ota_policy_header_valid(header, sizeof(header) - 1u));
    header[0] = 0x7f;
    assert(!s3_ota_policy_header_valid(header, sizeof(header)));
    make_header(header, 0x0012u); /* ESP32-P4 */
    assert(!s3_ota_policy_header_valid(header, sizeof(header)));
    assert(!s3_ota_policy_header_valid(NULL, 0));

    assert(s3_ota_policy_finish(false, false, 0, 0) ==
           S3_OTA_FINISH_INVALID_STATE);
    assert(s3_ota_policy_finish(false, true, 1024, 1024) ==
           S3_OTA_FINISH_INVALID_STATE);
    assert(s3_ota_policy_finish(true, false, 1024, 1024) ==
           S3_OTA_FINISH_INCOMPLETE);
    assert(s3_ota_policy_finish(true, true, 1023, 1024) ==
           S3_OTA_FINISH_INCOMPLETE);
    assert(s3_ota_policy_finish(true, true, 1024, 1024) ==
           S3_OTA_FINISH_VERIFY);

    puts("s3_ota_policy tests passed");
    return 0;
}
