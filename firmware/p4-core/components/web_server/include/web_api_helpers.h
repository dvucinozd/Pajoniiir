#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#define WEB_API_CANONICAL_HOSTNAME "pajoniiir.local"

size_t web_api_json_escape(const char *src, char *dst, size_t dst_size);
int web_api_format_beat_fx_json(char *dst,
                                size_t dst_size,
                                int effect,
                                int beat,
                                int target,
                                unsigned depth,
                                bool enabled);
int web_api_format_beat_fx_echo_diag_json(char *dst,
                                          size_t dst_size,
                                          bool allocated1,
                                          bool allocated2,
                                          bool enabled1,
                                          bool enabled2,
                                          bool delay_mode1,
                                          bool delay_mode2,
                                          unsigned delay_ms1,
                                          unsigned delay_ms2);
int web_api_format_controller_json(char *dst,
                                   size_t dst_size,
                                   bool present,
                                   unsigned vid,
                                   unsigned pid,
                                   const char *product_escaped,
                                   bool midi_in,
                                   bool midi_out,
                                   bool usb_audio,
                                   const char *active_profile_escaped,
                                   const char *profile_state_escaped,
                                   unsigned profile_count);
int web_api_format_service_log_json(char *dst,
                                    size_t dst_size,
                                    bool available,
                                    uint32_t queue_depth,
                                    uint32_t queue_capacity,
                                    uint32_t dropped,
                                    uint32_t written,
                                    uint64_t current_bytes,
                                    int32_t last_error);
int web_api_alloc_printf(char **out, const char *fmt, ...);
bool web_api_parse_int32(const char *value,
                         int32_t minimum,
                         int32_t maximum,
                         int32_t *out);
uint32_t web_api_clamp_seek_ms(int value, uint32_t duration_ms, bool duration_known);

#define WEB_API_PROFILE_MIN_SIZE 32u
#define WEB_API_PROFILE_MAX_SIZE 16384u

bool web_api_profile_content_length_valid(size_t content_len);
/* Missing/empty means overwrite=false. Only the explicit values "0" and "1"
 * are accepted; returns false for malformed input. */
bool web_api_profile_overwrite_parse(const char *value, bool *overwrite);

/*
 * DNS-rebinding allow-list for API and captive-portal requests. `ap_ipv4` is
 * read from the active AP netif, so deployments are not coupled to
 * 192.168.4.1. The canonical mDNS name is always accepted.
 */
bool web_api_host_allowed(const char *host_header, const char *ap_ipv4);
