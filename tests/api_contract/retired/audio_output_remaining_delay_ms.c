/* Retired: always returned zero. The i2s_channel_write call paces the output
 * loop; a software delay on top of it was dead code. */
#include "audio_output_timing.h"
uint32_t use(uint32_t rate, uint32_t elapsed);
uint32_t use(uint32_t rate, uint32_t elapsed) { return audio_output_remaining_delay_ms(rate, elapsed); }
