#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rekordbox_anlz.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct anlz_snapshot anlz_snapshot_t;

typedef enum {
    ANLZ_SNAPSHOT_COMPACT = 0,
    ANLZ_SNAPSHOT_FULL,
} anlz_snapshot_kind_t;

/*
 * Creates one immutable, versioned ANLZ object with an initial reference.
 * COMPACT keeps beatgrid/cues/VBR/low waveform but omits the high-resolution
 * waveform used only by the UI. FULL retains every parsed field.
 */
anlz_snapshot_t *anlz_snapshot_create(const anlz_metadata_t *meta,
                                      anlz_snapshot_kind_t kind);

/*
 * Retains an existing snapshot. The publisher must still own a reference while
 * this function runs (stores guarantee that by retaining under their lock).
 */
anlz_snapshot_t *anlz_snapshot_retain(anlz_snapshot_t *snapshot);

/* Releases one reference and frees the immutable payload after the last one. */
void anlz_snapshot_release(anlz_snapshot_t *snapshot);

/* Read-only accessors; the returned metadata lives until the caller releases. */
const anlz_metadata_t *anlz_snapshot_metadata(const anlz_snapshot_t *snapshot);
uint32_t anlz_snapshot_version(const anlz_snapshot_t *snapshot);

#ifdef __cplusplus
}
#endif
