#include <stdint.h>

#include "esp_attr.h"

/*
 * ESP-IDF 5.5 registers ESP32-P4 TCM (0x30100000..0x30102000) as
 * MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT heap. FreeRTOS dynamic allocation can
 * therefore receive TCM for idle/timer task TCB or stack memory, but the later
 * stack/TCB validator rejects that range during scheduler startup.
 *
 * The P4 firmware does not rely on TCM heap. Reserve nearly all unused TCM in
 * the image so heap initialization excludes it from malloc-capable regions.
 */
__attribute__((used)) static TCM_DRAM_ATTR volatile uint8_t s_tcm_heap_guard[0x1f40];

void p4_tcm_heap_guard_keep(void)
{
    s_tcm_heap_guard[0] = (uint8_t)(s_tcm_heap_guard[0] + 0U);
}
