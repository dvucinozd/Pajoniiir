/* SPDX-License-Identifier: Apache-2.0 */
#include "controller_led_runtime.h"

#include "controller_profile_runtime.h"
#include "controller_usb_host.h"
#include "flx4_led_midi.h"

static uint32_t s_dynamic_packets;
static uint32_t s_builtin_packets;
static uint32_t s_builtin_fallbacks;
static uint32_t s_unsupported;
static uint32_t s_send_failures;
static bool s_builtin_flx4_enabled;

void controller_led_runtime_set_builtin_flx4_enabled(bool enabled)
{
    __atomic_store_n(&s_builtin_flx4_enabled, enabled, __ATOMIC_RELEASE);
}

bool controller_led_runtime_build_packet(uint8_t led,
                                         uint8_t state,
                                         uint8_t deck,
                                         uint8_t packet[4])
{
    if (!packet) {
        return false;
    }

    const bool profile_active = controller_profile_runtime_active();
    const bool builtin_enabled =
        __atomic_load_n(&s_builtin_flx4_enabled, __ATOMIC_ACQUIRE);
    const bool authoritative = builtin_enabled &&
        flx4_led_midi_builtin_authoritative(led);
    if (profile_active && !authoritative &&
        controller_profile_runtime_map_led(led, deck, state, packet)) {
        (void)__atomic_add_fetch(&s_dynamic_packets, 1u,
                                 __ATOMIC_RELAXED);
        return true;
    }

    if (builtin_enabled &&
        flx4_led_midi_build_packet(led, state, deck, packet)) {
        (void)__atomic_add_fetch(&s_builtin_packets, 1u,
                                 __ATOMIC_RELAXED);
        if (profile_active && !authoritative) {
            (void)__atomic_add_fetch(&s_builtin_fallbacks, 1u,
                                     __ATOMIC_RELAXED);
        }
        return true;
    }

    (void)__atomic_add_fetch(&s_unsupported, 1u, __ATOMIC_RELAXED);
    return false;
}

esp_err_t controller_led_runtime_send(uint8_t led,
                                      uint8_t state,
                                      uint8_t deck)
{
    uint8_t packet[4];
    if (!controller_led_runtime_build_packet(led, state, deck, packet)) {
        return ESP_ERR_NOT_SUPPORTED;
    }
    const esp_err_t rc = controller_usb_host_send_packet(packet);
    if (rc != ESP_OK) {
        (void)__atomic_add_fetch(&s_send_failures, 1u,
                                 __ATOMIC_RELAXED);
        return rc;
    }

    controller_usb_identity_t identity;
    if (controller_usb_host_get_identity(&identity) &&
        identity.vid == 0x2B73u && identity.pid == 0x0045u &&
        flx4_led_midi_build_shifted_mirror_packet(
            led, state, deck, packet)) {
        const esp_err_t mirror_rc = controller_usb_host_send_packet(packet);
        if (mirror_rc != ESP_OK) {
            (void)__atomic_add_fetch(&s_send_failures, 1u,
                                     __ATOMIC_RELAXED);
            return mirror_rc;
        }
    }
    return ESP_OK;
}

void controller_led_runtime_get_diagnostics(
    controller_led_runtime_diagnostics_t *diag_out)
{
    if (!diag_out) {
        return;
    }
    *diag_out = (controller_led_runtime_diagnostics_t) {
        .dynamic_packets =
            __atomic_load_n(&s_dynamic_packets, __ATOMIC_ACQUIRE),
        .builtin_packets =
            __atomic_load_n(&s_builtin_packets, __ATOMIC_ACQUIRE),
        .builtin_fallbacks =
            __atomic_load_n(&s_builtin_fallbacks, __ATOMIC_ACQUIRE),
        .unsupported =
            __atomic_load_n(&s_unsupported, __ATOMIC_ACQUIRE),
        .send_failures =
            __atomic_load_n(&s_send_failures, __ATOMIC_ACQUIRE),
    };
}
