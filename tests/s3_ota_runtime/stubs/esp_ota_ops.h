#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_app_desc.h"
#include "esp_err.h"
#include "esp_partition.h"

typedef uint32_t esp_ota_handle_t;
typedef int esp_ota_img_states_t;

const esp_partition_t *esp_ota_get_next_update_partition(const void *start_from);
esp_err_t esp_ota_begin(const esp_partition_t *partition, size_t image_size,
                        esp_ota_handle_t *out_handle);
esp_err_t esp_ota_write(esp_ota_handle_t handle, const void *data, size_t size);
esp_err_t esp_ota_abort(esp_ota_handle_t handle);
esp_err_t esp_ota_end(esp_ota_handle_t handle);
esp_err_t esp_ota_get_partition_description(const esp_partition_t *partition,
                                            esp_app_desc_t *out_desc);
esp_err_t esp_ota_set_boot_partition(const esp_partition_t *partition);
