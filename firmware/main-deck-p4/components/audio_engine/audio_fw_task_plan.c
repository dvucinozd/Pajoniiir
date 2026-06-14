#include "audio_fw_task_plan.h"

audio_fw_task_plan_t audio_fw_task_plan_for_deck(uint8_t deck,
                                                 uint8_t compat_deck,
                                                 bool output_running)
{
    (void)deck;
    (void)compat_deck;
    (void)output_running;
    return (audio_fw_task_plan_t) {
        .start_loader = true,
        .start_decode = true,
        .start_output = false,
        .codec_owner = false,
        .transport_supported = true,
        .expected_tasks = 2,
    };
}
