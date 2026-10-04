#pragma once
#include "media_identity.h"
#include <stdio.h>

#define DJ_LINK_CACHE_BUDGET (UINT64_C(1) << 30)
#define DJ_LINK_CACHE_RESERVE (UINT64_C(64) << 20)
#define DJ_LINK_PDB_LIMIT (UINT64_C(64) << 20)
#define DJ_LINK_CACHE_PATH 272

/* Locator fields (IP/player/track id) deliberately do not occur here. A
 * trustworthy export identity may be supplied by future transports. Today
 * NFSv2 has no volume UUID: each load remounts/re-downloads, and the complete
 * export/audio digests confirm the persistent identity before publication. */
bool dj_link_normalize_path(const char *input, char out[256]);
bool dj_link_content_identity(const uint8_t export_digest[32],
    const uint8_t audio_digest[32], const char *path, uint64_t size,
    uint64_t remote_mtime_us, media_persistent_id_t *out);

typedef struct {
    void *ctx;
    bool (*current)(void *ctx); /* cancellation / SD generation / source epoch */
    bool (*begin)(void *ctx);   /* bounded filesystem gate */
    void (*cleanup_begin)(void *ctx); /* required with begin; retire handles after cancel */
    void (*end)(void *ctx);
    bool (*free_bytes)(void *ctx, uint64_t *bytes);
} dj_link_cache_io_t;
typedef struct {
    char root[160];
    dj_link_cache_io_t io;
    media_persistent_id_t pins[2];
    media_persistent_id_t active;
    uint64_t budget;
} dj_link_cache_t;
typedef struct {
    dj_link_cache_t *cache;
    FILE *file;
    char part[DJ_LINK_CACHE_PATH];
    uint64_t expected, written;
    media_sha256_ctx_t hash;
    uint8_t digest[32];
    bool sealed;
} dj_link_cache_txn_t;

/* Single writer owns this object. Deck pins are captured at admission and
 * remain immutable until the whole transaction exits. */
bool dj_link_cache_init(dj_link_cache_t *c, const char *root,
    const dj_link_cache_io_t *io, const media_persistent_id_t pins[2]);
bool dj_link_cache_begin(dj_link_cache_t *c, dj_link_cache_txn_t *t,
    uint64_t expected, uint64_t limit);
bool dj_link_cache_write(dj_link_cache_txn_t *t, const void *bytes, size_t len);
bool dj_link_cache_seal(dj_link_cache_txn_t *t); /* length, flush, fsync, close */
/* Immutable artifact + versioned, checksummed manifest, published LAST.
 * Safe even if FAT rename is not replace-atomic or power fails between them.
 * ext is one of MP3/WAV/FLAC/PDB/DAT/EXT/JPG. */
bool dj_link_cache_commit(dj_link_cache_txn_t *t, const media_persistent_id_t *id,
    const char *ext, char out[DJ_LINK_CACHE_PATH]);
void dj_link_cache_abort(dj_link_cache_txn_t *t);
/* Full manifest and file SHA are checked; existence/size alone never hit. */
bool dj_link_cache_hit(dj_link_cache_t *c, const media_persistent_id_t *id,
    const char *ext, char out[DJ_LINK_CACHE_PATH]);
