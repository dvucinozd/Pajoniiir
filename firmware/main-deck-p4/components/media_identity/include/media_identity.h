#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t bytes[32];
    bool valid;
} media_persistent_id_t;

typedef struct {
    uint32_t state[8];
    uint64_t total_len;
    uint8_t buffer[64];
    size_t buffer_len;
} media_sha256_ctx_t;

void media_sha256_init(media_sha256_ctx_t *ctx);
void media_sha256_update(media_sha256_ctx_t *ctx, const void *data, size_t len);
void media_sha256_final(media_sha256_ctx_t *ctx, uint8_t digest[32]);
void media_sha256(const void *data, size_t len, uint8_t digest[32]);

bool media_persistent_id_equal(const media_persistent_id_t *a,
                               const media_persistent_id_t *b);
void media_persistent_id_clear(media_persistent_id_t *id);

/* Derive the v2 Hot Cue identity. Integer fields are encoded little-endian and
 * path_len excludes the trailing NUL. */
bool media_persistent_id_derive(const uint8_t export_digest[32],
                                const char *usb_relative_path,
                                uint64_t file_size,
                                int64_t file_mtime,
                                media_persistent_id_t *out);
