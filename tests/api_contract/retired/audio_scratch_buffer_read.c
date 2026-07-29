#include "audio_scratch_buffer.h"
void use(audio_scratch_buffer_t *b, size_t i, int16_t *l, int16_t *r);
void use(audio_scratch_buffer_t *b, size_t i, int16_t *l, int16_t *r) { audio_scratch_buffer_read(b, i, l, r); }
