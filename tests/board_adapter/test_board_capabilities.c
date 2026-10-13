#include "board_capabilities.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
int main(void)
{
    const board_capabilities_t *a = board_capabilities_for(BOARD_JC4880);
    const board_capabilities_t *b = board_capabilities_for(BOARD_JC1060);
    const board_capabilities_t *m = board_capabilities_for(BOARD_M3);
    assert(m && strcmp(m->project, "main-deck-m3") == 0);
    assert(m->panel_width == 800 && m->panel_height == 480 && m->panel_rotation == 0);
    assert(m->scanout_bytes_per_pixel == 3 && m->waveform_first && m->waveform_top_to_bottom);
    assert(m->display_framebuffers == 2);
    assert(a->display_framebuffers == 1 && b->display_framebuffers == 1);
    assert(m->fixed_output_sample_rate == 48000 && m->pcm5102a && m->wifi && !m->ethernet);
#if defined(CONFIG_BSP_M3_HS_CONTROLLER) && CONFIG_BSP_M3_HS_CONTROLLER
    assert(m->storage_root == 1 && m->controller_root == 0 && m->fs_phy_index == 0);
#else
    assert(m->storage_root == 0 && m->controller_root == 1 && m->fs_phy_index == 0);
#endif
    assert(m->hosted_release_on_stop && m->keylock_dense_correlation);
    assert(m->wifi_apsta && !m->wifi_wpa3_transition);
    const board_capabilities_t *j = board_capabilities_for(BOARD_JC8012P4A1);
    assert(j && strcmp(j->project, "main-deck-jc8012p4a1") == 0);
    assert(strcmp(board_id_name(BOARD_JC8012P4A1), "jc8012p4a1") == 0);
    assert(j->display_width == 1280 && j->display_height == 800);
    assert(j->panel_width == 800 && j->panel_height == 1280 && j->panel_rotation == 270);
    assert(!j->ethernet && j->wifi && !j->pcm5102a && !j->sd_internal_bounce);
    assert(j->wifi_apsta && !j->wifi_wpa3_transition);
    assert(j->storage_root != j->controller_root && j->display_framebuffers == 1);
    assert(strcmp(j->project, a->project) && strcmp(j->project, b->project) && strcmp(j->project, m->project));
    assert(strcmp(m->project, a->project) && strcmp(m->project, b->project));
    assert(a && b && !board_capabilities_for((board_id_t)99));
    assert(a->display_width == 800 && a->display_height == 480);
    assert(a->panel_width == 480 && a->panel_height == 800 && a->panel_rotation == 270);
    assert(!a->ethernet && a->wifi && a->pcm5102a && !a->sd_internal_bounce);
    assert(!a->wifi_apsta && a->wifi_wpa3_transition);
    assert(b->display_width == 1024 && b->display_height == 600);
    assert(b->panel_rotation == 0 && b->ethernet && !b->wifi && !b->pcm5102a);
    assert(b->sd_internal_bounce);
    assert(strcmp(a->project, b->project) != 0);
    for (unsigned i = 0; i < 2; ++i) {
        const board_capabilities_t *c = board_capabilities_for((board_id_t)i);
        assert(c->storage_root != c->controller_root);
        assert(c->storage_root == i && c->controller_root == 1 - i && c->fs_phy_index == 0);
    }
#if defined(CONFIG_PAJONIIIR_BOARD_JC8012P4A1)
    assert(board_capabilities_get() == j);
#elif defined(CONFIG_PAJONIIIR_BOARD_M3)
    assert(board_capabilities_get() == m);
#elif defined(CONFIG_PAJONIIIR_BOARD_JC1060)
    assert(board_capabilities_get() == b);
#else
    assert(board_capabilities_get() == a);
#endif
    puts("board capabilities geometry, identity, sinks and USB roles PASS");
    return 0;
}
