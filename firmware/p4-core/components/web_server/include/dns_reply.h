#pragma once

#include <stddef.h>
#include <stdint.h>

size_t dns_build_captive_reply(const uint8_t *query,
                               size_t query_len,
                               uint8_t *reply,
                               size_t reply_size,
                               const uint8_t ip[4]);
