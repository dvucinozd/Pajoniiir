#include "control_link_tx_serializer.h"

#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    pthread_mutex_t tx_mutex;
    pthread_mutex_t wire_mutex;
    uint8_t wire[32];
    size_t wire_len;
    atomic_bool first_builder_entered;
    atomic_bool release_first_builder;
} fixture_t;

typedef struct {
    fixture_t *fixture;
    uint8_t tag;
    bool pause_in_builder;
} build_ctx_t;

typedef struct {
    control_link_tx_serializer_t *tx;
    build_ctx_t build;
    bool result;
} thread_ctx_t;

static bool lock_tx(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    return pthread_mutex_lock(&f->tx_mutex) == 0;
}

static void unlock_tx(void *ctx)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(pthread_mutex_unlock(&f->tx_mutex) == 0);
}

static int write_wire(void *ctx, const uint8_t *data, size_t bytes)
{
    fixture_t *f = (fixture_t *)ctx;
    assert(pthread_mutex_lock(&f->wire_mutex) == 0);
    memcpy(f->wire + f->wire_len, data, bytes);
    f->wire_len += bytes;
    assert(pthread_mutex_unlock(&f->wire_mutex) == 0);
    return (int)bytes;
}

static size_t build_test_frame(uint8_t *frame, size_t capacity,
                               uint8_t sequence, const void *ctx)
{
    const build_ctx_t *build = (const build_ctx_t *)ctx;
    assert(capacity >= 3u);
    if (build->pause_in_builder) {
        atomic_store_explicit(&build->fixture->first_builder_entered, true,
                              memory_order_release);
        while (!atomic_load_explicit(&build->fixture->release_first_builder,
                                     memory_order_acquire)) sched_yield();
    }
    frame[0] = sequence;
    frame[1] = build->tag;
    frame[2] = (uint8_t)(sequence ^ build->tag);
    return 3u;
}

static void *send_thread(void *arg)
{
    thread_ctx_t *thread = (thread_ctx_t *)arg;
    uint8_t frame[3];
    thread->result = control_link_tx_serializer_send(
        thread->tx, frame, sizeof(frame), build_test_frame, &thread->build, NULL);
    return NULL;
}

static void test_forced_interleaving_keeps_sequence_and_wire_order_together(void)
{
    fixture_t fixture = {
        .tx_mutex = PTHREAD_MUTEX_INITIALIZER,
        .wire_mutex = PTHREAD_MUTEX_INITIALIZER,
    };
    control_link_tx_serializer_t tx;
    control_link_tx_serializer_init(&tx, lock_tx, unlock_tx, write_wire,
                                    &fixture);
    thread_ctx_t first = {
        .tx = &tx,
        .build = { .fixture = &fixture, .tag = 0xA1u, .pause_in_builder = true },
    };
    thread_ctx_t second = {
        .tx = &tx,
        .build = { .fixture = &fixture, .tag = 0xB2u },
    };
    pthread_t t1;
    pthread_t t2;
    assert(pthread_create(&t1, NULL, send_thread, &first) == 0);
    while (!atomic_load_explicit(&fixture.first_builder_entered,
                                 memory_order_acquire)) sched_yield();
    assert(pthread_create(&t2, NULL, send_thread, &second) == 0);
    atomic_store_explicit(&fixture.release_first_builder, true,
                          memory_order_release);
    assert(pthread_join(t1, NULL) == 0);
    assert(pthread_join(t2, NULL) == 0);

    assert(first.result && second.result);
    assert(fixture.wire_len == 6u);
    assert(fixture.wire[0] == 0u && fixture.wire[1] == 0xA1u);
    assert(fixture.wire[3] == 1u && fixture.wire[4] == 0xB2u);
    assert(fixture.wire[2] == (uint8_t)(fixture.wire[0] ^ fixture.wire[1]));
    assert(fixture.wire[5] == (uint8_t)(fixture.wire[3] ^ fixture.wire[4]));
}

int main(void)
{
    test_forced_interleaving_keeps_sequence_and_wire_order_together();
    puts("control_link_tx_serializer tests passed");
    return 0;
}
