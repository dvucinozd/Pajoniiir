/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_host_manager.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/task.h"

static const char *TAG = "usb_host_mgr";

typedef enum {
    MANAGER_STOPPED = 0,
    MANAGER_STARTING,
    MANAGER_READY,
    MANAGER_FAILED,
} manager_state_t;

static usb_host_manager_config_t s_config;
static TaskHandle_t s_daemon_task;
static esp_err_t s_install_result = ESP_ERR_INVALID_STATE;
static uint32_t s_daemon_iterations;
static uint32_t s_daemon_errors;
static uint32_t s_no_clients_events;
static uint32_t s_all_free_events;
static bool s_root_power_requested;
static manager_state_t s_state = MANAGER_STOPPED;

static inline manager_state_t state_get(void)
{
    return (manager_state_t)__atomic_load_n(&s_state, __ATOMIC_ACQUIRE);
}

static inline void state_set(manager_state_t state)
{
    __atomic_store_n(&s_state, state, __ATOMIC_RELEASE);
}

static void usb_host_daemon_task(void *arg)
{
    TaskHandle_t starter = (TaskHandle_t)arg;
    const usb_host_config_t host_config = {
        .skip_phy_setup = false,
        .root_port_unpowered = s_config.root_port_unpowered,
        .intr_flags = s_config.intr_flags,
        .enum_filter_cb = NULL,
        .peripheral_map = s_config.peripheral_map,
    };

    s_install_result = usb_host_install(&host_config);
    if (s_install_result == ESP_OK) {
        state_set(MANAGER_READY);
        ESP_LOGI(TAG,
                 "USB Host Library ready peripheral_map=0x%02X root_unpowered=%u",
                 s_config.peripheral_map,
                 s_config.root_port_unpowered ? 1u : 0u);
    } else {
        state_set(MANAGER_FAILED);
        ESP_LOGE(TAG, "usb_host_install failed: %s",
                 esp_err_to_name(s_install_result));
    }
    xTaskNotifyGive(starter);

    if (s_install_result != ESP_OK) {
        s_daemon_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    for (;;) {
        uint32_t event_flags = 0u;
        const esp_err_t rc =
            usb_host_lib_handle_events(portMAX_DELAY, &event_flags);
        (void)__atomic_add_fetch(&s_daemon_iterations, 1u, __ATOMIC_RELAXED);

        if (rc != ESP_OK && rc != ESP_ERR_TIMEOUT) {
            (void)__atomic_add_fetch(&s_daemon_errors, 1u, __ATOMIC_RELAXED);
            ESP_LOGE(TAG, "usb_host_lib_handle_events: %s",
                     esp_err_to_name(rc));
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS) != 0u) {
            (void)__atomic_add_fetch(&s_no_clients_events, 1u,
                                     __ATOMIC_RELAXED);
        }
        if ((event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE) != 0u) {
            (void)__atomic_add_fetch(&s_all_free_events, 1u,
                                     __ATOMIC_RELAXED);
        }
    }
}

esp_err_t usb_host_manager_init(const usb_host_manager_config_t *config)
{
    if (!config || config->peripheral_map == 0u ||
        config->daemon_stack_size < 3072u || config->daemon_priority == 0u) {
        return ESP_ERR_INVALID_ARG;
    }

    manager_state_t expected = MANAGER_STOPPED;
    if (!__atomic_compare_exchange_n(&s_state, &expected, MANAGER_STARTING,
                                     false, __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        if (expected == MANAGER_READY) {
            return ESP_OK;
        }
        return expected == MANAGER_FAILED ? s_install_result
                                          : ESP_ERR_INVALID_STATE;
    }

    s_config = *config;
    s_install_result = ESP_ERR_INVALID_STATE;
    s_root_power_requested = !config->root_port_unpowered;
    const TaskHandle_t starter = xTaskGetCurrentTaskHandle();

    BaseType_t created;
    if (config->daemon_core_id == tskNO_AFFINITY) {
        created = xTaskCreate(usb_host_daemon_task, "usb_hostd",
                              config->daemon_stack_size, (void *)starter,
                              config->daemon_priority, &s_daemon_task);
    } else {
        created = xTaskCreatePinnedToCore(
            usb_host_daemon_task, "usb_hostd", config->daemon_stack_size,
            (void *)starter, config->daemon_priority, &s_daemon_task,
            config->daemon_core_id);
    }

    if (created != pdPASS) {
        s_daemon_task = NULL;
        s_install_result = ESP_ERR_NO_MEM;
        state_set(MANAGER_FAILED);
        return ESP_ERR_NO_MEM;
    }

    if (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(5000)) == 0u) {
        return ESP_ERR_TIMEOUT;
    }
    return s_install_result;
}

bool usb_host_manager_is_ready(void)
{
    return state_get() == MANAGER_READY;
}

esp_err_t usb_host_manager_set_all_root_power(bool enable)
{
    if (!usb_host_manager_is_ready()) {
        return ESP_ERR_INVALID_STATE;
    }
    const esp_err_t rc = usb_host_lib_set_root_port_power(enable);
    if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
        __atomic_store_n(&s_root_power_requested, enable, __ATOMIC_RELEASE);
    }
    return rc;
}

esp_err_t usb_host_manager_get_library_info(usb_host_lib_info_t *info_out)
{
    if (!info_out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!usb_host_manager_is_ready()) {
        memset(info_out, 0, sizeof(*info_out));
        return ESP_ERR_INVALID_STATE;
    }
    return usb_host_lib_info(info_out);
}

void usb_host_manager_get_diagnostics(usb_host_manager_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }
    *diag_out = (usb_host_manager_diagnostics_t) {
        .install_result = s_install_result,
        .daemon_iterations =
            __atomic_load_n(&s_daemon_iterations, __ATOMIC_ACQUIRE),
        .daemon_errors =
            __atomic_load_n(&s_daemon_errors, __ATOMIC_ACQUIRE),
        .no_clients_events =
            __atomic_load_n(&s_no_clients_events, __ATOMIC_ACQUIRE),
        .all_free_events =
            __atomic_load_n(&s_all_free_events, __ATOMIC_ACQUIRE),
        .peripheral_map = s_config.peripheral_map,
        .ready = usb_host_manager_is_ready(),
        .root_power_requested =
            __atomic_load_n(&s_root_power_requested, __ATOMIC_ACQUIRE),
    };
}
