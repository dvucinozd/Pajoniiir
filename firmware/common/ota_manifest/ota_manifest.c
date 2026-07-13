#include "ota_manifest.h"

#include <string.h>

static uint16_t read_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8u);
}

static uint32_t read_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static bool copy_fixed_text(char *dst, size_t dst_size,
                            const uint8_t *src, size_t src_size)
{
    const uint8_t *nul = memchr(src, 0, src_size);
    if (!nul || nul == src) return false;
    size_t length = (size_t)(nul - src);
    if (length >= dst_size) return false;
    memcpy(dst, src, length);
    dst[length] = '\0';
    for (size_t i = length + 1u; i < src_size; ++i) {
        if (src[i] != 0) return false;
    }
    return true;
}

ddj_ota_manifest_result_t ddj_ota_manifest_parse(
    const uint8_t *header,
    size_t header_size,
    ddj_ota_target_t expected_target,
    uint16_t expected_chip_id,
    const char *expected_project,
    size_t max_image_size,
    ddj_ota_manifest_t *out_manifest)
{
    if (!header || !expected_project || !out_manifest) {
        return DDJ_OTA_MANIFEST_INVALID_ARGUMENT;
    }
    if (header_size < DDJ_OTA_HEADER_SIZE) return DDJ_OTA_MANIFEST_BAD_HEADER_SIZE;
    if (memcmp(header, DDJ_OTA_BUNDLE_MAGIC, DDJ_OTA_BUNDLE_MAGIC_SIZE) != 0) {
        return DDJ_OTA_MANIFEST_BAD_MAGIC;
    }
    if (read_le16(header + DDJ_OTA_OFFSET_SCHEMA) != DDJ_OTA_SCHEMA_VERSION) {
        return DDJ_OTA_MANIFEST_BAD_SCHEMA;
    }
    if (read_le16(header + DDJ_OTA_OFFSET_HEADER_SIZE) != DDJ_OTA_HEADER_SIZE) {
        return DDJ_OTA_MANIFEST_BAD_HEADER_SIZE;
    }
    if (header[DDJ_OTA_OFFSET_TARGET] != (uint8_t)expected_target) {
        return DDJ_OTA_MANIFEST_WRONG_TARGET;
    }
    if (header[DDJ_OTA_OFFSET_FLAGS] != 0) return DDJ_OTA_MANIFEST_BAD_FLAGS;
    uint16_t chip_id = read_le16(header + DDJ_OTA_OFFSET_CHIP_ID);
    if (chip_id != expected_chip_id) return DDJ_OTA_MANIFEST_WRONG_CHIP;
    uint32_t image_size = read_le32(header + DDJ_OTA_OFFSET_IMAGE_SIZE);
    if (image_size < 24u || image_size > max_image_size) {
        return DDJ_OTA_MANIFEST_BAD_IMAGE_SIZE;
    }

    ddj_ota_manifest_t parsed = {
        .target = expected_target,
        .chip_id = chip_id,
        .image_size = image_size,
    };
    if (!copy_fixed_text(parsed.project, sizeof(parsed.project),
                         header + DDJ_OTA_OFFSET_PROJECT, DDJ_OTA_PROJECT_SIZE) ||
        strcmp(parsed.project, expected_project) != 0) {
        return DDJ_OTA_MANIFEST_WRONG_PROJECT;
    }
    if (!copy_fixed_text(parsed.version, sizeof(parsed.version),
                         header + DDJ_OTA_OFFSET_VERSION, DDJ_OTA_VERSION_SIZE)) {
        return DDJ_OTA_MANIFEST_BAD_VERSION;
    }
    if (!copy_fixed_text(parsed.key_id, sizeof(parsed.key_id),
                         header + DDJ_OTA_OFFSET_KEY_ID, DDJ_OTA_KEY_ID_SIZE) ||
        strcmp(parsed.key_id, DDJ_OTA_RELEASE_KEY_ID) != 0) {
        return DDJ_OTA_MANIFEST_WRONG_KEY;
    }
    memcpy(parsed.image_sha256, header + DDJ_OTA_OFFSET_SHA256,
           sizeof(parsed.image_sha256));
    *out_manifest = parsed;
    return DDJ_OTA_MANIFEST_OK;
}

const char *ddj_ota_manifest_result_name(ddj_ota_manifest_result_t result)
{
    switch (result) {
    case DDJ_OTA_MANIFEST_OK: return "ok";
    case DDJ_OTA_MANIFEST_INVALID_ARGUMENT: return "invalid argument";
    case DDJ_OTA_MANIFEST_BAD_MAGIC: return "bad bundle magic";
    case DDJ_OTA_MANIFEST_BAD_SCHEMA: return "unsupported manifest schema";
    case DDJ_OTA_MANIFEST_BAD_HEADER_SIZE: return "bad manifest header size";
    case DDJ_OTA_MANIFEST_WRONG_TARGET: return "wrong manifest target";
    case DDJ_OTA_MANIFEST_WRONG_CHIP: return "wrong manifest chip";
    case DDJ_OTA_MANIFEST_BAD_FLAGS: return "unsupported manifest flags";
    case DDJ_OTA_MANIFEST_BAD_IMAGE_SIZE: return "bad manifest image size";
    case DDJ_OTA_MANIFEST_WRONG_PROJECT: return "wrong manifest project";
    case DDJ_OTA_MANIFEST_BAD_VERSION: return "bad manifest version";
    case DDJ_OTA_MANIFEST_WRONG_KEY: return "unknown manifest signing key";
    case DDJ_OTA_MANIFEST_BAD_SIGNATURE: return "invalid manifest signature";
    default: return "unknown manifest error";
    }
}
