#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "audio_load_validation_gate.h"
#include "media_io_gate.h"

static unsigned s_polls;

static void remove_media_on_third_poll(void)
{
    if (++s_polls == 3u) media_io_gate_set_available(false);
}

static audio_load_validation_gate_snapshot_t snapshot(void)
{
    audio_load_validation_gate_snapshot_t value = {0};
    audio_load_validation_gate_snapshot(&value);
    return value;
}

int main(void)
{
    assert(media_io_gate_init() == ESP_OK);
    audio_load_validation_gate_test_reset();
    assert(audio_load_validation_gate_arm(2u, 5000u) == ESP_ERR_INVALID_ARG);
    assert(audio_load_validation_gate_arm(0u, 4999u) == ESP_ERR_INVALID_ARG);
    assert(audio_load_validation_gate_arm(0u, 60001u) == ESP_ERR_INVALID_ARG);

    media_io_gate_set_available(true);
    assert(audio_load_validation_gate_arm(0u, 5000u) == ESP_OK);
    assert(audio_load_validation_gate_arm(1u, 5000u) == ESP_ERR_INVALID_STATE);
    assert(audio_load_validation_gate_checkpoint(1u));
    assert(snapshot().state == AUDIO_LOAD_VALIDATION_GATE_ARMED);
    audio_load_validation_gate_cancel();
    assert(audio_load_validation_gate_checkpoint(0u));
    assert(snapshot().state == AUDIO_LOAD_VALIDATION_GATE_CANCELED);

    assert(audio_load_validation_gate_arm(1u, 5000u) == ESP_OK);
    s_polls = 0u;
    audio_load_validation_gate_test_set_poll_hook(remove_media_on_third_poll);
    assert(!audio_load_validation_gate_checkpoint(1u));
    audio_load_validation_gate_snapshot_t removed = snapshot();
    assert(removed.state == AUDIO_LOAD_VALIDATION_GATE_MEDIA_REMOVED);
    assert(removed.sequence == 2u);
    assert(removed.timeout_ms == 5000u);
    assert(removed.deck == 1u);
    assert(strcmp(audio_load_validation_gate_state_name(removed.state),
                  "media_removed") == 0);

    puts("audio load validation gate tests passed");
    return 0;
}
