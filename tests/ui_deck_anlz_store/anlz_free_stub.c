#include <stdlib.h>
#include <string.h>

#include "rekordbox_anlz.h"

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) {
        return;
    }
    free(meta->beats);
    free(meta->waveform_high);
    memset(meta, 0, sizeof(*meta));
}
