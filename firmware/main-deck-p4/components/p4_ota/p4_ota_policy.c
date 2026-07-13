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

p4_ota_finish_policy_t p4_ota_policy_finish(bool receiving,
                                             bool handle_open,
                                             size_t received_size,
                                             size_t expected_size)
{
    if (!receiving) {
        return P4_OTA_FINISH_INVALID_STATE;
    }
    if (!handle_open || received_size != expected_size) {
        return P4_OTA_FINISH_INCOMPLETE;
    }
    return P4_OTA_FINISH_VERIFY;
}
