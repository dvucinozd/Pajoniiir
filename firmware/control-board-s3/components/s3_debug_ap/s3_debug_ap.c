#include "s3_debug_ap.h"

#include <stdbool.h>

#ifdef S3_DEBUG_AP_PC_TEST

static s3_debug_ap_status_t s_status;
static s3_debug_ap_status_cb_t s_status_cb;
static esp_err_t s_test_start_result = ESP_OK;

static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

void s3_debug_ap_test_set_start_result(esp_err_t result)
{
    s_test_start_result = result;
}

void s3_debug_ap_test_reset(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    s_status_cb = NULL;
    s_test_start_result = ESP_OK;
}

esp_err_t s3_debug_ap_init(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    if (!enable) {
        set_status(S3_DEBUG_AP_STATUS_OFF);
        return ESP_OK;
    }

    set_status(S3_DEBUG_AP_STATUS_STARTING);
    if (s_test_start_result != ESP_OK) {
        set_status(S3_DEBUG_AP_STATUS_ERROR);
        return s_test_start_result;
    }
    set_status(S3_DEBUG_AP_STATUS_ON);
    return ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}

#else

static s3_debug_ap_status_t s_status = S3_DEBUG_AP_STATUS_OFF;
static s3_debug_ap_status_cb_t s_status_cb;

static void set_status(s3_debug_ap_status_t status)
{
    s_status = status;
    if (s_status_cb) {
        s_status_cb(status);
    }
}

esp_err_t s3_debug_ap_init(void)
{
    s_status = S3_DEBUG_AP_STATUS_OFF;
    return ESP_OK;
}

esp_err_t s3_debug_ap_set_status_callback(s3_debug_ap_status_cb_t cb)
{
    s_status_cb = cb;
    return ESP_OK;
}

esp_err_t s3_debug_ap_request(bool enable)
{
    set_status(enable ? S3_DEBUG_AP_STATUS_ERROR : S3_DEBUG_AP_STATUS_OFF);
    return enable ? ESP_ERR_NOT_SUPPORTED : ESP_OK;
}

s3_debug_ap_status_t s3_debug_ap_status(void)
{
    return s_status;
}

#endif
