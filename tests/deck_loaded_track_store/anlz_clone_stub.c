#include "rekordbox_anlz.h"

#include <stdlib.h>
#include <string.h>

bool anlz_clone_stub_fail;

esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *out)
{
    if (!src || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(out, 0, sizeof(*out));
    if (anlz_clone_stub_fail) {
        return ESP_ERR_NO_MEM;
    }

    *out = *src;
    out->beats = NULL;
    out->waveform_high = NULL;
    if (src->beat_count > 0u && src->beats) {
        size_t bytes = (size_t)src->beat_count * sizeof(*src->beats);
        out->beats = malloc(bytes);
        if (!out->beats) {
            memset(out, 0, sizeof(*out));
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->beats, src->beats, bytes);
    }
    if (src->waveform_high_len > 0u && src->waveform_high) {
        out->waveform_high = malloc(src->waveform_high_len);
        if (!out->waveform_high) {
            free(out->beats);
            memset(out, 0, sizeof(*out));
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->waveform_high, src->waveform_high,
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
}
