#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include "controller_runtime.h"

unsigned test_mutex_creations;
unsigned test_mutex_waits;
int test_fail_mutex_create = 1;

static esp_err_t event_cb(const flx4_control_event_t *event, void *ctx)
{
    (void)event;
    (void)ctx;
    /* Application callbacks remain outside the runtime mutex. */
    controller_runtime_diagnostics_t d;
    controller_runtime_get_diagnostics(&d);
    return ESP_OK;
}

static void *producer(void *ctx)
{
    (void)ctx;
    usb_midi_message_t msg = { .len = 3, .cin = 9, .status = 0x90, .data1 = 0x0b };
    for (unsigned i = 0; i < 10000; ++i) {
        msg.data2 = (i & 1) ? 127 : 0;
        (void)controller_runtime_handle_midi(&msg);
        if (i % 100 == 0) controller_runtime_set_connected((i / 100) & 1);
    }
    return NULL;
}

static void *consumer(void *ctx)
{
    (void)ctx;
    for (unsigned i = 0; i < 10000; ++i) {
        (void)controller_runtime_dispatch_pending(8);
        (void)controller_runtime_pending_count();
    }
    return NULL;
}

int main(void)
{
    controller_runtime_config_t config = { .event_cb = event_cb, .publish_connection_events = true };
    assert(controller_runtime_init(&config) == ESP_ERR_NO_MEM);
    assert(controller_runtime_dispatch_pending(1) == 0);
    test_fail_mutex_create = 0;
    assert(controller_runtime_init(&config) == ESP_OK);
    controller_runtime_set_builtin_flx4_enabled(true);
    pthread_t input, output;
    assert(pthread_create(&input, NULL, producer, NULL) == 0);
    assert(pthread_create(&output, NULL, consumer, NULL) == 0);
    assert(pthread_join(input, NULL) == 0);
    assert(pthread_join(output, NULL) == 0);
    controller_runtime_set_connected(false);
    while (controller_runtime_dispatch_pending(64)) {}
    controller_runtime_diagnostics_t d;
    controller_runtime_get_diagnostics(&d);
    assert(d.midi_messages == 10000 && !d.connected);
    assert(test_mutex_creations == 2 && test_mutex_waits >= 20000);
    assert(controller_runtime_pending_count() == 0);
    puts("PASS firmware mutex allocation failure, concurrent MIDI/dispatch and callback reentry");
    return 0;
}
