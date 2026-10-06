/* P4 artwork cache: Rekordbox cover thumbnails for Library rows and deck UI.
 *
 * One low-priority worker (core 1, below LVGL, never an audio task) reads the
 * JPEG export.pdb links to a track and decodes it
 * (ui_artwork_thumb), at most one every UI_ARTWORK_GAP_MS (ui_artwork.c). The
 * results land in a fixed PSRAM cache of UI_ARTWORK_SLOTS entries, LRU,
 * allocated once on first use; a catalog rebuild (library generation) makes
 * every entry stale. Every function runs on the LVGL task; pixels returned by
 * ui_artwork_get() stay valid until the next ui_artwork_get()/ui_artwork_poll()
 * call, so callers copy them at once. */
#ifndef UI_ARTWORK_H
#define UI_ARTWORK_H

#include <stdbool.h>
#include <stdint.h>
#include "media_identity.h"
#include "ui_artwork_thumb.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UI_ARTWORK_SLOTS 24          /* 8 rows + 2 decks, and a page back */

typedef enum { UI_ARTWORK_ROW, UI_ARTWORK_DECK } ui_artwork_size_t;

/* Cached thumbnail of `track_key`, or NULL (no artwork, not decoded yet, or
 * no memory). A miss queues a decode bound to the current catalog generation. */
const uint16_t *ui_artwork_get(uint32_t track_key, ui_artwork_size_t size);
/* Full identity lookup for decks; runtime key collisions cannot return
 * another source's artwork. Publish is LVGL-only, decode is worker-only. */
const uint16_t *ui_artwork_get_identity(uint32_t key,const media_persistent_id_t *id,ui_artwork_size_t size);
ui_artwork_thumb_t *ui_artwork_decode_sd(const char *path);
void ui_artwork_publish_identity(uint32_t key,const media_persistent_id_t *id,const ui_artwork_thumb_t *thumb);
/* The Library page changed: row decodes still queued for older pages are
 * dropped unread. */
void ui_artwork_begin_page(void);
/* Once per frame: takes at most one finished decode into the cache. true =
 * re-ask for what is on screen (a thumbnail landed or a request can be
 * retried). */
bool ui_artwork_poll(void);
/* Hold USB reads while a track load owns the stick. */
void ui_artwork_set_paused(bool paused);
/* Drop what is cached for `track_key`, so the next get reads it again. */
void ui_artwork_forget(uint32_t track_key);

/* Diagnostics since the last take (LVGL task). */
typedef struct {
    uint32_t queued;          /* decode requests sent to the worker */
    uint32_t queue_full;      /* misses retried later */
    uint32_t decoded;         /* thumbnails landed in the cache */
    uint32_t none;            /* no artwork / unreadable / not decodable */
    uint32_t skipped;         /* row requests dropped after a page change */
    uint32_t read_us_max;     /* worker: file read, USB gate waits included */
    uint32_t decode_us_max;   /* worker: JPEG decode + downscale (wall time, prio 1) */
    uint32_t poll_us_max;     /* ui_artwork_poll() on the LVGL task */
} ui_artwork_stats_t;
void ui_artwork_take_stats(ui_artwork_stats_t *out);

#ifdef __cplusplus
}
#endif

#endif
