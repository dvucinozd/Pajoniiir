#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define S3_OTA_ESP_IMAGE_MAGIC 0xE9u
#define S3_OTA_ESP32S3_CHIP_ID 0x0009u
#define S3_OTA_IMAGE_HEADER_SIZE 24u
#define S3_OTA_CHIP_ID_OFFSET 12u
#define S3_OTA_MAX_IMAGE_SIZE 0x1e0000u

typedef enum {
    S3_OTA_FINISH_INVALID_STATE = 0,
    S3_OTA_FINISH_INCOMPLETE,
    S3_OTA_FINISH_VERIFY,
} s3_ota_finish_policy_t;

bool s3_ota_policy_size_valid(size_t image_size, size_t slot_size);
bool s3_ota_policy_header_valid(const uint8_t *data, size_t size);
s3_ota_finish_policy_t s3_ota_policy_finish(bool receiving,
                                             bool handle_open,
                                             size_t received_size,
                                             size_t expected_size);
