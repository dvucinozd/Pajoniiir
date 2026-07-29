#include "ui_event_counter.h"

#include <stdio.h>

static int s_failures;
static unsigned s_checks;

#define CHECK(expr) do {                                                     \
    s_checks++;                                                              \
    if (!(expr)) {                                                           \
        printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);               \
        s_failures++;                                                        \
    }                                                                        \
} while (0)

static void test_request_is_durable_until_sampled_generation_is_applied(void)
{
    puts("== request is durable until sampled generation is applied ==");
    ui_event_counter_t counter = {0};
    ui_event_counter_reset(&counter);
    uint32_t applied = 0u;

    CHECK(ui_event_counter_sample(&counter) == 0u);
    CHECK(!ui_event_counter_pending(
        ui_event_counter_sample(&counter), applied));
    CHECK(ui_event_counter_request(&counter) == 1u);
    uint32_t sampled = ui_event_counter_sample(&counter);
    CHECK(sampled == 1u);
    CHECK(ui_event_counter_pending(sampled, applied));
    applied = sampled;
    CHECK(!ui_event_counter_pending(
        ui_event_counter_sample(&counter), applied));
}

static void test_request_arriving_during_processing_is_not_cleared(void)
{
    puts("== request during processing survives old completion ==");
    ui_event_counter_t counter = {0};
    uint32_t applied = 0u;
    CHECK(ui_event_counter_request(&counter) == 1u);
    uint32_t sampled = ui_event_counter_sample(&counter);
    CHECK(sampled == 1u);

    CHECK(ui_event_counter_request(&counter) == 2u);
    applied = sampled;
    CHECK(ui_event_counter_sample(&counter) == 2u);
    CHECK(ui_event_counter_pending(
        ui_event_counter_sample(&counter), applied));
    sampled = ui_event_counter_sample(&counter);
    applied = sampled;
    CHECK(!ui_event_counter_pending(
        ui_event_counter_sample(&counter), applied));
}

static void test_burst_coalesces_without_losing_latest_generation(void)
{
    puts("== burst coalesces to latest generation ==");
    ui_event_counter_t counter = {0};
    uint32_t applied = 0u;
    for (uint32_t expected = 1u; expected <= 100u; ++expected) {
        CHECK(ui_event_counter_request(&counter) == expected);
    }
    uint32_t sampled = ui_event_counter_sample(&counter);
    CHECK(sampled == 100u);
    CHECK(ui_event_counter_pending(sampled, applied));
    applied = sampled;
    CHECK(!ui_event_counter_pending(
        ui_event_counter_sample(&counter), applied));
}

static void test_independent_refresh_and_remove_counters_do_not_alias(void)
{
    puts("== refresh and remove counters remain independent ==");
    ui_event_counter_t refresh = {0};
    ui_event_counter_t remove = {0};
    CHECK(ui_event_counter_request(&refresh) == 1u);
    CHECK(ui_event_counter_request(&refresh) == 2u);
    CHECK(ui_event_counter_request(&remove) == 1u);
    CHECK(ui_event_counter_sample(&refresh) == 2u);
    CHECK(ui_event_counter_sample(&remove) == 1u);
    CHECK(ui_event_counter_pending(
        ui_event_counter_sample(&refresh), 1u));
    CHECK(!ui_event_counter_pending(
        ui_event_counter_sample(&remove), 1u));
}

static void test_null_guards(void)
{
    puts("== null guards ==");
    ui_event_counter_reset(NULL);
    CHECK(ui_event_counter_request(NULL) == 0u);
    CHECK(ui_event_counter_sample(NULL) == 0u);
}

int main(void)
{
    test_request_is_durable_until_sampled_generation_is_applied();
    test_request_arriving_during_processing_is_not_cleared();
    test_burst_coalesces_without_losing_latest_generation();
    test_independent_refresh_and_remove_counters_do_not_alias();
    test_null_guards();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("ui_event_counter tests passed");
        return 0;
    }
    printf("ui_event_counter tests FAILED (%d)\n", s_failures);
    return 1;
}
