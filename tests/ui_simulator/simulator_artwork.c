#include "ui_artwork.h"
const uint16_t *ui_artwork_get_identity(uint32_t key,const media_persistent_id_t *id,ui_artwork_size_t size)
{(void)id;return ui_artwork_get(key,size);}
#include "ui_artwork_thumb.h"
#include "artwork_fixture.h"

#include <string.h>

static ui_artwork_thumb_work_t s_work;
static ui_artwork_thumb_t s_quad;
static ui_artwork_thumb_t s_gray;
static bool s_ready;

const uint16_t *ui_artwork_get(uint32_t track_key, ui_artwork_size_t size)
{
    if (!s_ready) {
        bool q = ui_artwork_thumb_decode(ART_FIXTURE_QUAD,
                                         sizeof ART_FIXTURE_QUAD,
                                         &s_work, &s_quad);
        bool g = ui_artwork_thumb_decode(ART_FIXTURE_GRAY,
                                         sizeof ART_FIXTURE_GRAY,
                                         &s_work, &s_gray);
        if (!q || !g) return NULL;
        s_ready = true;
    }
    const ui_artwork_thumb_t *art = track_key == 1001u ? &s_quad :
                                    track_key == 1003u ? &s_gray : NULL;
    if (!art) return NULL;
    return size == UI_ARTWORK_DECK ? art->deck : art->row;
}

void ui_artwork_begin_page(void) {}
bool ui_artwork_poll(void) { return false; }
void ui_artwork_set_paused(bool paused) { (void)paused; }
void ui_artwork_forget(uint32_t track_key) { (void)track_key; }
void ui_artwork_take_stats(ui_artwork_stats_t *out)
{
    if (out) memset(out, 0, sizeof(*out));
}
