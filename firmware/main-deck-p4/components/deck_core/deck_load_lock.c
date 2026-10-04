/* Imported from kayrozen/Pajoniiir 428b97dd (MIT).
 * Copyright (c) 2024 The Pajoniiir Contributors. See repository LICENSE. */
#include "deck_load_lock.h"

deck_load_lock_verdict_t deck_load_lock_check(bool lock_on, bool playing)
{
    if (!lock_on) {
        return DECK_LOAD_LOCK_ALLOW_OFF;
    }
    return playing ? DECK_LOAD_LOCK_REFUSE_PLAYING : DECK_LOAD_LOCK_ALLOW_STOPPED;
}

bool deck_load_lock_allows(deck_load_lock_verdict_t verdict)
{
    return verdict != DECK_LOAD_LOCK_REFUSE_PLAYING;
}

const char *deck_load_lock_verdict_name(deck_load_lock_verdict_t verdict)
{
    switch (verdict) {
    case DECK_LOAD_LOCK_ALLOW_OFF:      return "off";
    case DECK_LOAD_LOCK_ALLOW_STOPPED:  return "stopped";
    case DECK_LOAD_LOCK_REFUSE_PLAYING: return "playing";
    }
    return "?";
}
