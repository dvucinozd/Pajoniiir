#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P4_OTA_ESP_IMAGE_MAGIC 0xE9u
#define P4_OTA_ESP32P4_CHIP_ID 0x0012u
#define P4_OTA_IMAGE_HEADER_SIZE 24u
#define P4_OTA_CHIP_ID_OFFSET 12u
#define P4_OTA_MAX_IMAGE_SIZE 0x400000u

typedef enum {
    P4_OTA_FINISH_INVALID_STATE = 0,
    P4_OTA_FINISH_INCOMPLETE,
    P4_OTA_FINISH_VERIFY,
} p4_ota_finish_policy_t;

bool p4_ota_policy_size_valid(size_t image_size, size_t slot_size);
bool p4_ota_policy_header_valid(const uint8_t *data, size_t size);
p4_ota_finish_policy_t p4_ota_policy_finish(bool receiving,
                                             bool handle_open,
                                             size_t received_size,
                                             size_t expected_size);
