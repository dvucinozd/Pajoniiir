/* Ensure the locked matcher is declared before the descriptor worker uses it. */
#include <stdint.h>
static int cpm_on_descriptor_locked(uint16_t vid, uint16_t pid);
#include "controller_profile_manager.c"
