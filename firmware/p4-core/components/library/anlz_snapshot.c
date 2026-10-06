#include "anlz_snapshot.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct anlz_snapshot {
    uint32_t ref_count;
    uint32_t version;
    anlz_metadata_t metadata;
};

static uint32_t s_next_snapshot_version;

static uint32_t next_snapshot_version(void)
{
    uint32_t version =
        __atomic_add_fetch(&s_next_snapshot_version, 1u, __ATOMIC_RELAXED);
    if (version == 0u) {
        version =
            __atomic_add_fetch(&s_next_snapshot_version, 1u, __ATOMIC_RELAXED);
    }
    return version;
}

anlz_snapshot_t *anlz_snapshot_create(const anlz_metadata_t *meta,
                                      anlz_snapshot_kind_t kind)
{
    if (!meta ||
        (kind != ANLZ_SNAPSHOT_COMPACT && kind != ANLZ_SNAPSHOT_FULL)) {
        return NULL;
    }

    anlz_snapshot_t *snapshot = calloc(1u, sizeof(*snapshot));
    if (!snapshot) {
        return NULL;
    }

    anlz_metadata_t source = *meta;
    if (kind == ANLZ_SNAPSHOT_COMPACT) {
        source.waveform_high = NULL;
        source.waveform_high_len = 0u;
    }
    if (anlz_clone(&source, &snapshot->metadata) != ESP_OK) {
        free(snapshot);
        return NULL;
    }

    snapshot->version = next_snapshot_version();
    __atomic_store_n(&snapshot->ref_count, 1u, __ATOMIC_RELEASE);
    return snapshot;
}

anlz_snapshot_t *anlz_snapshot_retain(anlz_snapshot_t *snapshot)
{
    if (!snapshot) {
        return NULL;
    }

    uint32_t current =
        __atomic_load_n(&snapshot->ref_count, __ATOMIC_ACQUIRE);
    while (current != 0u && current != UINT32_MAX) {
        if (__atomic_compare_exchange_n(&snapshot->ref_count,
                                        &current,
                                        current + 1u,
                                        false,
                                        __ATOMIC_ACQ_REL,
                                        __ATOMIC_ACQUIRE)) {
            return snapshot;
        }
    }
    return NULL;
}

void anlz_snapshot_release(anlz_snapshot_t *snapshot)
{
    if (!snapshot) {
        return;
    }

    uint32_t previous =
        __atomic_fetch_sub(&snapshot->ref_count, 1u, __ATOMIC_ACQ_REL);
    if (previous == 1u) {
        anlz_free(&snapshot->metadata);
        memset(snapshot, 0, sizeof(*snapshot));
        free(snapshot);
    }
}

const anlz_metadata_t *anlz_snapshot_metadata(const anlz_snapshot_t *snapshot)
{
    return snapshot ? &snapshot->metadata : NULL;
}

uint32_t anlz_snapshot_version(const anlz_snapshot_t *snapshot)
{
    return snapshot ? snapshot->version : 0u;
}
