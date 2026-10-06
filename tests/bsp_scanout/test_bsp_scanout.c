#include "bsp_scanout.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    unsigned cases = 0;
    assert(BSP_SCANOUT_BYTES_PER_PIXEL == 3);
    for (unsigned c = 0; c <= UINT16_MAX; ++c) {
        uint8_t guarded[5] = {0xa5, 0, 0, 0, 0x5a};
        bsp_scanout_store_rgb565(guarded + 1, (uint16_t)c);
        assert(guarded[0] == 0xa5 && guarded[4] == 0x5a);
        // Independent quotient/remainder expression for bit replication.
        assert(guarded[1] == (c % 32) * 8 + (c % 32) / 4);
        assert(guarded[2] == ((c / 32) % 64) * 4 + ((c / 32) % 64) / 16);
        assert(guarded[3] == (c / 2048) * 8 + (c / 2048) / 4);
        ++cases;
    }
    enum { W = 7, H = 5, BYTES = W * H * BSP_SCANOUT_BYTES_PER_PIXEL };
    uint8_t memory[BYTES + 2];
    for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x)
    for (unsigned h = 1; h <= H - y; ++h) for (unsigned w = 1; w <= W - x; ++w) {
        memset(memory, 0xa5, sizeof(memory));
        assert(bsp_scanout_fill_rect_rgb565(memory + 1, BYTES, W, H, x, y, w, h, 0xf800));
        assert(memory[0] == 0xa5 && memory[BYTES + 1] == 0xa5);
        for (unsigned yy = 0; yy < H; ++yy) for (unsigned xx = 0; xx < W; ++xx) {
            const uint8_t *p = memory + 1 + (yy * W + xx) * 3;
            if (xx >= x && xx < x + w && yy >= y && yy < y + h) {
                assert(p[0] == 0 && p[1] == 0 && p[2] == 255);
            } else assert(p[0] == 0xa5 && p[1] == 0xa5 && p[2] == 0xa5);
        }
        ++cases;
    }
    const unsigned invalid[][6] = {
        {0,H,0,0,1,1}, {W,0,0,0,1,1}, {W,H,0,0,0,1}, {W,H,0,0,1,0},
        {W,H,W,0,1,1}, {W,H,0,H,1,1}, {W,H,1,0,W,1}, {W,H,0,1,1,H},
        {W,H,UINT_MAX,0,1,1}, {W,H,0,0,UINT_MAX,1},
        {UINT_MAX,UINT_MAX,0,0,1,1}
    };
    for (unsigned i = 0; i < sizeof(invalid)/sizeof(invalid[0]); ++i) {
        memset(memory, 0xa5, sizeof(memory));
        const unsigned *v = invalid[i];
        assert(!bsp_scanout_fill_rect_rgb565(memory + 1, BYTES, v[0], v[1], v[2], v[3], v[4], v[5], 0));
        for (unsigned j = 0; j < sizeof(memory); ++j) assert(memory[j] == 0xa5);
        ++cases;
    }
    assert(!bsp_scanout_fill_rect_rgb565(NULL, BYTES, W,H,0,0,1,1,0)); ++cases;
    assert(!bsp_scanout_fill_rect_rgb565(memory+1, BYTES-1, W,H,0,0,1,1,0)); ++cases;
    printf("TESTS_RUN=%u\nbsp_scanout tests passed\n", cases);
    return 0;
}
