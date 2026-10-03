#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Check table structure before publication, then repeat with the actual source
 * length in the loader. A zero file_size means length is not known yet.
 * Repeated offsets are legal; a descending or zero-filled partial table is not.
 * This checks bounds, not the ID3-relative origin or MPEG frame alignment. */
static inline bool audio_pvbr_is_valid(const uint32_t *offsets, size_t count,
                                      size_t file_size)
{
    if (!offsets || count < 2u) return false;
    bool advances = false;
    for (size_t i = 0; i < count; ++i) {
        if (file_size && offsets[i] >= file_size) return false;
        if (i) {
            if (offsets[i] < offsets[i - 1u]) return false;
            advances = advances || offsets[i] > offsets[i - 1u];
        }
    }
    return advances;
}
