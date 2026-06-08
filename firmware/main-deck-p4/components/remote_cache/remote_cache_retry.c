#include "remote_cache_retry.h"

#ifndef ESP_ERR_INVALID_STATE
#define ESP_ERR_INVALID_STATE 0x103
#endif

bool remote_cache_should_retry(int err, uint8_t attempt)
{
    return err == ESP_ERR_INVALID_STATE && attempt < REMOTE_CACHE_BUSY_RETRY_LIMIT;
}

uint32_t remote_cache_retry_delay_ms(uint8_t attempt)
{
    uint32_t delay = 250u * (uint32_t)(attempt == 0 ? 1 : attempt);
    return delay > 1500u ? 1500u : delay;
}
