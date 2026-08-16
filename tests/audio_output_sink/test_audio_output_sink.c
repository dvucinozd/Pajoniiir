#include "audio_output_sink.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    audio_output_sink_result_t results[4];
    size_t written[4];
    uint32_t calls;
    uint32_t observed_timeout;
    uint8_t received[32];
    size_t received_bytes;
} fake_sink_t;

static audio_output_sink_result_t fake_write(void *ctx,
                                             const uint8_t *data,
                                             size_t bytes,
                                             size_t *written,
                                             uint32_t timeout_ticks)
{
    fake_sink_t *sink = (fake_sink_t *)ctx;
    uint32_t call = sink->calls++;
    sink->observed_timeout = timeout_ticks;
    size_t count = sink->written[call];
    if (count > bytes) count = bytes;
    if (sink->results[call] == AUDIO_OUTPUT_SINK_OK && count > 0u) {
        memcpy(sink->received + sink->received_bytes, data, count);
        sink->received_bytes += count;
    }
    *written = count;
    return sink->results[call];
}

static void test_complete_write_uses_one_bounded_call(void)
{
    uint8_t data[] = { 1, 2, 3, 4 };
    fake_sink_t sink = {
        .results = { AUDIO_OUTPUT_SINK_OK },
        .written = { sizeof(data) },
    };
    audio_output_sink_stats_t stats = { 0 };
    assert(audio_output_sink_write_all(fake_write, &sink, data, sizeof(data),
                                       7u, 3u, &stats) == AUDIO_OUTPUT_SINK_OK);
    assert(sink.calls == 1u);
    assert(sink.observed_timeout == 7u);
    assert(memcmp(sink.received, data, sizeof(data)) == 0);
    assert(stats.calls == 1u && stats.failed_blocks == 0u);
}

static void test_short_write_continues_only_with_unwritten_suffix(void)
{
    uint8_t data[] = { 1, 2, 3, 4, 5, 6 };
    fake_sink_t sink = {
        .results = { AUDIO_OUTPUT_SINK_OK, AUDIO_OUTPUT_SINK_OK },
        .written = { 2u, 4u },
    };
    audio_output_sink_stats_t stats = { 0 };
    assert(audio_output_sink_write_all(fake_write, &sink, data, sizeof(data),
                                       5u, 3u, &stats) == AUDIO_OUTPUT_SINK_OK);
    assert(sink.calls == 2u);
    assert(sink.received_bytes == sizeof(data));
    assert(memcmp(sink.received, data, sizeof(data)) == 0);
    assert(stats.short_writes == 1u && stats.failed_blocks == 0u);
}

static void test_timeout_and_error_are_bounded_and_counted(void)
{
    uint8_t data[] = { 1, 2, 3, 4 };
    audio_output_sink_stats_t stats = { 0 };
    fake_sink_t timeout = {
        .results = { AUDIO_OUTPUT_SINK_TIMEOUT },
    };
    assert(audio_output_sink_write_all(fake_write, &timeout, data, sizeof(data),
                                       9u, 3u, &stats) ==
           AUDIO_OUTPUT_SINK_TIMEOUT);
    assert(timeout.calls == 1u && timeout.observed_timeout == 9u);
    assert(stats.timeouts == 1u && stats.failed_blocks == 1u);

    fake_sink_t error = {
        .results = { AUDIO_OUTPUT_SINK_ERROR },
    };
    assert(audio_output_sink_write_all(fake_write, &error, data, sizeof(data),
                                       9u, 3u, &stats) == AUDIO_OUTPUT_SINK_ERROR);
    assert(error.calls == 1u);
    assert(stats.errors == 1u && stats.failed_blocks == 2u);
}

static void test_zero_progress_and_retry_exhaustion_fail(void)
{
    uint8_t data[] = { 1, 2, 3, 4 };
    audio_output_sink_stats_t stats = { 0 };
    fake_sink_t zero = {
        .results = { AUDIO_OUTPUT_SINK_OK },
        .written = { 0u },
    };
    assert(audio_output_sink_write_all(fake_write, &zero, data, sizeof(data),
                                       3u, 3u, &stats) == AUDIO_OUTPUT_SINK_ERROR);
    assert(zero.calls == 1u && stats.short_writes == 1u);

    fake_sink_t exhausted = {
        .results = { AUDIO_OUTPUT_SINK_OK, AUDIO_OUTPUT_SINK_OK },
        .written = { 1u, 1u },
    };
    assert(audio_output_sink_write_all(fake_write, &exhausted, data, sizeof(data),
                                       3u, 2u, &stats) == AUDIO_OUTPUT_SINK_ERROR);
    assert(exhausted.calls == 2u);
    assert(stats.failed_blocks == 2u);
}

int main(void)
{
    test_complete_write_uses_one_bounded_call();
    test_short_write_continues_only_with_unwritten_suffix();
    test_timeout_and_error_are_bounded_and_counted();
    test_zero_progress_and_retry_exhaustion_fail();
    puts("audio_output_sink tests passed");
    return 0;
}
