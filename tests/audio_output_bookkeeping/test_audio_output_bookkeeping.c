#include "audio_output_bookkeeping.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    audio_output_bookkeeping_t state;
    audio_output_bookkeeping_reset(&state);

    audio_output_bookkeeping_note(&state, 0u, 1u, 256u);
    audio_output_bookkeeping_note(&state, 0u, 1u, 128u);
    assert(audio_output_bookkeeping_take(&state, 0u, 1u) == 384u);
    assert(audio_output_bookkeeping_take(&state, 0u, 1u) == 0u);

    /* A seek/load epoch invalidates frames accepted on the old playhead. */
    audio_output_bookkeeping_note(&state, 1u, 7u, 512u);
    assert(audio_output_bookkeeping_take(&state, 1u, 8u) == 0u);
    audio_output_bookkeeping_note(&state, 1u, 8u, 64u);
    assert(audio_output_bookkeeping_take(&state, 1u, 8u) == 64u);

    /* Noting a newer epoch also drops deferred frames immediately. */
    audio_output_bookkeeping_note(&state, 0u, 3u, 100u);
    audio_output_bookkeeping_note(&state, 0u, 4u, 25u);
    assert(audio_output_bookkeeping_take(&state, 0u, 4u) == 25u);

    audio_output_bookkeeping_note(NULL, 0u, 1u, 1u);
    audio_output_bookkeeping_note(&state, 2u, 1u, 1u);
    assert(audio_output_bookkeeping_take(NULL, 0u, 1u) == 0u);
    assert(audio_output_bookkeeping_take(&state, 2u, 1u) == 0u);

    puts("audio_output_bookkeeping tests passed");
    return 0;
}
