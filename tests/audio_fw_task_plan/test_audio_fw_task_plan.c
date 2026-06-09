#include "audio_fw_task_plan.h"

#include <assert.h>
#include <stdio.h>

static void test_compat_deck_starts_full_output_task_set(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(0, 0);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(plan.start_output);
    assert(plan.codec_owner);
    assert(plan.expected_tasks == 3);
}

static void test_non_compat_deck_starts_producer_only_task_set(void)
{
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(1, 0);

    assert(plan.start_loader);
    assert(plan.start_decode);
    assert(!plan.start_output);
    assert(!plan.codec_owner);
    assert(plan.expected_tasks == 2);
}

int main(void)
{
    test_compat_deck_starts_full_output_task_set();
    test_non_compat_deck_starts_producer_only_task_set();
    puts("audio_fw_task_plan tests passed");
    return 0;
}
