/*
 * Firmware-only selected-track API bridge.
 *
 * ui.c is shared with the WIN32 simulator, whose mock layer still uses the
 * legacy getter name. The P4 firmware must bind directly to the production
 * library symbol without exporting a mock_library_* compatibility function.
 */
#define mock_library_get_current_track_index library_selected_track_index
#include "ui.c"
#undef mock_library_get_current_track_index
