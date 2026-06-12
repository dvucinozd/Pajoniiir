#include "audio_fw_task_plan.h"

audio_fw_task_plan_t audio_fw_task_plan_for_deck(uint8_t deck,
                                                 uint8_t compat_deck,
                                                 bool output_running)
{
    (void)deck;
    (void)compat_deck;
    bool output_owner = !output_running;
    return (audio_fw_task_plan_t) {
        .start_loader = true,
        .start_decode = true,
        .start_output = output_owner,
        .codec_owner = output_owner,
        .transport_supported = true,
        .expected_tasks = output_owner ? 3 : 2,
    };
}
