#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DDJ_OTA_BUNDLE_MAGIC "DDJOTA1\0"
#define DDJ_OTA_BUNDLE_MAGIC_SIZE 8u
#define DDJ_OTA_SCHEMA_VERSION 1u
#define DDJ_OTA_HEADER_SIZE 188u
#define DDJ_OTA_SIGNED_SIZE 124u
#define DDJ_OTA_SIGNATURE_SIZE 64u
#define DDJ_OTA_PROJECT_SIZE 32u
#define DDJ_OTA_VERSION_SIZE 32u
#define DDJ_OTA_SHA256_SIZE 32u
#define DDJ_OTA_KEY_ID_SIZE 8u
#define DDJ_OTA_RELEASE_KEY_ID "rel-001"

#define DDJ_OTA_OFFSET_SCHEMA 8u
#define DDJ_OTA_OFFSET_HEADER_SIZE 10u
#define DDJ_OTA_OFFSET_TARGET 12u
#define DDJ_OTA_OFFSET_FLAGS 13u
#define DDJ_OTA_OFFSET_CHIP_ID 14u
#define DDJ_OTA_OFFSET_IMAGE_SIZE 16u
#define DDJ_OTA_OFFSET_PROJECT 20u
#define DDJ_OTA_OFFSET_VERSION 52u
#define DDJ_OTA_OFFSET_SHA256 84u
#define DDJ_OTA_OFFSET_KEY_ID 116u
#define DDJ_OTA_OFFSET_SIGNATURE 124u

typedef enum {
    DDJ_OTA_TARGET_P4 = 1,
    DDJ_OTA_TARGET_RETIRED_2 = 2,
} ddj_ota_target_t;

typedef enum {
    DDJ_OTA_MANIFEST_OK = 0,
    DDJ_OTA_MANIFEST_INVALID_ARGUMENT,
    DDJ_OTA_MANIFEST_BAD_MAGIC,
    DDJ_OTA_MANIFEST_BAD_SCHEMA,
    DDJ_OTA_MANIFEST_BAD_HEADER_SIZE,
    DDJ_OTA_MANIFEST_WRONG_TARGET,
    DDJ_OTA_MANIFEST_WRONG_CHIP,
    DDJ_OTA_MANIFEST_BAD_FLAGS,
    DDJ_OTA_MANIFEST_BAD_IMAGE_SIZE,
    DDJ_OTA_MANIFEST_WRONG_PROJECT,
    DDJ_OTA_MANIFEST_BAD_VERSION,
    DDJ_OTA_MANIFEST_WRONG_KEY,
    DDJ_OTA_MANIFEST_BAD_SIGNATURE,
} ddj_ota_manifest_result_t;

typedef struct {
    ddj_ota_target_t target;
    uint16_t chip_id;
    uint32_t image_size;
    char project[DDJ_OTA_PROJECT_SIZE];
    char version[DDJ_OTA_VERSION_SIZE];
    uint8_t image_sha256[DDJ_OTA_SHA256_SIZE];
    char key_id[DDJ_OTA_KEY_ID_SIZE + 1u];
} ddj_ota_manifest_t;

ddj_ota_manifest_result_t ddj_ota_manifest_parse(
    const uint8_t *header,
    size_t header_size,
    ddj_ota_target_t expected_target,
    uint16_t expected_chip_id,
    const char *expected_project,
    size_t max_image_size,
    ddj_ota_manifest_t *out_manifest);

bool ddj_ota_manifest_verify_signature(const uint8_t *header, size_t header_size);
const char *ddj_ota_manifest_result_name(ddj_ota_manifest_result_t result);

#ifdef __cplusplus
}
#endif
