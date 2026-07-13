#include "s3_ota.h"
#include "s3_ota_policy.h"

#include <stdio.h>
#include <string.h>

#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "firmware_health.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#define S3_OTA_PROJECT_NAME "control-board-s3"

static const char *TAG = "s3_ota";
static SemaphoreHandle_t s_lock;
static s3_ota_status_t s_status;
static const esp_partition_t *s_target;
static esp_ota_handle_t s_handle;
static bool s_handle_open;

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

const char *s3_ota_state_name(s3_ota_state_t state)
{
    switch (state) {
    case S3_OTA_RECEIVING: return "receiving";
    case S3_OTA_READY_TO_REBOOT: return "ready_to_reboot";
    case S3_OTA_FAILED: return "failed";
    case S3_OTA_IDLE:
    default: return "idle";
    }
}

esp_err_t s3_ota_init(void)
{
    if (!s_lock) s_lock = xSemaphoreCreateMutex();
    if (!s_lock) return ESP_ERR_NO_MEM;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    memset(&s_status, 0, sizeof(s_status));
    s_status.state = S3_OTA_IDLE;
    s_target = NULL;
    s_handle_open = false;
    refresh_running_locked();
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t s3_ota_begin(size_t image_size)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_status.state == S3_OTA_RECEIVING) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }

    s_target = esp_ota_get_next_update_partition(NULL);
    if (!s_target || !s3_ota_policy_size_valid(image_size, s_target->size)) {
        copy_text(s_status.last_error, sizeof(s_status.last_error),
                  !s_target ? "no OTA target slot" : "invalid image size");
        s_status.state = S3_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return !s_target ? ESP_ERR_NOT_FOUND : ESP_ERR_INVALID_SIZE;
    }

    esp_err_t rc = esp_ota_begin(s_target, image_size, &s_handle);
    if (rc != ESP_OK) {
        copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        s_status.state = S3_OTA_FAILED;
        xSemaphoreGive(s_lock);
        return rc;
    }
    s_handle_open = true;
    s_status.state = S3_OTA_RECEIVING;
    s_status.expected_size = image_size;
    s_status.received_size = 0;
    s_status.target_version[0] = '\0';
    s_status.last_error[0] = '\0';
    copy_text(s_status.target_slot, sizeof(s_status.target_slot), s_target->label);
    ESP_LOGW(TAG, "receiving %u bytes into %s", (unsigned)image_size, s_target->label);
    xSemaphoreGive(s_lock);
    return ESP_OK;
}

esp_err_t s3_ota_write(const void *data, size_t size)
{
    if (!data || size == 0 || !s_lock) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_handle_open || s_status.state != S3_OTA_RECEIVING ||
        s_status.received_size > s_status.expected_size ||
        size > s_status.expected_size - s_status.received_size) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t rc = esp_ota_write(s_handle, data, size);
    if (rc == ESP_OK) {
        s_status.received_size += size;
    } else {
        copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        s_status.state = S3_OTA_FAILED;
    }
    xSemaphoreGive(s_lock);
    return rc;
}

esp_err_t s3_ota_finish(void)
{
    if (!s_lock) return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (!s_handle_open || s_status.state != S3_OTA_RECEIVING ||
        s_status.received_size != s_status.expected_size) {
        xSemaphoreGive(s_lock);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t rc = esp_ota_end(s_handle);
    s_handle_open = false;
    esp_app_desc_t desc = {0};
    if (rc == ESP_OK) rc = esp_ota_get_partition_description(s_target, &desc);
    if (rc == ESP_OK && strcmp(desc.project_name, S3_OTA_PROJECT_NAME) != 0) {
        rc = ESP_ERR_INVALID_RESPONSE;
        copy_text(s_status.last_error, sizeof(s_status.last_error), "wrong firmware project");
    }
    if (rc == ESP_OK) rc = esp_ota_set_boot_partition(s_target);
    if (rc == ESP_OK) {
        copy_text(s_status.target_version, sizeof(s_status.target_version), desc.version);
        s_status.state = S3_OTA_READY_TO_REBOOT;
        ESP_LOGW(TAG, "image verified; next boot slot=%s version=%s",
                 s_status.target_slot, s_status.target_version);
    } else {
        if (s_status.last_error[0] == '\0') {
            copy_text(s_status.last_error, sizeof(s_status.last_error), esp_err_to_name(rc));
        }
        s_status.state = S3_OTA_FAILED;
    }
    xSemaphoreGive(s_lock);
    return rc;
}

void s3_ota_abort(const char *reason)
{
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_handle_open) {
        (void)esp_ota_abort(s_handle);
        s_handle_open = false;
    }
    copy_text(s_status.last_error, sizeof(s_status.last_error), reason ? reason : "aborted");
    s_status.state = S3_OTA_FAILED;
    xSemaphoreGive(s_lock);
}

void s3_ota_get_status(s3_ota_status_t *out_status)
{
    if (!out_status) return;
    memset(out_status, 0, sizeof(*out_status));
    if (!s_lock) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    refresh_running_locked();
    *out_status = s_status;
    xSemaphoreGive(s_lock);
}
