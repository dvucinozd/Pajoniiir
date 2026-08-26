#include "s3_debug_ap.h"

#include <assert.h>
#include <stdio.h>

static s3_debug_ap_status_t s_seen[8];
static int s_seen_count;
static uint32_t s_seen_token;

static void status_cb(s3_debug_ap_status_t status)
{
    assert(s_seen_count < (int)(sizeof(s_seen) / sizeof(s_seen[0])));
    s_seen[s_seen_count++] = status;
}

static void token_cb(uint32_t token)
{
    s_seen_token = token;
}

static void reset_seen(void)
{
    s_seen_count = 0;
    s_seen_token = 0u;
}

void s3_debug_ap_test_set_start_result(esp_err_t result);
void s3_debug_ap_test_reset(void);

static void test_default_off(void)
{
    s3_debug_ap_test_reset();
    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_OFF);
}

static void test_start_success_reports_starting_then_on(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    assert(s3_debug_ap_set_token_callback(token_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_OK);

    assert(s3_debug_ap_request(true) == ESP_OK);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ON);
    assert(s_seen_count == 2);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_STARTING);
    assert(s_seen[1] == S3_DEBUG_AP_STATUS_ON);
    assert(s_seen_token == 123456u);
}

static void test_start_failure_reports_error(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    assert(s3_debug_ap_set_token_callback(token_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_FAIL);

    assert(s3_debug_ap_request(true) == ESP_FAIL);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ERROR);
    assert(s_seen_count == 2);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_STARTING);
    assert(s_seen[1] == S3_DEBUG_AP_STATUS_ERROR);
    assert(s_seen_token == 0u);
}

static void test_start_failure_requires_off_before_retry(void)
{
    s3_debug_ap_test_reset();
    s3_debug_ap_test_set_start_result(ESP_FAIL);
    assert(s3_debug_ap_request(true) == ESP_FAIL);

    s3_debug_ap_test_set_start_result(ESP_OK);
    assert(s3_debug_ap_request(true) == ESP_ERR_INVALID_STATE);
    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ERROR);
    assert(s3_debug_ap_request(false) == ESP_OK);
    assert(s3_debug_ap_request(true) == ESP_OK);
    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_ON);
}

static void test_stop_reports_off(void)
{
    s3_debug_ap_test_reset();
    reset_seen();
    assert(s3_debug_ap_set_status_callback(status_cb) == ESP_OK);
    assert(s3_debug_ap_set_token_callback(token_cb) == ESP_OK);
    s3_debug_ap_test_set_start_result(ESP_OK);
    assert(s3_debug_ap_request(true) == ESP_OK);

    reset_seen();
    assert(s3_debug_ap_request(false) == ESP_OK);

    assert(s3_debug_ap_status() == S3_DEBUG_AP_STATUS_OFF);
    assert(s_seen_count == 1);
    assert(s_seen[0] == S3_DEBUG_AP_STATUS_OFF);
    assert(s_seen_token == 0u);
}

int main(void)
{
    test_default_off();
    test_start_success_reports_starting_then_on();
    test_start_failure_reports_error();
    test_start_failure_requires_off_before_retry();
    test_stop_reports_off();
    puts("s3_debug_ap_state tests passed");
    return 0;
}
