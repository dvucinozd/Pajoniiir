#include <stdint.h>

#include "esp_attr.h"
#include "esp_idf_version.h"

/*
 * ESP-IDF 5.5 registers ESP32-P4 TCM (0x30100000..0x30102000) as
 * MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT heap. FreeRTOS dynamic allocation can
 * therefore receive TCM for idle/timer task TCB or stack memory, but the later
 * stack/TCB validator rejects that range during scheduler startup.
 *
 * The P4 firmware does not rely on TCM heap. Reserve nearly all unused TCM in
 * the image so heap initialization excludes it from malloc-capable regions.
 *
 * ESP-IDF 5.5.4 added 216 bytes of IDF-owned TCM text/data compared with the
 * older 5.5 installation. Keep both supported development environments
 * linkable while still leaving too little free TCM to form a heap block.
 */
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 5, 4)
#define P4_TCM_HEAP_GUARD_SIZE 0x1e60U
#else
#define P4_TCM_HEAP_GUARD_SIZE 0x1f40U
#endif

__attribute__((used)) static TCM_DRAM_ATTR volatile uint8_t
    s_tcm_heap_guard[P4_TCM_HEAP_GUARD_SIZE];

void p4_tcm_heap_guard_keep(void)
{
    s_tcm_heap_guard[0] = (uint8_t)(s_tcm_heap_guard[0] + 0U);
}
