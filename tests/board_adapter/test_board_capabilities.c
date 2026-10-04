#include "board_capabilities.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    const board_capabilities_t *a = board_capabilities_for(BOARD_JC4880);
    const board_capabilities_t *b = board_capabilities_for(BOARD_JC1060);
    assert(a && b && !board_capabilities_for((board_id_t)99));
    assert(a->display_width == 800 && a->display_height == 480);
    assert(a->panel_width == 480 && a->panel_height == 800 && a->panel_rotation == 270);
    assert(!a->ethernet && a->wifi && a->pcm5102a && !a->sd_internal_bounce);
    assert(b->display_width == 1024 && b->display_height == 600);
    assert(b->panel_rotation == 0 && b->ethernet && !b->wifi && !b->pcm5102a);
    assert(b->sd_internal_bounce);
    assert(strcmp(a->project, b->project) != 0);
    for (unsigned i = 0; i < 2; ++i) {
        const board_capabilities_t *c = board_capabilities_for((board_id_t)i);
        assert(c->storage_root != c->controller_root);
        assert(c->storage_root == i && c->controller_root == 1 - i && c->fs_phy_index == 0);
    }
#ifdef CONFIG_PAJONIIIR_BOARD_JC1060
    assert(board_capabilities_get() == b);
#else
    assert(board_capabilities_get() == a);
#endif
    puts("board capabilities geometry, identity, sinks and USB roles PASS");
    return 0;
}
