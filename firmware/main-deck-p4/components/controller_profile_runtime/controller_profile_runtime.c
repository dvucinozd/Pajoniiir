#include "controller_profile_runtime.h"

#include "controller_profile.h"

#include <stdlib.h>
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
static bool s_ready;

void controller_profile_runtime_init(void)
{
#ifndef CONTROLLER_PROFILE_RUNTIME_PC_TEST
    if (!s_lock) {
        s_lock = xSemaphoreCreateMutex();
    }
#endif
    s_active = false;
    s_ready = false;
}

bool controller_profile_runtime_activate(const uint8_t *blob, size_t len,
                                         uint16_t vid, uint16_t pid)
{
    if (!blob || len == 0) {
        controller_profile_runtime_clear();
        return true;
    }

    /* Parse into a scratch profile first so a failed parse never disturbs a
     * currently active one. */
    cp_profile_t *parsed = malloc(sizeof(*parsed));
    if (!parsed) {
        RT_LOGW("profile parse allocation failed (VID=0x%04X PID=0x%04X)",
                vid, pid);
        return false;
    }
    int rc = cp_profile_parse(blob, len, parsed);
    if (rc != CP_OK) {
        RT_LOGW("profile parse failed rc=%d (VID=0x%04X PID=0x%04X)", rc, vid, pid);
        free(parsed);
        return false;
    }
    if (parsed->vid != vid || parsed->pid != pid) {
        RT_LOGW("profile VID/PID mismatch blob=0x%04X:0x%04X transfer=0x%04X:0x%04X",
                parsed->vid, parsed->pid, vid, pid);
        free(parsed);
        return false;
    }

    RT_LOCK();
    s_profile = *parsed;
    cp_runtime_init(&s_runtime);
    s_active = true;
    s_ready = s_profile.init_sysex_len == 0;
    RT_UNLOCK();
    free(parsed);
    RT_LOGI("dynamic profile active: VID=0x%04X PID=0x%04X inputs=%u",
            s_profile.vid, s_profile.pid, (unsigned)s_profile.input_count);
    return true;
}

void controller_profile_runtime_clear(void)
{
    RT_LOCK();
    s_active = false;
    s_ready = false;
    RT_UNLOCK();
}

bool controller_profile_runtime_ready(void)
{
    RT_LOCK();
    bool ready = !s_active || s_ready;
    RT_UNLOCK();
    return ready;
}

bool controller_profile_runtime_filter_requires_smart_cfx(void)
{
    RT_LOCK();
    bool requires = !s_active || !(s_profile.flags & CP_PF_FILTER_ALWAYS);
    RT_UNLOCK();
    return requires;
}

bool controller_profile_runtime_initialize(controller_profile_packet_cb_t cb, void *ctx)
{
    bool ok = true;
    RT_LOCK();
    if (!s_active) ok = false;
    else if (!s_ready) {
        for (size_t off = 0; off < s_profile.init_sysex_len; off += 3) {
            size_t remaining = s_profile.init_sysex_len - off;
            size_t count = remaining < 3 ? remaining : 3;
            uint8_t packet[4] = {remaining > 3 ? 4 : (uint8_t)(4 + count), 0, 0, 0};
            memcpy(packet + 1, s_profile.init_sysex + off, count);
            if (!cb || !cb(packet, ctx)) { ok = false; break; }
        }
        /* Failed/partial initialization cannot be retried as an active profile. */
        s_ready = ok;
        if (!ok) s_active = false;
    }
    RT_UNLOCK();
    return ok;
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
    if (s_active && s_ready) {
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
    if (s_active && s_ready) {
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
    size_t n = 0;
    RT_LOCK();
    if (s_active && s_ready) {
        n = cp_runtime_emit_snapshot(&s_profile, &s_runtime, cb, ctx);
    }
    RT_UNLOCK();
    return n;
}
