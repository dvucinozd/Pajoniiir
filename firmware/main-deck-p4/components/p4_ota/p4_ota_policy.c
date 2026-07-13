#include "p4_ota_policy.h"

bool p4_ota_policy_size_valid(size_t image_size, size_t slot_size)
{
    return image_size >= P4_OTA_IMAGE_HEADER_SIZE &&
           slot_size > 0u && image_size <= slot_size;
}

bool p4_ota_policy_header_valid(const uint8_t *data, size_t size)
{
    if (!data || size < P4_OTA_IMAGE_HEADER_SIZE ||
        data[0] != P4_OTA_ESP_IMAGE_MAGIC) {
        return false;
    }
    uint16_t chip_id = (uint16_t)data[P4_OTA_CHIP_ID_OFFSET] |
                       ((uint16_t)data[P4_OTA_CHIP_ID_OFFSET + 1u] << 8);
    return chip_id == P4_OTA_ESP32P4_CHIP_ID;
}
