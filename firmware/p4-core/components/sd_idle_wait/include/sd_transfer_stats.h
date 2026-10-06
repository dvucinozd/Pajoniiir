#pragma once
#include <stdint.h>
typedef struct { uint32_t reads, writes, errors, read_max_us, write_max_us; } sd_transfer_stats_t;
void sd_transfer_get_stats(sd_transfer_stats_t *out);
