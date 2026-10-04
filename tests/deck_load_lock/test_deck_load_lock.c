#include "deck_load_lock.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(deck_load_lock_check(true, false) == DECK_LOAD_LOCK_ALLOW_STOPPED);
    assert(deck_load_lock_check(true, true) == DECK_LOAD_LOCK_REFUSE_PLAYING);
    assert(deck_load_lock_check(false, true) == DECK_LOAD_LOCK_ALLOW_OFF);
    assert(deck_load_lock_allows(DECK_LOAD_LOCK_ALLOW_STOPPED));
    assert(deck_load_lock_allows(DECK_LOAD_LOCK_ALLOW_OFF));
    assert(!deck_load_lock_allows(DECK_LOAD_LOCK_REFUSE_PLAYING));
    assert(deck_load_lock_verdict_name(DECK_LOAD_LOCK_REFUSE_PLAYING)[0] == 'p');
    puts("deck_load_lock: all tests passed");
    return 0;
}
