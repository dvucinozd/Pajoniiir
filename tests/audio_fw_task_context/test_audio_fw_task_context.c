#include "audio_fw_task_context.h"

#include <assert.h>
#include <stdio.h>

static void test_reset_clears_context(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    int engine;
    int pcm_ring;
    int resampler;
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(1, 0, true);
    audio_fw_task_context_t ctx = {
        .deck = 1,
        .preload = &preload,
        .runtime = &runtime,
        .engine = &engine,
        .pcm_ring = &pcm_ring,
        .resampler = &resampler,
        .task_plan = plan,
    };

    audio_fw_task_context_reset(&ctx);

    assert(ctx.deck == 0);
    assert(ctx.preload == NULL);
    assert(ctx.runtime == NULL);
    assert(ctx.engine == NULL);
    assert(ctx.pcm_ring == NULL);
    assert(ctx.resampler == NULL);
    assert(ctx.task_plan.expected_tasks == 0);
    assert(!audio_fw_task_context_is_bound(&ctx));
}

static void test_bind_keeps_explicit_deck_and_runtime_slots(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    int engine;
    int pcm_ring;
    int resampler;
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(1, 0, true);
    audio_fw_task_context_t ctx;
    audio_fw_runtime_reset(&runtime);
    audio_fw_runtime_begin_load(&runtime);
    audio_fw_task_context_reset(&ctx);

    audio_fw_task_context_bind(&ctx,
                               1,
                               &preload,
                               &runtime,
                               &engine,
                               &pcm_ring,
                               &resampler,
                               plan);

    assert(ctx.deck == 1);
    assert(ctx.preload == &preload);
    assert(ctx.runtime == &runtime);
    assert(ctx.engine == &engine);
    assert(ctx.pcm_ring == &pcm_ring);
    assert(ctx.resampler == &resampler);
    assert(ctx.task_plan.expected_tasks == 2);
    assert(!ctx.task_plan.start_output);
    assert(audio_fw_task_context_is_bound(&ctx));
    assert(audio_fw_task_context_is_current(&ctx));
    audio_fw_runtime_invalidate_session(&runtime);
    assert(!audio_fw_task_context_is_current(&ctx));
}

static void test_bind_requires_all_state_slots(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    int engine;
    int pcm_ring;
    int resampler;
    audio_fw_task_plan_t plan = audio_fw_task_plan_for_deck(0, 0, false);
    audio_fw_task_context_t ctx;

    audio_fw_task_context_bind(&ctx, 0, NULL, &runtime, &engine, &pcm_ring, &resampler, plan);
    assert(!audio_fw_task_context_is_bound(&ctx));

    audio_fw_task_context_bind(&ctx, 0, &preload, NULL, &engine, &pcm_ring, &resampler, plan);
    assert(!audio_fw_task_context_is_bound(&ctx));

    audio_fw_task_context_bind(&ctx, 0, &preload, &runtime, NULL, &pcm_ring, &resampler, plan);
    assert(!audio_fw_task_context_is_bound(&ctx));

    audio_fw_task_context_bind(&ctx, 0, &preload, &runtime, &engine, NULL, &resampler, plan);
    assert(!audio_fw_task_context_is_bound(&ctx));

    audio_fw_task_context_bind(&ctx, 0, &preload, &runtime, &engine, &pcm_ring, NULL, plan);
    assert(!audio_fw_task_context_is_bound(&ctx));
}

int main(void)
{
    test_reset_clears_context();
    test_bind_keeps_explicit_deck_and_runtime_slots();
    test_bind_requires_all_state_slots();
    puts("audio_fw_task_context tests passed");
    return 0;
}
