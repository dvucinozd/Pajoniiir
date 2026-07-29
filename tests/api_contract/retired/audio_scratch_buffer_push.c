/* Retired with the independent scratch PCM store: the canonical PSRAM timeline
 * owns capture now, and a second writer path was the source of the ownership
 * bug this API is gone to prevent. */
#include "audio_scratch_buffer.h"
void use(audio_scratch_buffer_t *b, int16_t l, int16_t r);
void use(audio_scratch_buffer_t *b, int16_t l, int16_t r) { audio_scratch_buffer_push(b, l, r); }
