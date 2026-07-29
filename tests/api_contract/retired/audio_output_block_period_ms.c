/* Retired: the block period is expressed in microseconds now, and the
 * millisecond helper rounded to zero at every supported sample rate. */
#include "audio_output_timing.h"
uint32_t use(uint32_t rate);
uint32_t use(uint32_t rate) { return audio_output_block_period_ms(rate); }
