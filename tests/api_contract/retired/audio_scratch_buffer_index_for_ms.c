#include "audio_scratch_buffer.h"
size_t use(audio_scratch_buffer_t *b, uint32_t ms);
size_t use(audio_scratch_buffer_t *b, uint32_t ms) { return audio_scratch_buffer_index_for_ms(b, ms); }
