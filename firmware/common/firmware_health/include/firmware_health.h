#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "esp_ota_ops.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *project_name;
    const char *version;
    const char *idf_version;
    const char *partition_label;
    uint32_t partition_address;
    uint32_t partition_size;
    esp_ota_img_states_t image_state;
    bool rollback_pending;
} firmware_health_info_t;

esp_err_t firmware_health_init(void);
esp_err_t firmware_health_mark_ready(void);
esp_err_t firmware_health_get_info(firmware_health_info_t *out_info);

#ifdef __cplusplus
}
#endif
