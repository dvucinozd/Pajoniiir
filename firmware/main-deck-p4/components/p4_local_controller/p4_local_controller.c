/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Experimental production bridge for the P4-only controller migration.
 *
 * The default product build does not compile this file. The CI-only feature
 * variant keeps the proven S3 UART path alive as an automatic fallback while a
 * USB1 FLX4 is not locally owned. Once the P4 path connects, local MIDI events
 * feed the existing deck queue and duplicate S3 controller events are
 * suppressed. S3 heartbeat and service state continue to flow for A/B evidence.
 */
#include "p4_local_controller.h"

#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#include "sd_io_gate.h"
#include "usb_host_manager.h"
#include "usb_storage.h"

#define LOCAL_QUEUE_WAIT_MS 5u
#define LOCAL_DISPATCH_BUDGET 64u
#define LOCAL_BOOTSTRAP_WAIT_MS 5000u
#define LOCAL_USB1_ROOT_INDEX 1u

/* Internal hook provided by the routed controller transport selected by the
 * same feature flag. */
void controller_usb_host_output_gate_set_connected(bool connected);

static const char *TAG = "p4_local_ctrl";
static QueueHandle_t s_deck_queue;
static QueueHandle_t s_legacy_queue;
static TaskHandle_t s_legacy_forward_task;
static TaskHandle_t s_dispatch_task;
static TaskHandle_t s_bootstrap_task;
static atomic_bool s_local_connected;
static atomic_bool s_bootstrap_started;
static atomic_uint_fast8_t s_local_sequence;
static uint32_t s_local_semantic_events;
static uint32_t s_local_queue_failures;
static uint32_t s_legacy_forwarded_events;
static uint32_t s_legacy_suppressed_events;
static uint32_t s_profile_activations;
static uint32_t s_profile_fallbacks;

esp_err_t __real_control_link_init(QueueHandle_t ctrl_event_queue);
void __real_control_link_send_led_deck(led_id_t led, uint8_t state,
                                      uint8_t deck);
esp_err_t __real_usb_storage_init(usb_storage_event_cb_t cb);

static inline void count_inc(uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELAXED);
}

static bool enqueue_deck_event(const ctrl_event_t *event)
{
    if (!event || !s_deck_queue) {
        return false;
    }
    if (xQueueSend(s_deck_queue, event,
                   pdMS_TO_TICKS(LOCAL_QUEUE_WAIT_MS)) == pdTRUE) {
        return true;
    }
    count_inc(&s_local_queue_failures);
    ESP_LOGW(TAG, "deck queue rejected local event type=%u id=0x%02X value=%d",
             (unsigned)event->type, event->id, event->value);
    return false;
}

static bool semantic_to_deck_event(const flx4_control_event_t *source,
                                   ctrl_event_t *out)
{
    if (!source || !out) {
        return false;
    }

    ctrl_event_type_t type;
    switch (source->type) {
    case CTRL_TYPE_BUTTON:
        type = CTRL_EV_BUTTON;
        break;
    case CTRL_TYPE_ENCODER:
        if (source->id == 0u || control_link_id_is_deck_jog(source->id)) {
            type = CTRL_EV_JOG;
        } else if (source->id == 1u ||
                   source->id == CTRL_ID_BROWSE_DELTA ||
                   source->id == CTRL_ID_BROWSE_SHIFT_DELTA) {
            type = CTRL_EV_BROWSE;
        } else {
            return false;
        }
        break;
    case CTRL_TYPE_PITCH:
        type = CTRL_EV_PITCH;
        break;
    case CTRL_TYPE_STATE:
        type = CTRL_EV_STATE;
        break;
    default:
        return false;
    }

    *out = (ctrl_event_t) {
        .type = type,
        .id = source->id,
        .value = source->value,
        .seq = atomic_fetch_add_explicit(&s_local_sequence, 1u,
                                         memory_order_relaxed),
        .deck = control_link_id_deck(source->id),
        .control = control_link_id_control(source->id),
    };
    return true;
}

static void local_semantic_callback(const flx4_control_event_t *event,
                                    void *ctx)
{
    (void)ctx;
    ctrl_event_t deck_event;
    if (semantic_to_deck_event(event, &deck_event) &&
        enqueue_deck_event(&deck_event)) {
        count_inc(&s_local_semantic_events);
    }
}

static void local_midi_callback(const usb_midi_message_t *message, void *ctx)
{
    (void)ctx;
    if (message) {
        (void)controller_runtime_handle_midi(message);
    }
}

static void publish_local_connection_state(bool connected)
{
    const ctrl_event_t event = {
        .type = CTRL_EV_STATE,
        .id = CTRL_ID_FLX4_CONNECTION,
        .value = connected ? CTRL_FLX4_CONNECTED : CTRL_FLX4_DISCONNECTED,
        .seq = atomic_fetch_add_explicit(&s_local_sequence, 1u,
                                         memory_order_relaxed),
        .deck = CTRL_DECK_NONE,
        .control = CTRL_ID_FLX4_CONNECTION,
    };
    (void)enqueue_deck_event(&event);
}

static bool activate_matching_profile(const controller_usb_identity_t *identity)
{
    controller_profile_runtime_clear();
    if (!identity) {
        return false;
    }

    controller_profile_registry_t registry;
    if (controller_profile_manager_get_registry_snapshot(&registry) != ESP_OK) {
        count_inc(&s_profile_fallbacks);
        return false;
    }
    const int index = controller_profile_registry_match(
        &registry, identity->vid, identity->pid);
    if (index < 0 || index >= (int)registry.count) {
        count_inc(&s_profile_fallbacks);
        ESP_LOGI(TAG, "no local profile for 0x%04X:0x%04X; built-in FLX4 map",
                 identity->vid, identity->pid);
        return false;
    }

    const controller_profile_meta_t meta = registry.profiles[index];
    if (!meta.valid || meta.size == 0u || meta.size > CPM_MAX_PROFILE_SIZE) {
        count_inc(&s_profile_fallbacks);
        return false;
    }

    uint8_t *blob = malloc(meta.size);
    if (!blob) {
        count_inc(&s_profile_fallbacks);
        return false;
    }

    size_t bytes_read = 0u;
    sd_io_gate_begin();
    FILE *file = fopen(meta.path, "rb");
    if (file) {
        bytes_read = fread(blob, 1u, meta.size, file);
        fclose(file);
    }
    sd_io_gate_end();

    controller_profile_meta_t parsed = {0};
    const bool valid = bytes_read == meta.size &&
        controller_profile_meta_parse(blob, meta.size, &parsed) == ESP_OK &&
        controller_profile_runtime_activate(blob, meta.size,
                                            identity->vid, identity->pid);
    free(blob);

    if (valid) {
        count_inc(&s_profile_activations);
        ESP_LOGI(TAG, "local profile '%s' active for 0x%04X:0x%04X",
                 meta.id, identity->vid, identity->pid);
        return true;
    }

    controller_profile_runtime_clear();
    count_inc(&s_profile_fallbacks);
    ESP_LOGW(TAG, "local profile '%s' rejected; built-in FLX4 map", meta.id);
    return false;
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
        (void)activate_matching_profile(identity);
        atomic_store_explicit(&s_local_connected, true, memory_order_release);
        publish_local_connection_state(true);
        controller_runtime_set_connected(true);
        ESP_LOGW(TAG,
                 "P4-local controller active VID=0x%04X PID=0x%04X port=%u '%s'",
                 identity->vid, identity->pid, identity->parent_port,
                 identity->product);
        return;
    }

    /* Match the mature UART ordering: synthesize all held releases before the
     * disconnected level reaches deck_core. */
    controller_runtime_set_connected(false);
    drain_runtime_before_disconnect();
    controller_profile_runtime_clear();
    atomic_store_explicit(&s_local_connected, false, memory_order_release);
    publish_local_connection_state(false);
    ESP_LOGW(TAG, "P4-local controller disconnected; S3 fallback resumed");
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

static bool legacy_event_allowed(const ctrl_event_t *event)
{
    if (!event || !atomic_load_explicit(&s_local_connected, memory_order_acquire)) {
        return true;
    }
    if (event->type == CTRL_EV_HEARTBEAT) {
        return true;
    }
    return event->type == CTRL_EV_STATE &&
           event->id != CTRL_ID_FLX4_CONNECTION;
}

static void legacy_forward_task(void *arg)
{
    (void)arg;
    ctrl_event_t event;
    for (;;) {
        if (xQueueReceive(s_legacy_queue, &event, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (legacy_event_allowed(&event)) {
            if (enqueue_deck_event(&event)) {
                count_inc(&s_legacy_forwarded_events);
            }
        } else {
            count_inc(&s_legacy_suppressed_events);
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
    if (rc == ESP_OK && !s_dispatch_task) {
        if (xTaskCreate(local_dispatch_task, "p4_ctrl_dispatch", 4096u,
                        NULL, 4u, &s_dispatch_task) != pdPASS) {
            rc = ESP_ERR_NO_MEM;
        }
    }

    if (rc == ESP_OK) {
        const controller_usb_host_config_t usb_config = {
            .midi_cb = local_midi_callback,
            .connection_cb = local_connection_callback,
            .callback_ctx = NULL,
            .task_stack_size = 8192u,
            .task_priority = 5u,
            .task_core_id = tskNO_AFFINITY,
            /* A reconnect snapshot plus shifted pad mirrors can exceed 160
             * packets before the interrupt OUT endpoint drains them. */
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
        ESP_LOGW(TAG,
                 "experimental P4-local controller path ready on USB1; S3 fallback retained");
    } else {
        ESP_LOGE(TAG, "P4-local controller bootstrap failed: %s",
                 esp_err_to_name(rc));
    }
    s_bootstrap_task = NULL;
    vTaskDelete(NULL);
}

esp_err_t __wrap_control_link_init(QueueHandle_t ctrl_event_queue)
{
    if (!ctrl_event_queue || s_deck_queue) {
        return ESP_ERR_INVALID_ARG;
    }
    s_deck_queue = ctrl_event_queue;
    s_legacy_queue = xQueueCreate(64u, sizeof(ctrl_event_t));
    if (!s_legacy_queue) {
        return ESP_ERR_NO_MEM;
    }
    if (xTaskCreate(legacy_forward_task, "s3_ctrl_fallback", 4096u,
                    NULL, 4u, &s_legacy_forward_task) != pdPASS) {
        vQueueDelete(s_legacy_queue);
        s_legacy_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    return __real_control_link_init(s_legacy_queue);
}

esp_err_t __wrap_usb_storage_init(usb_storage_event_cb_t cb)
{
    const esp_err_t rc = __real_usb_storage_init(cb);
    if (rc != ESP_OK) {
        return rc;
    }
    bool expected = false;
    if (atomic_compare_exchange_strong_explicit(
            &s_bootstrap_started, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        if (xTaskCreate(local_bootstrap_task, "p4_ctrl_boot", 6144u,
                        NULL, 4u, &s_bootstrap_task) != pdPASS) {
            s_bootstrap_task = NULL;
            atomic_store_explicit(&s_bootstrap_started, false,
                                  memory_order_release);
            return ESP_ERR_NO_MEM;
        }
    }
    return ESP_OK;
}

void __wrap_control_link_send_led_deck(led_id_t led, uint8_t state,
                                      uint8_t deck)
{
    (void)controller_led_runtime_send((uint8_t)led, state, deck);
    __real_control_link_send_led_deck(led, state, deck);
}

bool p4_local_controller_enabled(void)
{
    return true;
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
        .legacy_forwarded_events =
            __atomic_load_n(&s_legacy_forwarded_events, __ATOMIC_ACQUIRE),
        .legacy_suppressed_events =
            __atomic_load_n(&s_legacy_suppressed_events, __ATOMIC_ACQUIRE),
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
