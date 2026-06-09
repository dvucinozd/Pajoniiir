#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool start_loader;
    bool start_decode;
    bool start_output;
    bool codec_owner;
    bool transport_supported;
    int expected_tasks;
} audio_fw_task_plan_t;

audio_fw_task_plan_t audio_fw_task_plan_for_deck(uint8_t deck, uint8_t compat_deck);
