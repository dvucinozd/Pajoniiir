#include <stdint.h>

#include "esp_attr.h"
#include "esp_idf_version.h"

/*
 * ESP-IDF 5.5 registers ESP32-P4 TCM (0x30100000..0x30102000) as
 * MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT heap. FreeRTOS dynamic allocation can
 * therefore receive TCM for idle/timer task TCB or stack memory, but the later
 * stack/TCB validator rejects that range during scheduler startup.
 *
 * The workaround is intentionally limited to ESP-IDF 5.x. ESP-IDF 6 has a
 * different linker/heap layout and reusing a size derived from 5.5.4 could
 * reserve IDF-owned TCM or create an orphan-section/link overflow.
 */
#if ESP_IDF_VERSION < ESP_IDF_VERSION_VAL(6, 0, 0)

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

#else

void p4_tcm_heap_guard_keep(void)
{
    /* No IDF 5.x TCM heap reservation on ESP-IDF 6. */
}

#endif
