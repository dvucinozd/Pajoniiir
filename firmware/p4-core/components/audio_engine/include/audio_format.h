#pragma once

#include <stddef.h>
#include <stdint.h>

typedef enum {
    AUDIO_FORMAT_UNKNOWN = 0,
    AUDIO_FORMAT_MP3,
    AUDIO_FORMAT_WAV,
    AUDIO_FORMAT_FLAC,
} audio_format_t;

audio_format_t audio_format_detect_header(const uint8_t *data, size_t len);
audio_format_t audio_format_detect_path(const char *path);
const char *audio_format_name(audio_format_t format);
