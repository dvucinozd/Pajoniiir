#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "library_validation_gate.h"
#include "media_io_gate.h"

static unsigned s_polls;

static void remove_media_on_third_poll(void)
{
    if (++s_polls == 3u) media_io_gate_set_available(false);
}

static library_validation_gate_snapshot_t snapshot(void)
{
    library_validation_gate_snapshot_t value = {0};
    library_validation_gate_snapshot(&value);
    return value;
}

int main(void)
{
    assert(media_io_gate_init() == ESP_OK);
    library_validation_gate_test_reset();
    assert(library_validation_gate_arm(4999u) == ESP_ERR_INVALID_ARG);
    assert(library_validation_gate_arm(60001u) == ESP_ERR_INVALID_ARG);

    media_io_gate_set_available(true);
    assert(library_validation_gate_arm(5000u) == ESP_OK);
    assert(library_validation_gate_arm(5000u) == ESP_ERR_INVALID_STATE);
    library_validation_gate_cancel();
    assert(library_validation_gate_checkpoint());
    assert(snapshot().state == LIBRARY_VALIDATION_GATE_CANCELED);

    assert(library_validation_gate_arm(5000u) == ESP_OK);
    s_polls = 0u;
    library_validation_gate_test_set_poll_hook(remove_media_on_third_poll);
    assert(!library_validation_gate_checkpoint());
    library_validation_gate_snapshot_t removed = snapshot();
    assert(removed.state == LIBRARY_VALIDATION_GATE_MEDIA_REMOVED);
    assert(removed.sequence == 2u);
    assert(removed.timeout_ms == 5000u);
    assert(strcmp(library_validation_gate_state_name(removed.state),
                  "media_removed") == 0);

    puts("library validation gate tests passed");
    return 0;
}
