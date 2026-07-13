#include "firmware_health.h"

#include <string.h>

#include "sdkconfig.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_system.h"

static const char *TAG = "fw_health";
static bool s_initialized;
static bool s_pending_verify;

static const char *state_name(esp_ota_img_states_t state)
{
    switch (state) {
    case ESP_OTA_IMG_NEW: return "new";
    case ESP_OTA_IMG_PENDING_VERIFY: return "pending_verify";
    case ESP_OTA_IMG_VALID: return "valid";
    case ESP_OTA_IMG_INVALID: return "invalid";
    case ESP_OTA_IMG_ABORTED: return "aborted";
    case ESP_OTA_IMG_UNDEFINED:
    default: return "undefined";
    }
}

esp_err_t firmware_health_get_info(firmware_health_info_t *out_info)
{
    if (!out_info) return ESP_ERR_INVALID_ARG;
    memset(out_info, 0, sizeof(*out_info));

    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_app_desc_t *desc = esp_app_get_description();
    if (!running || !desc) return ESP_ERR_NOT_FOUND;

    esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
    esp_err_t state_rc = esp_ota_get_state_partition(running, &state);
    /* Factory images do not have an OTA state record. ESP-IDF reports that as
     * NOT_SUPPORTED (target/version dependent) or NOT_FOUND; both mean a
     * normal non-pending baseline rather than a boot-health failure. */
    if (state_rc != ESP_OK &&
        state_rc != ESP_ERR_NOT_FOUND &&
        state_rc != ESP_ERR_NOT_SUPPORTED) {
        return state_rc;
    }

    out_info->project_name = desc->project_name;
    out_info->version = desc->version;
    out_info->idf_version = desc->idf_ver;
    out_info->partition_label = running->label;
    out_info->partition_address = running->address;
    out_info->partition_size = running->size;
    out_info->image_state = state;
    out_info->rollback_pending = state == ESP_OTA_IMG_PENDING_VERIFY;
    return ESP_OK;
}

esp_err_t firmware_health_init(void)
{
    firmware_health_info_t info;
    esp_err_t rc = firmware_health_get_info(&info);
    if (rc != ESP_OK) {
        ESP_LOGE(TAG, "cannot inspect running image: %s", esp_err_to_name(rc));
        return rc;
    }
    s_pending_verify = info.rollback_pending;
    s_initialized = true;
    ESP_LOGW(TAG, "running %s %s slot=%s addr=0x%lx size=0x%lx state=%s",
             info.project_name, info.version, info.partition_label,
             (unsigned long)info.partition_address,
             (unsigned long)info.partition_size, state_name(info.image_state));
    return ESP_OK;
}

esp_err_t firmware_health_mark_ready(void)
{
    if (!s_initialized) {
        esp_err_t rc = firmware_health_init();
        if (rc != ESP_OK) return rc;
    }
    if (!s_pending_verify) return ESP_OK;

#if CONFIG_DDJ_OTA_FORCE_ROLLBACK_TEST
    ESP_LOGE(TAG, "forced rollback test: restarting before OTA confirmation");
    esp_restart();
    return ESP_FAIL;
#endif

    esp_err_t rc = esp_ota_mark_app_valid_cancel_rollback();
    if (rc == ESP_OK) {
        s_pending_verify = false;
        ESP_LOGW(TAG, "startup health check passed; OTA image marked valid");
    } else {
        ESP_LOGE(TAG, "failed to mark OTA image valid: %s", esp_err_to_name(rc));
    }
    return rc;
}
