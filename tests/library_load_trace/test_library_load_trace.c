#include "library_load_trace.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    library_load_trace_test_reset_all();
    library_load_trace_boot_init();
    library_load_trace_mark(LIBRARY_LOAD_PHASE_USB_EXT, 5u);

    bool previous_valid = false;
    bool current_valid = false;
    library_load_trace_record_t previous;
    library_load_trace_record_t current;
    library_load_trace_snapshot(&previous_valid, &previous,
                                &current_valid, &current);
    assert(!previous_valid);
    assert(current_valid);
    assert(current.boot_id == 1u);
    assert(current.phase == LIBRARY_LOAD_PHASE_USB_EXT);
    assert(current.track_key == 5u);

    library_load_trace_test_reboot();
    library_load_trace_boot_init();
    library_load_trace_snapshot(&previous_valid, &previous,
                                &current_valid, &current);
    assert(previous_valid);
    assert(previous.phase == LIBRARY_LOAD_PHASE_USB_EXT);
    assert(previous.track_key == 5u);
    assert(current_valid);
    assert(current.boot_id == 2u);
    assert(current.phase == LIBRARY_LOAD_PHASE_IDLE);
    assert(current.track_key == 0u);
    assert(library_load_trace_phase_name(LIBRARY_LOAD_PHASE_CACHE_SD_READ));
    puts("library_load_trace tests passed");
    return 0;
}
