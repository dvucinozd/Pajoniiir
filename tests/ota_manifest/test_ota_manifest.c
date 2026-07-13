#include "ota_manifest.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8u);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)value;
    dst[1] = (uint8_t)(value >> 8u);
    dst[2] = (uint8_t)(value >> 16u);
    dst[3] = (uint8_t)(value >> 24u);
}

static void make_header(uint8_t header[DDJ_OTA_HEADER_SIZE],
                        ddj_ota_target_t target, uint16_t chip_id,
                        const char *project)
{
    memset(header, 0, DDJ_OTA_HEADER_SIZE);
    memcpy(header, DDJ_OTA_BUNDLE_MAGIC, DDJ_OTA_BUNDLE_MAGIC_SIZE);
    put_le16(header + DDJ_OTA_OFFSET_SCHEMA, DDJ_OTA_SCHEMA_VERSION);
    put_le16(header + DDJ_OTA_OFFSET_HEADER_SIZE, DDJ_OTA_HEADER_SIZE);
    header[DDJ_OTA_OFFSET_TARGET] = (uint8_t)target;
    put_le16(header + DDJ_OTA_OFFSET_CHIP_ID, chip_id);
    put_le32(header + DDJ_OTA_OFFSET_IMAGE_SIZE, 0x120000u);
    memcpy(header + DDJ_OTA_OFFSET_PROJECT, project, strlen(project));
    memcpy(header + DDJ_OTA_OFFSET_VERSION, "RC2-test", 8u);
    memset(header + DDJ_OTA_OFFSET_SHA256, 0x5a, DDJ_OTA_SHA256_SIZE);
    memcpy(header + DDJ_OTA_OFFSET_KEY_ID, DDJ_OTA_RELEASE_KEY_ID,
           strlen(DDJ_OTA_RELEASE_KEY_ID));
}

static ddj_ota_manifest_result_t parse_p4(const uint8_t *header, size_t size,
                                          ddj_ota_manifest_t *manifest)
{
    return ddj_ota_manifest_parse(header, size, DDJ_OTA_TARGET_P4, 0x0012u,
                                  "main-deck-p4", 0x400000u, manifest);
}

int main(void)
{
    uint8_t header[DDJ_OTA_HEADER_SIZE];
    ddj_ota_manifest_t manifest;
    make_header(header, DDJ_OTA_TARGET_P4, 0x0012u, "main-deck-p4");
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_OK);
    assert(manifest.target == DDJ_OTA_TARGET_P4);
    assert(manifest.chip_id == 0x0012u);
    assert(manifest.image_size == 0x120000u);
    assert(strcmp(manifest.project, "main-deck-p4") == 0);
    assert(strcmp(manifest.version, "RC2-test") == 0);
    assert(strcmp(manifest.key_id, DDJ_OTA_RELEASE_KEY_ID) == 0);
    assert(manifest.image_sha256[0] == 0x5a);

    assert(parse_p4(NULL, 0, &manifest) == DDJ_OTA_MANIFEST_INVALID_ARGUMENT);
    assert(parse_p4(header, DDJ_OTA_HEADER_SIZE - 1u, &manifest) ==
           DDJ_OTA_MANIFEST_BAD_HEADER_SIZE);

    header[0] ^= 1u;
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_BAD_MAGIC);
    header[0] ^= 1u;
    put_le16(header + DDJ_OTA_OFFSET_SCHEMA, 2u);
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_BAD_SCHEMA);
    put_le16(header + DDJ_OTA_OFFSET_SCHEMA, DDJ_OTA_SCHEMA_VERSION);
    put_le16(header + DDJ_OTA_OFFSET_HEADER_SIZE, DDJ_OTA_HEADER_SIZE - 1u);
    assert(parse_p4(header, sizeof(header), &manifest) ==
           DDJ_OTA_MANIFEST_BAD_HEADER_SIZE);
    put_le16(header + DDJ_OTA_OFFSET_HEADER_SIZE, DDJ_OTA_HEADER_SIZE);

    header[DDJ_OTA_OFFSET_TARGET] = DDJ_OTA_TARGET_S3;
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_WRONG_TARGET);
    header[DDJ_OTA_OFFSET_TARGET] = DDJ_OTA_TARGET_P4;
    header[DDJ_OTA_OFFSET_FLAGS] = 1u;
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_BAD_FLAGS);
    header[DDJ_OTA_OFFSET_FLAGS] = 0u;
    put_le16(header + DDJ_OTA_OFFSET_CHIP_ID, 0x0009u);
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_WRONG_CHIP);
    put_le16(header + DDJ_OTA_OFFSET_CHIP_ID, 0x0012u);

    put_le32(header + DDJ_OTA_OFFSET_IMAGE_SIZE, 23u);
    assert(parse_p4(header, sizeof(header), &manifest) ==
           DDJ_OTA_MANIFEST_BAD_IMAGE_SIZE);
    put_le32(header + DDJ_OTA_OFFSET_IMAGE_SIZE, 0x400001u);
    assert(parse_p4(header, sizeof(header), &manifest) ==
           DDJ_OTA_MANIFEST_BAD_IMAGE_SIZE);
    put_le32(header + DDJ_OTA_OFFSET_IMAGE_SIZE, 0x120000u);

    header[DDJ_OTA_OFFSET_PROJECT] = 'x';
    assert(parse_p4(header, sizeof(header), &manifest) ==
           DDJ_OTA_MANIFEST_WRONG_PROJECT);
    make_header(header, DDJ_OTA_TARGET_P4, 0x0012u, "main-deck-p4");
    memset(header + DDJ_OTA_OFFSET_VERSION, 'x', DDJ_OTA_VERSION_SIZE);
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_BAD_VERSION);
    make_header(header, DDJ_OTA_TARGET_P4, 0x0012u, "main-deck-p4");
    header[DDJ_OTA_OFFSET_KEY_ID] = 'x';
    assert(parse_p4(header, sizeof(header), &manifest) == DDJ_OTA_MANIFEST_WRONG_KEY);

    make_header(header, DDJ_OTA_TARGET_S3, 0x0009u, "control-board-s3");
    assert(ddj_ota_manifest_parse(header, sizeof(header), DDJ_OTA_TARGET_S3,
                                  0x0009u, "control-board-s3", 0x1e0000u,
                                  &manifest) == DDJ_OTA_MANIFEST_OK);
    assert(strcmp(ddj_ota_manifest_result_name(DDJ_OTA_MANIFEST_BAD_SIGNATURE),
                  "invalid manifest signature") == 0);

    puts("ota_manifest tests passed");
    return 0;
}
