#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define P4_OTA_ESP_IMAGE_MAGIC 0xE9u
#define P4_OTA_ESP32P4_CHIP_ID 0x0012u
#define P4_OTA_IMAGE_HEADER_SIZE 24u
#define P4_OTA_CHIP_ID_OFFSET 12u
#define P4_OTA_MAX_IMAGE_SIZE 0x400000u

bool p4_ota_policy_size_valid(size_t image_size, size_t slot_size);
bool p4_ota_policy_header_valid(const uint8_t *data, size_t size);
