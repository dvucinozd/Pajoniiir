#include "audio_fw_task_context.h"

#include <assert.h>
#include <stdio.h>

static void test_reset_clears_context(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    audio_fw_task_context_t ctx = {
        .deck = 1,
        .preload = &preload,
        .runtime = &runtime,
    };

    audio_fw_task_context_reset(&ctx);

    assert(ctx.deck == 0);
    assert(ctx.preload == NULL);
    assert(ctx.runtime == NULL);
    assert(!audio_fw_task_context_is_bound(&ctx));
}

static void test_bind_keeps_explicit_deck_and_state_slots(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    audio_fw_task_context_t ctx;
    audio_fw_task_context_reset(&ctx);

    audio_fw_task_context_bind(&ctx, 1, &preload, &runtime);

    assert(ctx.deck == 1);
    assert(ctx.preload == &preload);
    assert(ctx.runtime == &runtime);
    assert(audio_fw_task_context_is_bound(&ctx));
}

static void test_bind_requires_all_state_slots(void)
{
    audio_fw_preload_t preload;
    audio_fw_runtime_t runtime;
    audio_fw_task_context_t ctx;

    audio_fw_task_context_bind(&ctx, 0, NULL, &runtime);
    assert(!audio_fw_task_context_is_bound(&ctx));

    audio_fw_task_context_bind(&ctx, 0, &preload, NULL);
    assert(!audio_fw_task_context_is_bound(&ctx));
}

int main(void)
{
    test_reset_clears_context();
    test_bind_keeps_explicit_deck_and_state_slots();
    test_bind_requires_all_state_slots();
    puts("audio_fw_task_context tests passed");
    return 0;
}
