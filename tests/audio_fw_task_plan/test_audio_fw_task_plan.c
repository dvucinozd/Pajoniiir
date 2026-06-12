#include "audio_fw_task_plan.h"

#include <assert.h>
#include <stdio.h>

static void test_compat_deck_starts_full_output_task_set(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(0, 0, false);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(plan.start_output);
    assert(plan.codec_owner);
    assert(plan.transport_supported);
    assert(plan.expected_tasks == 3);
}

static void test_non_compat_deck_starts_producer_only_task_set(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(1, 0, true);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(!plan.start_output);
    assert(!plan.codec_owner);
    assert(plan.transport_supported);
    assert(plan.expected_tasks == 2);
}

static void test_non_compat_deck_starts_output_when_none_is_running(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(1, 0, false);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(plan.start_output);
    assert(plan.codec_owner);
    assert(plan.transport_supported);
    assert(plan.expected_tasks == 3);
}

static void test_compat_deck_becomes_producer_when_output_already_runs(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(0, 0, true);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(!plan.start_output);
    assert(!plan.codec_owner);
    assert(plan.transport_supported);
    assert(plan.expected_tasks == 2);
}

int main(void)
{
    test_compat_deck_starts_full_output_task_set();
    test_non_compat_deck_starts_producer_only_task_set();
    test_non_compat_deck_starts_output_when_none_is_running();
    test_compat_deck_becomes_producer_when_output_already_runs();
    puts("audio_fw_task_plan tests passed");
    return 0;
}
