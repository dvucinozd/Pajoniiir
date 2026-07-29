#include <stdlib.h>
#include <string.h>

#include "rekordbox_anlz.h"

uint32_t anlz_clone_stub_calls;
uint32_t anlz_free_stub_calls;
bool anlz_clone_stub_fail;

esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *out)
{
    if (!src || !out || src == out) {
        return ESP_ERR_INVALID_ARG;
    }
    anlz_clone_stub_calls++;
    if (anlz_clone_stub_fail) {
        memset(out, 0, sizeof(*out));
        return ESP_ERR_NO_MEM;
    }
    memset(out, 0, sizeof(*out));
    *out = *src;
    out->beats = NULL;
    out->waveform_high = NULL;

    if (src->beat_count > 0u) {
        if (!src->beats) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_INVALID_ARG;
        }
        out->beats = malloc((size_t)src->beat_count * sizeof(*out->beats));
        if (!out->beats) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->beats,
               src->beats,
               (size_t)src->beat_count * sizeof(*out->beats));
    }
    if (src->waveform_high_len > 0u) {
        if (!src->waveform_high) {
            anlz_free(out);
            return ESP_ERR_INVALID_ARG;
        }
        out->waveform_high = malloc(src->waveform_high_len);
        if (!out->waveform_high) {
            anlz_free(out);
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->waveform_high,
               src->waveform_high,
               src->waveform_high_len);
    }
    return ESP_OK;
}

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) {
        return;
    }
    free(meta->beats);
    free(meta->waveform_high);
    memset(meta, 0, sizeof(*meta));
    anlz_free_stub_calls++;
}
