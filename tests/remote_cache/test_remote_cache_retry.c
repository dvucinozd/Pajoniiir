#include "remote_cache_retry.h"

#include <assert.h>
#include <stdio.h>

#define ESP_OK                 0
#define ESP_ERR_INVALID_STATE  0x103
#define ESP_ERR_NOT_FOUND      0x105

static void test_retries_host_busy_then_stops_at_limit(void)
{
    assert(remote_cache_should_retry(ESP_ERR_INVALID_STATE, 1));
    assert(remote_cache_should_retry(ESP_ERR_INVALID_STATE, REMOTE_CACHE_BUSY_RETRY_LIMIT - 1));
    assert(!remote_cache_should_retry(ESP_ERR_INVALID_STATE, REMOTE_CACHE_BUSY_RETRY_LIMIT));
}

static void test_does_not_retry_success_or_permanent_errors(void)
{
    assert(!remote_cache_should_retry(ESP_OK, 1));
    assert(!remote_cache_should_retry(ESP_ERR_NOT_FOUND, 1));
}

int main(void)
{
    test_retries_host_busy_then_stops_at_limit();
    test_does_not_retry_success_or_permanent_errors();
    puts("remote_cache retry tests passed");
    return 0;
}
