/* Retired simulator alias. The simulator uses the production selected-track API
 * so the shared UI source compiles against one set of names. */
#include "library.h"
void use(int index, unsigned char deck);
void use(int index, unsigned char deck) { mock_library_load_track_to_deck(index, deck); }
