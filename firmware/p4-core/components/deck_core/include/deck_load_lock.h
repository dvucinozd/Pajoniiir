/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE.
 * Plain verdict model; the P4 integration enables load lock by default. */
#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DECK_LOAD_LOCK_ALLOW_OFF,       /* switch off: load at any time */
    DECK_LOAD_LOCK_ALLOW_STOPPED,   /* switch on, deck paused or stopped */
    DECK_LOAD_LOCK_REFUSE_PLAYING,  /* switch on, deck playing */
} deck_load_lock_verdict_t;

deck_load_lock_verdict_t deck_load_lock_check(bool lock_on, bool playing);
bool deck_load_lock_allows(deck_load_lock_verdict_t verdict);
/* "off", "stopped", "playing" for logs. */
const char *deck_load_lock_verdict_name(deck_load_lock_verdict_t verdict);

#ifdef __cplusplus
}
#endif
