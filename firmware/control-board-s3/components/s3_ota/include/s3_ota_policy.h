#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define S3_OTA_ESP_IMAGE_MAGIC 0xE9u
#define S3_OTA_ESP32S3_CHIP_ID 0x0009u
#define S3_OTA_IMAGE_HEADER_SIZE 24u
#define S3_OTA_CHIP_ID_OFFSET 12u

bool s3_ota_policy_size_valid(size_t image_size, size_t slot_size);
bool s3_ota_policy_header_valid(const uint8_t *data, size_t size);
