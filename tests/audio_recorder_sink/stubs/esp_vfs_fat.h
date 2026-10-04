#pragma once
#include <stdint.h>
#include "esp_err.h"
esp_err_t esp_vfs_fat_info(const char *, uint64_t *, uint64_t *);
