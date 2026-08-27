/* SPDX-License-Identifier: Apache-2.0 */
/* Direct ESP32-P4 controller owner. */
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
#include "freertos/queue.h"
#include "freertos/task.h"
#include "usb_host_manager.h"

#define LOCAL_DISPATCH_BUDGET 64u
#define LOCAL_BOOTSTRAP_RETRY_MS 1000u
#define LOCAL_MANAGER_LOG_MS 5000u
#define LOCAL_PROFILE_QUEUE_DEPTH 1u
#define LOCAL_USB1_ROOT_INDEX 1u

void controller_usb_host_output_gate_set_connected(bool connected);

static const char *TAG = "p4_controller";
static TaskHandle_t s_dispatch_task;
static TaskHandle_t s_bootstrap_task;
static TaskHandle_t s_profile_task;
static QueueHandle_t s_profile_queue;
static atomic_bool s_local_connected;
static atomic_bool s_semantic_connected;
static atomic_bool s_bootstrap_started;
static atomic_bool s_bootstrap_ready;
static atomic_uint_fast32_t s_connection_epoch;
static uint32_t s_local_semantic_events;
static uint32_t s_local_queue_failures;
static uint32_t s_profile_activations;
static uint32_t s_profile_fallbacks;
static uint32_t s_bootstrap_failures;
static int32_t s_last_bootstrap_error = ESP_ERR_INVALID_STATE;

typedef struct {
    controller_usb_identity_t identity;
    uint32_t epoch;
} local_profile_work_t;

static inline void count_inc(uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static void record_bootstrap_failure(esp_err_t rc, const char *stage)
{
    count_inc(&s_bootstrap_failures);
    __atomic_store_n(&s_last_bootstrap_error, (int32_t)rc,
                     __ATOMIC_RELEASE);
    ESP_LOGE(TAG, "direct controller bootstrap %s failed: %s; retrying",
             stage, esp_err_to_name(rc));
}

static esp_err_t local_semantic_callback(const flx4_control_event_t *event,
                                         void *ctx)
{
    (void)ctx;
    if (!event) {
        return ESP_ERR_INVALID_ARG;
    }
    const esp_err_t rc = control_link_inject_semantic(
        event->type, event->id, event->value);
    if (rc == ESP_OK) {
        count_inc(&s_local_semantic_events);
    } else {
        count_inc(&s_local_queue_failures);
    }
    return rc;
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

static void set_semantic_connection(bool connected)
{
    const bool was_connected = atomic_exchange_explicit(
        &s_semantic_connected, connected, memory_order_acq_rel);
    if (was_connected == connected) {
        return;
    }
    publish_local_connection_state(connected);
    controller_runtime_set_connected(connected);
}

static void drain_runtime_before_disconnect(void)
{
    for (unsigned pass = 0u; pass < 8u; ++pass) {
        if (controller_runtime_dispatch_pending(LOCAL_DISPATCH_BUDGET) == 0u) {
            break;
        }
    }
}

static void local_profile_task(void *arg)
{
    (void)arg;
    local_profile_work_t work;
    for (;;) {
        if (xQueueReceive(s_profile_queue, &work, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (!atomic_load_explicit(&s_local_connected, memory_order_acquire) ||
            work.epoch != atomic_load_explicit(&s_connection_epoch,
                                               memory_order_acquire)) {
            continue;
        }

        uint16_t caps = 0u;
        if (work.identity.midi.in_ep_addr != 0u) {
            caps |= CTRL_DESC_CAP_MIDI_IN;
        }
        if (work.identity.midi.out_ep_addr != 0u) {
            caps |= CTRL_DESC_CAP_MIDI_OUT;
        }
        if (work.identity.usb_audio_active) {
            caps |= CTRL_DESC_CAP_USB_AUDIO;
        }
        const int matched = controller_profile_manager_on_descriptor_report(
            work.identity.vid, work.identity.pid, caps,
            work.identity.product, work.epoch);

        controller_profile_registry_t registry = {0};
        const bool current =
            atomic_load_explicit(&s_local_connected, memory_order_acquire) &&
            work.epoch == atomic_load_explicit(&s_connection_epoch,
                                               memory_order_acquire);
        const bool active = current &&
            controller_profile_manager_get_registry_snapshot(&registry) ==
                ESP_OK &&
            registry.connected_epoch == work.epoch &&
            registry.transfer_state == CPM_TRANSFER_ACTIVE;
        if (!current) {
            continue;
        }

        const bool builtin_flx4 = work.identity.vid == 0x2B73u &&
                                  work.identity.pid == 0x0045u;
        count_inc(active ? &s_profile_activations : &s_profile_fallbacks);
        if (active) {
            controller_runtime_request_snapshot();
            set_semantic_connection(true);
        }
        ESP_LOGW(TAG,
                 "direct controller resolved VID=0x%04X PID=0x%04X "
                 "profile=%s '%s'",
                 work.identity.vid, work.identity.pid,
                 matched >= 0 && active ? "local" :
                 (builtin_flx4 ? "built-in" : "unsupported"),
                 work.identity.product);
    }
}

static void local_connection_callback(bool connected,
                                      const controller_usb_identity_t *identity,
                                      void *ctx)
{
    (void)ctx;
    controller_usb_host_output_gate_set_connected(connected);

    if (connected && identity) {
        const bool builtin_flx4 = identity->vid == 0x2B73u &&
                                  identity->pid == 0x0045u;
        controller_runtime_set_builtin_flx4_enabled(builtin_flx4);
        controller_led_runtime_set_builtin_flx4_enabled(builtin_flx4);
        const uint32_t epoch = (uint32_t)atomic_fetch_add_explicit(
            &s_connection_epoch, 1u, memory_order_relaxed) + 1u;
        atomic_store_explicit(&s_local_connected, true, memory_order_release);
        if (builtin_flx4) {
            set_semantic_connection(true);
        }
        const local_profile_work_t work = {
            .identity = *identity,
            .epoch = epoch,
        };
        if (!s_profile_queue ||
            xQueueOverwrite(s_profile_queue, &work) != pdTRUE) {
            count_inc(&s_local_queue_failures);
        }
        ESP_LOGW(TAG,
                 "direct controller detected VID=0x%04X PID=0x%04X port=%u; "
                 "profile resolution queued '%s'",
                 identity->vid, identity->pid, identity->parent_port,
                 identity->product);
        return;
    }

    atomic_store_explicit(&s_local_connected, false, memory_order_release);
    set_semantic_connection(false);
    controller_runtime_set_builtin_flx4_enabled(false);
    controller_led_runtime_set_builtin_flx4_enabled(false);
    drain_runtime_before_disconnect();
    controller_profile_runtime_clear();
    (void)controller_profile_manager_on_disconnect();
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
    uint32_t manager_wait_ms = 0u;
    while (!usb_host_manager_is_ready()) {
        vTaskDelay(pdMS_TO_TICKS(100));
        manager_wait_ms += 100u;
        if (manager_wait_ms >= LOCAL_MANAGER_LOG_MS) {
            ESP_LOGW(TAG, "waiting for shared USB Host manager");
            manager_wait_ms = 0u;
        }
    }

    const controller_runtime_config_t runtime_config = {
        .event_cb = local_semantic_callback,
        .callback_ctx = NULL,
    };
    esp_err_t rc = controller_runtime_init(&runtime_config);
    if (rc != ESP_OK) {
        record_bootstrap_failure(rc, "runtime init");
        s_bootstrap_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    while (!s_profile_queue) {
        s_profile_queue = xQueueCreate(LOCAL_PROFILE_QUEUE_DEPTH,
                                       sizeof(local_profile_work_t));
        if (!s_profile_queue) {
            record_bootstrap_failure(ESP_ERR_NO_MEM, "profile queue");
            vTaskDelay(pdMS_TO_TICKS(LOCAL_BOOTSTRAP_RETRY_MS));
        }
    }
    while (!s_profile_task) {
        if (xTaskCreate(local_profile_task, "p4_ctrl_profile", 6144u,
                        NULL, 3u, &s_profile_task) != pdPASS) {
            record_bootstrap_failure(ESP_ERR_NO_MEM, "profile worker");
            vTaskDelay(pdMS_TO_TICKS(LOCAL_BOOTSTRAP_RETRY_MS));
        }
    }
    while (!s_dispatch_task) {
        if (xTaskCreate(local_dispatch_task, "p4_ctrl_dispatch", 4096u,
                        NULL, 4u, &s_dispatch_task) != pdPASS) {
            record_bootstrap_failure(ESP_ERR_NO_MEM, "dispatch worker");
            vTaskDelay(pdMS_TO_TICKS(LOCAL_BOOTSTRAP_RETRY_MS));
        }
    }

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
    for (;;) {
        rc = controller_usb_host_init(&usb_config);
        if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
            rc = usb_host_manager_set_root_power_by_index(
                LOCAL_USB1_ROOT_INDEX, true);
        }
        if (rc == ESP_OK || rc == ESP_ERR_INVALID_STATE) {
            break;
        }
        record_bootstrap_failure(rc, "USB host");
        vTaskDelay(pdMS_TO_TICKS(LOCAL_BOOTSTRAP_RETRY_MS));
    }
    __atomic_store_n(&s_last_bootstrap_error, (int32_t)ESP_OK,
                     __ATOMIC_RELEASE);
    atomic_store_explicit(&s_bootstrap_ready, true, memory_order_release);
    ESP_LOGW(TAG, "P4-only controller path ready on USB1");
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
        .bootstrap_failures =
            __atomic_load_n(&s_bootstrap_failures, __ATOMIC_ACQUIRE),
        .last_bootstrap_error =
            __atomic_load_n(&s_last_bootstrap_error, __ATOMIC_ACQUIRE),
        .local_connected =
            atomic_load_explicit(&s_local_connected, memory_order_acquire),
        .bootstrap_started =
            atomic_load_explicit(&s_bootstrap_started, memory_order_acquire),
        .bootstrap_ready =
            atomic_load_explicit(&s_bootstrap_ready, memory_order_acquire),
    };
}
