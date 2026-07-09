#pragma once

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

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
int web_api_alloc_printf(char **out, const char *fmt, ...);
uint32_t web_api_clamp_seek_ms(int value, uint32_t duration_ms, bool duration_known);
