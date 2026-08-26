#include "control_link_tx_serializer.h"

void control_link_tx_serializer_init(control_link_tx_serializer_t *tx,
                                     control_link_tx_lock_fn lock,
                                     control_link_tx_unlock_fn unlock,
                                     control_link_tx_write_fn write,
                                     void *io_ctx)
{
    if (!tx) return;
    tx->next_sequence = 0u;
    tx->lock = lock;
    tx->unlock = unlock;
    tx->write = write;
    tx->io_ctx = io_ctx;
}

bool control_link_tx_serializer_send(control_link_tx_serializer_t *tx,
                                     uint8_t *frame,
                                     size_t capacity,
                                     control_link_tx_build_fn build,
                                     const void *build_ctx,
                                     control_link_tx_result_t *out_result)
{
    if (out_result) *out_result = (control_link_tx_result_t) { 0 };
    if (!tx || !frame || capacity == 0u || !build || !tx->lock ||
        !tx->unlock || !tx->write || !tx->lock(tx->io_ctx)) {
        return false;
    }

    uint8_t sequence = tx->next_sequence++;
    size_t expected = build(frame, capacity, sequence, build_ctx);
    int written = expected > 0u
        ? tx->write(tx->io_ctx, frame, expected) : 0;
    tx->unlock(tx->io_ctx);

    if (out_result) {
        out_result->sequence = sequence;
        out_result->expected_bytes = expected;
        out_result->written_bytes = written;
    }
    return expected > 0u && written == (int)expected;
}
