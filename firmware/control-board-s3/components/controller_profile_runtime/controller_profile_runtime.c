#include "controller_profile_runtime.h"

#include "controller_profile.h"

#include <string.h>

#ifdef CONTROLLER_PROFILE_RUNTIME_PC_TEST
#define RT_LOCK()   ((void)0)
#define RT_UNLOCK() ((void)0)
#define RT_LOGW(...) ((void)0)
#define RT_LOGI(...) ((void)0)
#else
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
static SemaphoreHandle_t s_lock;
#define RT_LOCK()   do { if (s_lock) xSemaphoreTake(s_lock, portMAX_DELAY); } while (0)
#define RT_UNLOCK() do { if (s_lock) xSemaphoreGive(s_lock); } while (0)
static const char *TAG = "ctrl_profile_rt";
#define RT_LOGW(...) ESP_LOGW(TAG, __VA_ARGS__)
#define RT_LOGI(...) ESP_LOGI(TAG, __VA_ARGS__)
#endif

static cp_profile_t s_profile;
static cp_runtime_t s_runtime;
static bool s_active;
static uint16_t s_bound_vid;
static uint16_t s_bound_pid;
static uint32_t s_bound_epoch;

void controller_profile_runtime_init(void)
{
#ifndef CONTROLLER_PROFILE_RUNTIME_PC_TEST
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
#endif
    s_active = false;
    s_bound_vid = 0u;
    s_bound_pid = 0u;
    s_bound_epoch = 0u;
}

bool controller_profile_runtime_activate(const uint8_t *blob, size_t len,
                                         uint16_t vid, uint16_t pid,
                                         uint32_t connection_epoch)
{
    if (!blob || len == 0) {
        controller_profile_runtime_clear();
        return true;
    }

    /* Parse into a scratch profile first so a failed parse never disturbs a
     * currently active one. */
    static cp_profile_t parsed;
    int rc = cp_profile_parse(blob, len, &parsed);
    if (rc != CP_OK) {
        RT_LOGW("profile parse failed rc=%d (VID=0x%04X PID=0x%04X)", rc, vid, pid);
        return false;
    }
    if (connection_epoch == 0u || parsed.vid != vid || parsed.pid != pid) {
        RT_LOGW("profile VID/PID mismatch blob=0x%04X:0x%04X transfer=0x%04X:0x%04X",
                parsed.vid, parsed.pid, vid, pid);
        return false;
    }

    RT_LOCK();
    s_profile = parsed;
    cp_runtime_init(&s_runtime);
    s_bound_vid = vid;
    s_bound_pid = pid;
    s_bound_epoch = connection_epoch;
    s_active = true;
    RT_UNLOCK();
    RT_LOGI("dynamic profile active: VID=0x%04X PID=0x%04X inputs=%u",
            s_profile.vid, s_profile.pid, (unsigned)s_profile.input_count);
    return true;
}

void controller_profile_runtime_clear(void)
{
    RT_LOCK();
    s_active = false;
    memset(&s_runtime, 0, sizeof(s_runtime));
    s_bound_vid = 0u;
    s_bound_pid = 0u;
    s_bound_epoch = 0u;
    RT_UNLOCK();
}

bool controller_profile_runtime_bound_to(uint16_t vid, uint16_t pid,
                                         uint32_t connection_epoch)
{
    RT_LOCK();
    bool bound = s_active && connection_epoch != 0u &&
                 s_bound_vid == vid && s_bound_pid == pid &&
                 s_bound_epoch == connection_epoch;
    RT_UNLOCK();
    return bound;
}

bool controller_profile_runtime_active(void)
{
    RT_LOCK();
    bool active = s_active;
    RT_UNLOCK();
    return active;
}

bool controller_profile_runtime_map(uint8_t status, uint8_t data1, uint8_t data2,
                                     uint8_t *type, uint8_t *id, int16_t *value)
{
    bool matched = false;
    RT_LOCK();
    if (s_active) {
        cp_event_t ev;
        if (cp_runtime_process(&s_profile, &s_runtime, status, data1, data2, &ev)) {
            if (type) *type = ev.type;
            if (id) *id = ev.id;
            if (value) *value = ev.value;
            matched = true;
        }
    }
    RT_UNLOCK();
    return matched;
}

bool controller_profile_runtime_map_led(uint8_t led, uint8_t deck, uint8_t state,
                                        uint8_t packet[4])
{
    bool ok = false;
    RT_LOCK();
    if (s_active) {
        uint8_t midi[3];
        if (cp_profile_map_led(&s_profile, led, deck, state, midi)) {
            /* USB-MIDI event packet: CIN = the MIDI status nibble (0x9 Note On,
             * 0xB Control Change), matching the built-in FLX4 LED packets. */
            packet[0] = (uint8_t)(midi[0] >> 4);
            packet[1] = midi[0];
            packet[2] = midi[1];
            packet[3] = midi[2];
            ok = true;
        }
    }
    RT_UNLOCK();
    return ok;
}

size_t controller_profile_runtime_emit_snapshot(controller_profile_runtime_emit_cb_t cb,
                                                void *ctx)
{
    cp_event_t events[CP_MAX_INPUTS];
    size_t n = 0u;
    RT_LOCK();
    if (s_active) {
        n = cp_runtime_collect_snapshot(&s_profile, &s_runtime,
                                        events, CP_MAX_INPUTS);
    }
    RT_UNLOCK();
    size_t emitted = 0u;
    while (emitted < n && cb &&
           cb(events[emitted].type, events[emitted].id,
              events[emitted].value, ctx)) {
        emitted++;
    }
    return emitted;
}
