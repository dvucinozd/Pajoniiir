#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "s3_ota.h"
#include "s3_ota_policy.h"
#include "firmware_health.h"
#include "psa/crypto.h"

static esp_partition_t s_target = {.label = "ota_1", .size = S3_OTA_MAX_IMAGE_SIZE};
static const esp_partition_t *s_next_target;
static const esp_partition_t *s_boot_partition;
static esp_err_t s_begin_result;
static esp_err_t s_write_result;
static esp_err_t s_end_result;
static esp_err_t s_desc_result;
static esp_err_t s_set_boot_result;
static psa_status_t s_hash_setup_result;
static psa_status_t s_hash_update_result;
static psa_status_t s_hash_finish_result;
static uint8_t s_digest[DDJ_OTA_SHA256_SIZE];
static esp_app_desc_t s_desc;
static unsigned s_begin_calls;
static unsigned s_write_calls;
static unsigned s_abort_calls;
static unsigned s_end_calls;
static unsigned s_set_boot_calls;
static unsigned s_hash_abort_calls;

static void fake_reset(void)
{
    s_next_target = &s_target;
    s_boot_partition = NULL;
    s_begin_result = ESP_OK;
    s_write_result = ESP_OK;
    s_end_result = ESP_OK;
    s_desc_result = ESP_OK;
    s_set_boot_result = ESP_OK;
    s_hash_setup_result = PSA_SUCCESS;
    s_hash_update_result = PSA_SUCCESS;
    s_hash_finish_result = PSA_SUCCESS;
    memset(s_digest, 0x5a, sizeof(s_digest));
    memset(&s_desc, 0, sizeof(s_desc));
    strcpy(s_desc.project_name, "control-board-s3");
    strcpy(s_desc.version, "RC-test");
    s_begin_calls = s_write_calls = s_abort_calls = s_end_calls = 0u;
    s_set_boot_calls = s_hash_abort_calls = 0u;
    assert(s3_ota_init() == ESP_OK);
}

static ddj_ota_manifest_t manifest_for(size_t size)
{
    ddj_ota_manifest_t manifest = {0};
    manifest.target = DDJ_OTA_TARGET_S3;
    manifest.image_size = (uint32_t)size;
    memcpy(manifest.image_sha256, s_digest, sizeof(s_digest));
    strcpy(manifest.version, "RC-test");
    return manifest;
}

static void expect_failed_without_boot(void)
{
    s3_ota_status_t status;
    s3_ota_get_status(&status);
    assert(status.state == S3_OTA_FAILED);
    assert(s_boot_partition == NULL);
}

const esp_partition_t *esp_ota_get_next_update_partition(const void *start_from)
{
    (void)start_from;
    return s_next_target;
}

esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle)
{
    (void)partition;
    (void)image_size;
    s_begin_calls++;
    if (s_begin_result == ESP_OK) *out_handle = 77u;
    return s_begin_result;
}

esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size)
{
    (void)handle;
    (void)data;
    (void)size;
    s_write_calls++;
    return s_write_result;
}

esp_err_t esp_ota_abort(esp_ota_handle_t handle)
{
    assert(handle == 77u);
    s_abort_calls++;
    return ESP_OK;
}

esp_err_t esp_ota_end(esp_ota_handle_t handle)
{
    assert(handle == 77u);
    s_end_calls++;
    return s_end_result;
}

esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *out_desc)
{
    assert(partition == &s_target);
    if (s_desc_result == ESP_OK) *out_desc = s_desc;
    return s_desc_result;
}

esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition)
{
    s_set_boot_calls++;
    if (s_set_boot_result == ESP_OK) s_boot_partition = partition;
    return s_set_boot_result;
}

psa_status_t psa_hash_setup(psa_hash_operation_t *operation,
                            psa_algorithm_t algorithm)
{
    assert(algorithm == PSA_ALG_SHA_256);
    if (s_hash_setup_result == PSA_SUCCESS) operation->active = 1u;
    return s_hash_setup_result;
}

psa_status_t psa_hash_update(psa_hash_operation_t *operation,
                             const uint8_t *input, size_t input_length)
{
    assert(operation->active == 1u);
    assert(input != NULL && input_length > 0u);
    return s_hash_update_result;
}

psa_status_t psa_hash_finish(psa_hash_operation_t *operation,
                             uint8_t *hash, size_t hash_size,
                             size_t *hash_length)
{
    assert(operation->active == 1u);
    if (s_hash_finish_result != PSA_SUCCESS) return s_hash_finish_result;
    assert(hash_size >= sizeof(s_digest));
    memcpy(hash, s_digest, sizeof(s_digest));
    *hash_length = sizeof(s_digest);
    operation->active = 0u;
    return PSA_SUCCESS;
}

psa_status_t psa_hash_abort(psa_hash_operation_t *operation)
{
    s_hash_abort_calls++;
    operation->active = 0u;
    return PSA_SUCCESS;
}

esp_err_t firmware_health_get_info(firmware_health_info_t *out_info)
{
    static const firmware_health_info_t info = {
        .version = "running",
        .partition_label = "ota_0",
    };
    *out_info = info;
    return ESP_OK;
}

static void test_begin_failures_never_select_boot(void)
{
    fake_reset();
    ddj_ota_manifest_t manifest = manifest_for(S3_OTA_IMAGE_HEADER_SIZE);
    s_next_target = NULL;
    assert(s3_ota_begin(&manifest) == ESP_ERR_NOT_FOUND);
    assert(s_begin_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(S3_OTA_IMAGE_HEADER_SIZE - 1u);
    assert(s3_ota_begin(&manifest) == ESP_ERR_INVALID_SIZE);
    assert(s_begin_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(S3_OTA_IMAGE_HEADER_SIZE);
    s_begin_result = ESP_FAIL;
    assert(s3_ota_begin(&manifest) == ESP_FAIL);
    assert(s_abort_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(S3_OTA_IMAGE_HEADER_SIZE);
    s_hash_setup_result = PSA_ERROR_GENERIC_ERROR;
    assert(s3_ota_begin(&manifest) == ESP_FAIL);
    assert(s_abort_calls == 1u);
    expect_failed_without_boot();
}

static void test_write_failures_close_the_transaction_immediately(void)
{
    uint8_t image[S3_OTA_IMAGE_HEADER_SIZE] = {0};
    fake_reset();
    ddj_ota_manifest_t manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    s_write_result = ESP_FAIL;
    assert(s3_ota_write(image, sizeof(image)) == ESP_FAIL);
    assert(s_abort_calls == 1u);
    assert(s_hash_abort_calls == 1u);
    expect_failed_without_boot();

    /* A new begin must not overwrite a stale flash handle after the failure. */
    s_write_result = ESP_OK;
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s_begin_calls == 2u);
    s3_ota_abort("test cleanup");
    assert(s_abort_calls == 2u);

    fake_reset();
    manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    s_hash_update_result = PSA_ERROR_GENERIC_ERROR;
    assert(s3_ota_write(image, sizeof(image)) == ESP_FAIL);
    assert(s_abort_calls == 1u);
    assert(s_hash_abort_calls == 1u);
    expect_failed_without_boot();
}

static void test_finish_negative_paths_never_select_boot(void)
{
    uint8_t image[S3_OTA_IMAGE_HEADER_SIZE] = {0};
    ddj_ota_manifest_t manifest;

    fake_reset();
    manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image) - 1u) == ESP_OK);
    assert(s3_ota_finish() == ESP_ERR_INVALID_SIZE);
    assert(s_abort_calls == 1u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    s_hash_finish_result = PSA_ERROR_GENERIC_ERROR;
    assert(s3_ota_finish() == ESP_FAIL);
    assert(s_abort_calls == 1u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    manifest.image_sha256[0] ^= 1u;
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    assert(s3_ota_finish() == ESP_ERR_INVALID_CRC);
    assert(s_abort_calls == 1u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    s_end_result = ESP_FAIL;
    assert(s3_ota_finish() == ESP_FAIL);
    assert(s_set_boot_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    strcpy(s_desc.project_name, "wrong-project");
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    assert(s3_ota_finish() == ESP_ERR_INVALID_RESPONSE);
    assert(s_set_boot_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    strcpy(s_desc.version, "wrong-version");
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    assert(s3_ota_finish() == ESP_ERR_INVALID_RESPONSE);
    assert(s_set_boot_calls == 0u);
    expect_failed_without_boot();

    fake_reset();
    manifest = manifest_for(sizeof(image));
    s_set_boot_result = ESP_FAIL;
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    assert(s3_ota_finish() == ESP_FAIL);
    assert(s_set_boot_calls == 1u);
    expect_failed_without_boot();
}

static void test_success_and_reinit_cleanup(void)
{
    uint8_t image[S3_OTA_IMAGE_HEADER_SIZE] = {0};
    fake_reset();
    ddj_ota_manifest_t manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    assert(s3_ota_write(image, sizeof(image)) == ESP_OK);
    assert(s3_ota_finish() == ESP_OK);
    assert(s_end_calls == 1u);
    assert(s_boot_partition == &s_target);
    s3_ota_status_t status;
    s3_ota_get_status(&status);
    assert(status.state == S3_OTA_READY_TO_REBOOT);

    fake_reset();
    manifest = manifest_for(sizeof(image));
    assert(s3_ota_begin(&manifest) == ESP_OK);
    unsigned aborts_before = s_abort_calls;
    assert(s3_ota_init() == ESP_OK);
    assert(s_abort_calls == aborts_before + 1u);
    assert(s_hash_abort_calls == 1u);
}

int main(void)
{
    test_begin_failures_never_select_boot();
    test_write_failures_close_the_transaction_immediately();
    test_finish_negative_paths_never_select_boot();
    test_success_and_reinit_cleanup();
    puts("s3_ota production runtime tests passed");
    return 0;
}
