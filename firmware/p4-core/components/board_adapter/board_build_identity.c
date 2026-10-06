#include "board_build_identity.h"
#include "board_build_identity_generated.h"

const char *board_build_source_sha(void) { return PAJONIIIR_SOURCE_SHA; }
bool board_build_source_dirty(void) { return PAJONIIIR_SOURCE_DIRTY != 0; }
