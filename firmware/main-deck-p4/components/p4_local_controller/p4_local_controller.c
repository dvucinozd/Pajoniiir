/* SPDX-License-Identifier: Apache-2.0 */
/* Direct ESP32-P4 controller owner. There is deliberately no S3 fallback. */
#include "p4_local_controller.h"

#include <stdatomic.h>

#include "control_link.h"
#include "controller_led_runtime.h"
#include "controller_profile_manager.h"
#include "controller_profile_runtime.h"
#include "controller_runtime.h"
#include "controller_usb_host.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "usb_host_manager.h"

#define LOCAL_DISPATCH_BUDGET 64u
#define LOCAL_BOOTSTRAP_WAIT_MS 5000u
#define LOCAL_USB1_ROOT_INDEX 1u

void controller_usb_host_output_gate_set_connected(bool connected);

static const char *TAG = "p4_controller";
static TaskHandle_t s_dispatch_task;
static TaskHandle_t s_bootstrap_task;
static atomic_bool s_local_connected;
static atomic_bool s_bootstrap_started;
static atomic_uint_fast32_t s_connection_epoch;
static uint32_t s_local_semantic_events;
static uint32_t s_local_queue_failures;
static uint32_t s_profile_activations;
static uint32_t s_profile_fallbacks;

static inline void count_inc(uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static void local_semantic_callback(const flx4_control_event_t *event,
                                    void *ctx)
{
    (void)ctx;
    if (!event) {
        return;
    }
    if (control_link_inject_semantic(event->type, event->id, event->value) ==
        ESP_OK) {
        count_inc(&s_local_semantic_events);
    } else {
        count_inc(&s_local_queue_failures);
    }
}

static void local_midi_callback(const usb_midi_message_t *message, void *ctx)
{
    (void)ctx;
    if (message) {
        (void)controller_runtime_handle_midi(message);
    }
}

static esp_err_t local_led_sink(uint8_t led, uint8_t state, uint8_t deck,
                                void *ctx)
{
    (void)ctx;
    return controller_led_runtime_send(led, state, deck);
}

static void publish_local_connection_state(bool connected)
{
    if (control_link_inject_semantic(
            CTRL_TYPE_STATE, CTRL_ID_FLX4_CONNECTION,
            connected ? CTRL_FLX4_CONNECTED : CTRL_FLX4_DISCONNECTED) !=
        ESP_OK) {
        count_inc(&s_local_queue_failures);
    }
}

static void drain_runtime_before_disconnect(void)
{
    for (unsigned pass = 0u; pass < 8u; ++pass) {
        if (controller_runtime_dispatch_pending(LOCAL_DISPATCH_BUDGET) == 0u) {
            break;
        }
    }
}

static void local_connection_callback(bool connected,
                                      const controller_usb_identity_t *identity,
                                      void *ctx)
{
    (void)ctx;
    controller_usb_host_output_gate_set_connected(connected);

    if (connected && identity) {
        const uint32_t epoch = (uint32_t)atomic_fetch_add_explicit(
            &s_connection_epoch, 1u, memory_order_relaxed) + 1u;
        const int matched = controller_profile_manager_on_descriptor_report(
            identity->vid, identity->pid,
            CTRL_DESC_CAP_MIDI_IN | CTRL_DESC_CAP_MIDI_OUT |
                CTRL_DESC_CAP_USB_AUDIO,
            identity->product, epoch);
        controller_profile_registry_t registry = {0};
        const bool active =
            controller_profile_manager_get_registry_snapshot(&registry) == ESP_OK &&
            registry.transfer_state == CPM_TRANSFER_ACTIVE;
        count_inc(active ? &s_profile_activations : &s_profile_fallbacks);

        atomic_store_explicit(&s_local_connected, true, memory_order_release);
        publish_local_connection_state(true);
        controller_runtime_set_connected(true);
        ESP_LOGW(TAG,
                 "direct controller active VID=0x%04X PID=0x%04X port=%u profile=%s '%s'",
                 identity->vid, identity->pid, identity->parent_port,
                 matched >= 0 && active ? "local" : "built-in",
                 identity->product);
        return;
    }

    controller_runtime_set_connected(false);
    drain_runtime_before_disconnect();
    controller_profile_runtime_clear();
    (void)controller_profile_manager_on_disconnect();
    atomic_store_explicit(&s_local_connected, false, memory_order_release);
    publish_local_connection_state(false);
    ESP_LOGW(TAG, "direct controller disconnected");
}

static void local_dispatch_task(void *arg)
{
    (void)arg;
    for (;;) {
        const size_t dispatched =
            controller_runtime_dispatch_pending(LOCAL_DISPATCH_BUDGET);
        if (dispatched == 0u) {
            vTaskDelay(pdMS_TO_TICKS(2));
        } else {
            taskYIELD();
        }
    }
}

static void local_bootstrap_task(void *arg)
{
    (void)arg;
    const TickType_t deadline = xTaskGetTickCount() +
        pdMS_TO_TICKS(LOCAL_BOOTSTRAP_WAIT_MS);
    while (!usb_host_manager_is_ready()) {
        if ((int32_t)(deadline - xTaskGetTickCount()) <= 0) {
            ESP_LOGE(TAG, "shared USB Host manager did not become ready");
            s_bootstrap_task = NULL;
            vTaskDelete(NULL);
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    const controller_runtime_config_t runtime_config = {
        .event_cb = local_semantic_callback,
        .callback_ctx = NULL,
    };
    esp_err_t rc = controller_runtime_init(&runtime_config);
    if (rc == ESP_OK && !s_dispatch_task &&
        xTaskCreate(local_dispatch_task, "p4_ctrl_dispatch", 4096u,
                    NULL, 4u, &s_dispatch_task) != pdPASS) {
        rc = ESP_ERR_NO_MEM;
    }

    if (rc == ESP_OK) {
        const controller_usb_host_config_t usb_config = {
            .midi_cb = local_midi_callback,
            .connection_cb = local_connection_callback,
            .callback_ctx = NULL,
            .task_stack_size = 8192u,
            .task_priority = 5u,
            .task_core_id = tskNO_AFFINITY,
            .midi_out_queue_depth = 256u,
            .max_event_messages = 8,
        };
        rc = controller_usb_host_init(&usb_config);
    }
    if (rc == ESP_OK) {
        rc = usb_host_manager_set_root_power_by_index(
            LOCAL_USB1_ROOT_INDEX, true);
    }

    if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "P4-only controller path ready on USB1");
    } else {
        ESP_LOGE(TAG, "direct controller bootstrap failed: %s",
                 esp_err_to_name(rc));
    }
    s_bootstrap_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t p4_local_controller_start(void)
{
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_bootstrap_started, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }

    control_link_set_led_sink(local_led_sink, NULL);
    if (xTaskCreate(local_bootstrap_task, "p4_ctrl_boot", 6144u,
                    NULL, 4u, &s_bootstrap_task) != pdPASS) {
        s_bootstrap_task = NULL;
        atomic_store_explicit(&s_bootstrap_started, false,
                              memory_order_release);
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void p4_local_controller_get_diagnostics(
    p4_local_controller_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }
    *diag_out = (p4_local_controller_diagnostics_t) {
        .local_semantic_events =
            __atomic_load_n(&s_local_semantic_events, __ATOMIC_ACQUIRE),
        .local_queue_failures =
            __atomic_load_n(&s_local_queue_failures, __ATOMIC_ACQUIRE),
        .profile_activations =
            __atomic_load_n(&s_profile_activations, __ATOMIC_ACQUIRE),
        .profile_fallbacks =
            __atomic_load_n(&s_profile_fallbacks, __ATOMIC_ACQUIRE),
        .local_connected =
            atomic_load_explicit(&s_local_connected, memory_order_acquire),
        .bootstrap_started =
            atomic_load_explicit(&s_bootstrap_started, memory_order_acquire),
    };
}
