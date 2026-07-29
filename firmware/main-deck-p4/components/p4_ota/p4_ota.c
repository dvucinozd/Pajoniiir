#include "p4_ota.h"
#include "p4_ota_policy.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "firmware_health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "psa/crypto.h"

static const char *TAG = "p4_ota";
static SemaphoreHandle_t s_lock;
static p4_ota_status_t s_status;
static const esp_partition_t *s_target;
static esp_ota_handle_t s_handle;
static bool s_handle_open;
static psa_hash_operation_t s_image_sha = PSA_HASH_OPERATION_INIT;
static bool s_image_sha_active;
static uint8_t s_expected_sha256[DDJ_OTA_SHA256_SIZE];
static char s_expected_version[DDJ_OTA_VERSION_SIZE];

#define P4_OTA_PROJECT_NAME "main-deck-p4"

static void copy_text(char *dst, size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0) return;
    snprintf(dst, dst_size, "%s", src ? src : "");
}

static void refresh_running_locked(void)
{
    firmware_health_info_t info;
    if (firmware_health_get_info(&info) == ESP_OK) {
        copy_text(s_status.running_slot, sizeof(s_status.running_slot), info.partition_label);
        copy_text(s_status.running_version, sizeof(s_status.running_version), info.version);
    }
}

static void close_receive_resources_locked(void)
{
    if (s_handle_open) {
        (void)esp_ota_abort(s_handle);
        s_handle_open = false;
    }
    if (s_image_sha_active) {
        (void)psa_hash_abort(&s_image_sha);
        s_image_sha = psa_hash_operation_init();
        s_image_sha_active = false;
    }
}

const char *p4_ota_state_name(p4_ota_state_t state)
{
    switch (state) {
    case P4_OTA_RECEIVING: return "receiving";
    case P4_OTA_READY_TO_REBOOT: return "ready_to_reboot";
    case P4_OTA_FAILED: return "failed";
    case P4_OTA_IDLE:
    default: return "idle";
    }
}

esp_err_t p4_ota_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = P4_OTA_IDLE;
    s_image_sha = psa_hash_operation_init();
    s_image_sha_active = false;
    refresh_running_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t p4_ota_begin(const ddj_ota_manifest_t *manifest)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    if (!manifest || manifest->target != DDJ_OTA_TARGET_P4) return ESP_ERR_INVALID_ARG;
    size_t image_size = manifest->image_size;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_status.state == P4_OTA_RECEIVING) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target || !p4_ota_policy_size_valid(image_size, s_target->size)) {
        copy_text(s_status.last_error, sizeof(s_status.last_error),
                  !s_target ? "no OTA target slot" : "invalid image size");
        s_status.state = P4_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return !s_target ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_SIZE;
    }

    esp_err_t rc = esp_ota_begin(s_target, image_size, &s_handle);
    if (rc != ESP_OK) {
        copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        s_status.state = P4_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return rc;
    }
    s_handle_open = true;
    s_image_sha = psa_hash_operation_init();
    if (psa_hash_setup(&s_image_sha, PSA_ALG_SHA_256) != PSA_SUCCESS) {
        (void)esp_ota_abort(s_handle);
        s_handle_open = false;
        copy_text(s_status.last_error, sizeof(s_status.last_error), "SHA-256 init failed");
        s_status.state = P4_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return ESP_FAIL;
    }
    s_image_sha_active = true;
    memcpy(s_expected_sha256, manifest->image_sha256, sizeof(s_expected_sha256));
    copy_text(s_expected_version, sizeof(s_expected_version), manifest->version);
    s_status.state = P4_OTA_RECEIVING;
    s_status.expected_size = image_size;
    s_status.received_size = 0;
    s_status.target_version[0] = '\0';
    s_status.last_error[0] = '\0';
    copy_text(s_status.target_slot, sizeof(s_status.target_slot), s_target->label);
    ESP_LOGW(TAG, "receiving %u bytes into %s", (unsigned)image_size, s_target->label);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t p4_ota_write(const void *data, size_t size)
{
    if (!data || size == 0 || !s_lock) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_handle_open || s_status.state != P4_OTA_RECEIVING ||
        s_status.received_size > s_status.expected_size ||
        size > s_status.expected_size - s_status.received_size) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = esp_ota_write(s_handle, data, size);
    if (rc == ESP_OK) {
        if (psa_hash_update(&s_image_sha, data, size) == PSA_SUCCESS) {
            s_status.received_size += size;
        } else {
            rc = ESP_FAIL;
            copy_text(s_status.last_error, sizeof(s_status.last_error), "SHA-256 update failed");
            s_status.state = P4_OTA_FAILED;
        }
    } else {
        copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        s_status.state = P4_OTA_FAILED;
    }
    xSemaphoreGive(s_lock);
    return rc;
}

esp_err_t p4_ota_finish(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    p4_ota_finish_policy_t finish_policy = p4_ota_policy_finish(
        s_status.state == P4_OTA_RECEIVING, s_handle_open,
        s_status.received_size, s_status.expected_size);
    if (finish_policy == P4_OTA_FINISH_INVALID_STATE) {
        /* A duplicate/idle finish is a caller error, not a failed transfer.
         * Release any inconsistent leftover resources without overwriting the
         * authoritative status/error from the operation that already ended. */
        close_receive_resources_locked();
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (finish_policy == P4_OTA_FINISH_INCOMPLETE) {
        close_receive_resources_locked();
        copy_text(s_status.last_error, sizeof(s_status.last_error),
                  "incomplete OTA image");
        s_status.state = P4_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t actual_sha256[DDJ_OTA_SHA256_SIZE];
    size_t actual_sha256_size = 0u;
    psa_status_t hash_rc = psa_hash_finish(&s_image_sha,
                                           actual_sha256,
                                           sizeof(actual_sha256),
                                           &actual_sha256_size);
    esp_err_t rc = hash_rc == PSA_SUCCESS &&
                   actual_sha256_size == sizeof(actual_sha256)
                       ? ESP_OK
                       : ESP_FAIL;
    s_image_sha = psa_hash_operation_init();
    s_image_sha_active = false;
    if (rc == ESP_OK && memcmp(actual_sha256, s_expected_sha256,
                               sizeof(actual_sha256)) != 0) {
        rc = ESP_ERR_INVALID_CRC;
        copy_text(s_status.last_error, sizeof(s_status.last_error),
                  "firmware SHA-256 mismatch");
    }
    if (rc != ESP_OK) {
        (void)esp_ota_abort(s_handle);
    } else {
        rc = esp_ota_end(s_handle);
    }
    s_handle_open = false;
    esp_app_desc_t desc = {0};
    if (rc == ESP_OK) rc = esp_ota_get_partition_description(s_target, &desc);
    if (rc == ESP_OK && strcmp(desc.project_name, P4_OTA_PROJECT_NAME) != 0) {
        rc = ESP_ERR_INVALID_RESPONSE;
        copy_text(s_status.last_error, sizeof(s_status.last_error), "wrong firmware project");
    }
    if (rc == ESP_OK && strcmp(desc.version, s_expected_version) != 0) {
        rc = ESP_ERR_INVALID_RESPONSE;
        copy_text(s_status.last_error, sizeof(s_status.last_error),
                  "signed version does not match image");
    }
    if (rc == ESP_OK) rc = esp_ota_set_boot_partition(s_target);
    if (rc == ESP_OK) {
        copy_text(s_status.target_version, sizeof(s_status.target_version), desc.version);
        s_status.state = P4_OTA_READY_TO_REBOOT;
        ESP_LOGW(TAG, "image verified; next boot slot=%s version=%s",
                 s_status.target_slot, s_status.target_version);
    } else {
        if (s_status.last_error[0] == '\0') {
            copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        }
        s_status.state = P4_OTA_FAILED;
    }
    xSemaphoreGive(s_lock);
    return rc;
}

void p4_ota_abort(const char *reason)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    close_receive_resources_locked();
    copy_text(s_status.last_error, sizeof(s_status.last_error), reason ? reason : "aborted");
    s_status.state = P4_OTA_FAILED;
    xSemaphoreGive(s_lock);
}

void p4_ota_get_status(p4_ota_status_t *out_status)
{
    if (!out_status) return;
    memset(out_status, 0, sizeof(*out_status));
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    refresh_running_locked();
    *out_status = s_status;
    xSemaphoreGive(s_lock);
}
