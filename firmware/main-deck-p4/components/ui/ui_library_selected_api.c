/*
 * Firmware-only API bridge. The simulator continues to compile ui_library.c
 * directly against its temporary mock_library_* symbols, while the production
 * component resolves the same source calls to the correctly named library API.
 */
#define mock_library_load_track_to_deck      library_set_selected_track_index
#define mock_library_get_current_track_index library_selected_track_index
#include "ui_library.c"
#undef mock_library_get_current_track_index
#undef mock_library_load_track_to_deck
