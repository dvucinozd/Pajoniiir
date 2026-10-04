#include <assert.h>
#include <stdio.h>
#include "firmware_health.h"
#include "esp_app_desc.h"

static const esp_partition_t partition = {"ota_1", 0x820000, 0x400000};
static const esp_app_desc_t description = {"main-deck-p4", "test", "6.0.2"};
static esp_ota_img_states_t image_state;
static esp_err_t query_result, confirm_result, reject_result;
static unsigned confirmations, rejections;

const esp_partition_t *esp_ota_get_running_partition(void) { return &partition; }
const esp_app_desc_t *esp_app_get_description(void) { return &description; }
esp_err_t esp_ota_get_state_partition(const esp_partition_t *p,
                                     esp_ota_img_states_t *state)
{
    assert(p == &partition);
    if (query_result == ESP_OK) *state = image_state;
    return query_result;
}
esp_err_t esp_ota_mark_app_valid_cancel_rollback(void)
{
    ++confirmations;
    return confirm_result;
}
esp_err_t esp_ota_mark_app_invalid_rollback_and_reboot(void)
{
    ++rejections;
    return reject_result;
}
void esp_restart(void) { assert(!"unexpected forced rollback injection"); }

int main(void)
{
    firmware_health_info_t info;
    assert(firmware_health_reject_pending() == ESP_ERR_INVALID_STATE);
    image_state = ESP_OTA_IMG_VALID;
    assert(firmware_health_init() == ESP_OK);
    assert(firmware_health_reject_pending() == ESP_ERR_INVALID_STATE);
    assert(firmware_health_mark_ready() == ESP_OK);
    assert(confirmations == 0 && rejections == 0);

    /* Factory images have no OTA selection state; they must never be rejected. */
    query_result = ESP_ERR_NOT_SUPPORTED;
    assert(firmware_health_init() == ESP_OK);
    assert(firmware_health_reject_pending() == ESP_ERR_INVALID_STATE);
    assert(firmware_health_get_info(&info) == ESP_OK && !info.rollback_pending);
    query_result = ESP_ERR_NOT_FOUND;
    assert(firmware_health_init() == ESP_OK);
    assert(firmware_health_reject_pending() == ESP_ERR_INVALID_STATE);

    query_result = ESP_OK;
    image_state = ESP_OTA_IMG_PENDING_VERIFY;
    assert(firmware_health_init() == ESP_OK);
    reject_result = ESP_FAIL; /* No rollback target: report failure, not success. */
    assert(firmware_health_reject_pending() == ESP_FAIL && rejections == 1);
    assert(confirmations == 0);
    confirm_result = ESP_FAIL;
    assert(firmware_health_mark_ready() == ESP_FAIL);
    confirm_result = ESP_OK;
    assert(firmware_health_mark_ready() == ESP_OK);
    assert(confirmations == 2);
    assert(firmware_health_reject_pending() == ESP_ERR_INVALID_STATE);
    assert(firmware_health_mark_ready() == ESP_OK && confirmations == 2);
    query_result = ESP_ERR_TIMEOUT;
    assert(firmware_health_get_info(&info) == ESP_ERR_TIMEOUT);
    assert(firmware_health_get_info(NULL) == ESP_ERR_INVALID_ARG);
    puts("firmware_health: PASS");
    return 0;
}
