#include "p4_ota_policy.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void make_header(uint8_t header[P4_OTA_IMAGE_HEADER_SIZE], uint16_t chip_id)
{
    memset(header, 0, P4_OTA_IMAGE_HEADER_SIZE);
    header[0] = P4_OTA_ESP_IMAGE_MAGIC;
    header[P4_OTA_CHIP_ID_OFFSET] = (uint8_t)chip_id;
    header[P4_OTA_CHIP_ID_OFFSET + 1u] = (uint8_t)(chip_id >> 8);
}

int main(void)
{
    assert(!p4_ota_policy_size_valid(0, 0x400000));
    assert(!p4_ota_policy_size_valid(P4_OTA_IMAGE_HEADER_SIZE - 1u, 0x400000));
    assert(!p4_ota_policy_size_valid(1, 0));
    assert(p4_ota_policy_size_valid(0x1f41c0, 0x400000));
    assert(p4_ota_policy_size_valid(0x400000, 0x400000));
    assert(!p4_ota_policy_size_valid(0x400001, 0x400000));

    uint8_t header[P4_OTA_IMAGE_HEADER_SIZE];
    make_header(header, P4_OTA_ESP32P4_CHIP_ID);
    assert(p4_ota_policy_header_valid(header, sizeof(header)));
    assert(!p4_ota_policy_header_valid(header, sizeof(header) - 1u));
    header[0] = 0x7f;
    assert(!p4_ota_policy_header_valid(header, sizeof(header)));
    make_header(header, 0x0009u); /* ESP32-S3 */
    assert(!p4_ota_policy_header_valid(header, sizeof(header)));
    assert(!p4_ota_policy_header_valid(NULL, 0));

    assert(p4_ota_policy_finish(false, false, 0, 0) ==
           P4_OTA_FINISH_INVALID_STATE);
    assert(p4_ota_policy_finish(false, true, 1024, 1024) ==
           P4_OTA_FINISH_INVALID_STATE);
    assert(p4_ota_policy_finish(true, false, 1024, 1024) ==
           P4_OTA_FINISH_INCOMPLETE);
    assert(p4_ota_policy_finish(true, true, 1023, 1024) ==
           P4_OTA_FINISH_INCOMPLETE);
    assert(p4_ota_policy_finish(true, true, 1024, 1024) ==
           P4_OTA_FINISH_VERIFY);

    puts("p4_ota_policy tests passed");
    return 0;
}
