#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef bool (*control_link_tx_lock_fn)(void *ctx);
typedef void (*control_link_tx_unlock_fn)(void *ctx);
typedef int (*control_link_tx_write_fn)(void *ctx,
                                        const uint8_t *data,
                                        size_t bytes);
typedef size_t (*control_link_tx_build_fn)(uint8_t *frame,
                                           size_t capacity,
                                           uint8_t sequence,
                                           const void *ctx);

typedef struct {
    uint8_t next_sequence;
    control_link_tx_lock_fn lock;
    control_link_tx_unlock_fn unlock;
    control_link_tx_write_fn write;
    void *io_ctx;
} control_link_tx_serializer_t;

typedef struct {
    uint8_t sequence;
    size_t expected_bytes;
    int written_bytes;
} control_link_tx_result_t;

void control_link_tx_serializer_init(control_link_tx_serializer_t *tx,
                                     control_link_tx_lock_fn lock,
                                     control_link_tx_unlock_fn unlock,
                                     control_link_tx_write_fn write,
                                     void *io_ctx);

/* Sequence allocation, frame construction and the complete writer call form
 * one serialized transaction. A failed write still consumes its sequence so
 * diagnostics never mistake a later frame for the failed one. */
bool control_link_tx_serializer_send(control_link_tx_serializer_t *tx,
                                     uint8_t *frame,
                                     size_t capacity,
                                     control_link_tx_build_fn build,
                                     const void *build_ctx,
                                     control_link_tx_result_t *out_result);
