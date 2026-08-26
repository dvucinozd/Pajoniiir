#include "audio_fw_runtime.h"
#include <assert.h>
#include <stdio.h>

static void test_reset_clears_task_lifecycle_state(void)
{
    audio_fw_runtime_t runtime = {
        .loader_task = (void *)0x1000,
        .decode_task = (void *)0x2000,
        .output_task = (void *)0x3000,
        .run = true,
        .tasks_started = 3,
        .codec_open = true,
    };

    audio_fw_runtime_reset(&runtime);

    assert(runtime.loader_task == NULL);
    assert(runtime.decode_task == NULL);
    assert(runtime.output_task == NULL);
    assert(!runtime.run);
    assert(runtime.tasks_started == 0);
    assert(!runtime.codec_open);
}

static void test_begin_load_starts_fresh_run(void)
{
    audio_fw_runtime_t runtime = {
        .loader_task = (void *)0x1000,
        .decode_task = (void *)0x2000,
        .output_task = (void *)0x3000,
        .run = false,
        .tasks_started = 2,
        .codec_open = true,
    };

    audio_fw_runtime_begin_load(&runtime);

    assert(runtime.loader_task == NULL);
    assert(runtime.decode_task == NULL);
    assert(runtime.output_task == NULL);
    assert(runtime.run);
    assert(runtime.tasks_started == 0);
    assert(!runtime.codec_open);
    assert(runtime.session_generation == 1u);
}

static void test_mark_task_started_counts_started_tasks(void)
{
    audio_fw_runtime_t runtime;
    audio_fw_runtime_reset(&runtime);

    audio_fw_runtime_mark_task_started(&runtime);
    audio_fw_runtime_mark_task_started(&runtime);

    assert(runtime.tasks_started == 2);
}

static void test_mark_stopped_clears_run_and_owned_handles(void)
{
    audio_fw_runtime_t runtime = {
        .loader_task = (void *)0x1000,
        .decode_task = (void *)0x2000,
        .output_task = (void *)0x3000,
        .run = true,
        .tasks_started = 3,
        .codec_open = true,
    };

    audio_fw_runtime_mark_stopped(&runtime);

    assert(runtime.loader_task == NULL);
    assert(runtime.decode_task == NULL);
    assert(runtime.output_task == NULL);
    assert(!runtime.run);
    assert(runtime.tasks_started == 0);
    assert(!runtime.codec_open);
}

static void test_invalidate_changes_generation_and_closes_run_gate(void)
{
    audio_fw_runtime_t runtime;
    audio_fw_runtime_reset(&runtime);
    audio_fw_runtime_begin_load(&runtime);
    uint32_t active = runtime.session_generation;
    audio_fw_runtime_invalidate_session(&runtime);
    assert(runtime.session_generation != active);
    assert(!runtime.run);
}

int main(void)
{
    test_reset_clears_task_lifecycle_state();
    test_begin_load_starts_fresh_run();
    test_mark_task_started_counts_started_tasks();
    test_mark_stopped_clears_run_and_owned_handles();
    test_invalidate_changes_generation_and_closes_run_gate();
    puts("audio_fw_runtime tests passed");
    return 0;
}
