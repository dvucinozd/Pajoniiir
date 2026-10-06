#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

typedef bool (*bsp_dsi_id_read_fn)(void *context, uint8_t reg, uint8_t *value);

// Identification only. Each callback selects one read-only register; abort on
// the first transport error or wrong prefix. A revision is accepted only when
// repeated along with the complete identity. This does not authorize init.
static inline bool bsp_dsi_id_probe(bsp_dsi_id_read_fn read_byte, void *context,
                                    uint8_t id[4])
{
    const uint8_t expected[] = {0xc1, 0x62, 0x11};
    if (!id) return false;
    memset(id, 0, 4);
    if (!read_byte) return false;
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (uint8_t reg = 0; reg < 4; ++reg) {
            uint8_t value = 0;
            if (!read_byte(context, reg, &value)) return false;
            if (pass == 0) id[reg] = value;
            if (reg < sizeof(expected) && value != expected[reg]) return false;
            if (pass == 1 && value != id[reg]) return false;
        }
    }
    return true;
}
