#include "audio_format.h"

#include <stdbool.h>
#include <string.h>

static bool ascii_ieq(char a, char b)
{
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
    }
    return a == b;
}

static bool ends_with_ext(const char *path, const char *ext)
{
    if (!path || !ext) {
        return false;
    }
    size_t path_len = strlen(path);
    size_t ext_len = strlen(ext);
    if (path_len < ext_len) {
        return false;
    }
    const char *tail = path + path_len - ext_len;
    for (size_t i = 0; i < ext_len; ++i) {
        if (!ascii_ieq(tail[i], ext[i])) {
            return false;
        }
    }
    return true;
}

audio_format_t audio_format_detect_header(const uint8_t *data, size_t len)
{
    if (!data || len < 3) {
        return AUDIO_FORMAT_UNKNOWN;
    }

    if (len >= 12 &&
        memcmp(data, "RIFF", 4) == 0 &&
        memcmp(data + 8, "WAVE", 4) == 0) {
        return AUDIO_FORMAT_WAV;
    }
    if (len >= 4 && memcmp(data, "fLaC", 4) == 0) {
        return AUDIO_FORMAT_FLAC;
    }
    if (memcmp(data, "ID3", 3) == 0) {
        return AUDIO_FORMAT_MP3;
    }
    if (len >= 2 && data[0] == 0xff && (data[1] & 0xe0u) == 0xe0u) {
        return AUDIO_FORMAT_MP3;
    }

    return AUDIO_FORMAT_UNKNOWN;
}

audio_format_t audio_format_detect_path(const char *path)
{
    if (ends_with_ext(path, ".mp3")) {
        return AUDIO_FORMAT_MP3;
    }
    if (ends_with_ext(path, ".wav") || ends_with_ext(path, ".wave")) {
        return AUDIO_FORMAT_WAV;
    }
    if (ends_with_ext(path, ".flac")) {
        return AUDIO_FORMAT_FLAC;
    }
    return AUDIO_FORMAT_UNKNOWN;
}

const char *audio_format_name(audio_format_t format)
{
    switch (format) {
    case AUDIO_FORMAT_MP3:
        return "MP3";
    case AUDIO_FORMAT_WAV:
        return "WAV";
    case AUDIO_FORMAT_FLAC:
        return "FLAC";
    case AUDIO_FORMAT_UNKNOWN:
    default:
        return "UNKNOWN";
    }
}
