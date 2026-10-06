#include "media_identity.h"

#include <string.h>

static const uint32_t K[64] = {
    0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
    0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
    0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
    0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
    0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
    0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
    0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
    0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u,
};

static uint32_t rotr(uint32_t x, unsigned n) { return (x >> n) | (x << (32u - n)); }

static void transform(media_sha256_ctx_t *ctx, const uint8_t block[64])
{
    uint32_t w[64];
    for (unsigned i = 0; i < 16; ++i) {
        const uint8_t *p = block + i * 4u;
        w[i] = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
               ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    }
    for (unsigned i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    uint32_t a=ctx->state[0], b=ctx->state[1], c=ctx->state[2], d=ctx->state[3];
    uint32_t e=ctx->state[4], f=ctx->state[5], g=ctx->state[6], h=ctx->state[7];
    for (unsigned i = 0; i < 64; ++i) {
        uint32_t s1 = rotr(e,6) ^ rotr(e,11) ^ rotr(e,25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t t1 = h + s1 + ch + K[i] + w[i];
        uint32_t s0 = rotr(a,2) ^ rotr(a,13) ^ rotr(a,22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t t2 = s0 + maj;
        h=g; g=f; f=e; e=d+t1; d=c; c=b; b=a; a=t1+t2;
    }
    ctx->state[0]+=a; ctx->state[1]+=b; ctx->state[2]+=c; ctx->state[3]+=d;
    ctx->state[4]+=e; ctx->state[5]+=f; ctx->state[6]+=g; ctx->state[7]+=h;
}

void media_sha256_init(media_sha256_ctx_t *ctx)
{
    if (!ctx) return;
    *ctx = (media_sha256_ctx_t){
        .state = {0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                  0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u},
    };
}

void media_sha256_update(media_sha256_ctx_t *ctx, const void *data, size_t len)
{
    if (!ctx || (!data && len)) return;
    const uint8_t *src = data;
    ctx->total_len += len;
    while (len) {
        size_t take = 64u - ctx->buffer_len;
        if (take > len) take = len;
        memcpy(ctx->buffer + ctx->buffer_len, src, take);
        ctx->buffer_len += take;
        src += take;
        len -= take;
        if (ctx->buffer_len == 64u) {
            transform(ctx, ctx->buffer);
            ctx->buffer_len = 0u;
        }
    }
}

void media_sha256_final(media_sha256_ctx_t *ctx, uint8_t digest[32])
{
    if (!ctx || !digest) return;
    uint64_t bits = ctx->total_len * 8u;
    uint8_t pad[128] = {0x80};
    size_t pad_len = ctx->buffer_len < 56u ? 56u - ctx->buffer_len : 120u - ctx->buffer_len;
    media_sha256_update(ctx, pad, pad_len);
    uint8_t tail[8];
    for (unsigned i = 0; i < 8; ++i) tail[7u - i] = (uint8_t)(bits >> (i * 8u));
    media_sha256_update(ctx, tail, sizeof(tail));
    for (unsigned i = 0; i < 8; ++i) {
        digest[i*4u]=(uint8_t)(ctx->state[i]>>24); digest[i*4u+1]=(uint8_t)(ctx->state[i]>>16);
        digest[i*4u+2]=(uint8_t)(ctx->state[i]>>8); digest[i*4u+3]=(uint8_t)ctx->state[i];
    }
    memset(ctx, 0, sizeof(*ctx));
}

void media_sha256(const void *data, size_t len, uint8_t digest[32])
{
    media_sha256_ctx_t ctx;
    media_sha256_init(&ctx); media_sha256_update(&ctx, data, len); media_sha256_final(&ctx, digest);
}

bool media_persistent_id_equal(const media_persistent_id_t *a,
                               const media_persistent_id_t *b)
{
    return a && b && a->valid && b->valid && memcmp(a->bytes, b->bytes, 32u) == 0;
}

void media_persistent_id_clear(media_persistent_id_t *id)
{
    if (id) memset(id, 0, sizeof(*id));
}

static void put_le64(uint8_t out[8], uint64_t value)
{
    for (unsigned i = 0; i < 8; ++i) out[i] = (uint8_t)(value >> (i * 8u));
}

bool media_persistent_id_derive(const uint8_t export_digest[32],
                                const char *usb_relative_path,
                                uint64_t file_size,
                                int64_t file_mtime,
                                media_persistent_id_t *out)
{
    static const char domain[] = "pajoniiir.hotcue.v2";
    if (!export_digest || !usb_relative_path || !out) return false;
    size_t path_len = strlen(usb_relative_path);
    if (path_len == 0u || path_len > UINT16_MAX) return false;
    media_sha256_ctx_t ctx;
    media_sha256_init(&ctx);
    media_sha256_update(&ctx, domain, sizeof(domain) - 1u);
    media_sha256_update(&ctx, export_digest, 32u);
    uint8_t len_le[2] = {(uint8_t)path_len, (uint8_t)(path_len >> 8)};
    uint8_t value_le[8];
    media_sha256_update(&ctx, len_le, sizeof(len_le));
    media_sha256_update(&ctx, usb_relative_path, path_len);
    put_le64(value_le, file_size); media_sha256_update(&ctx, value_le, sizeof(value_le));
    put_le64(value_le, (uint64_t)file_mtime); media_sha256_update(&ctx, value_le, sizeof(value_le));
    media_sha256_final(&ctx, out->bytes);
    out->valid = true;
    return true;
}
