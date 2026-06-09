#include "audio_fw_task_plan.h"

audio_fw_task_plan_t audio_fw_task_plan_for_deck(uint8_t deck, uint8_t compat_deck)
{
    bool compat = deck == compat_deck;
    return (audio_fw_task_plan_t) {
        .start_loader = true,
        .start_decode = true,
        .start_output = compat,
        .codec_owner = compat,
        .transport_supported = true,
        .expected_tasks = compat ? 3 : 2,
    };
}
