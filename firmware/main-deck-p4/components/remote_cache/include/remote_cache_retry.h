#pragma once

#include <stdbool.h>
#include <stdint.h>

#define REMOTE_CACHE_BUSY_RETRY_LIMIT 8u

bool remote_cache_should_retry(int err, uint8_t attempt);
uint32_t remote_cache_retry_delay_ms(uint8_t attempt);
