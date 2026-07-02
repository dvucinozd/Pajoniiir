#include "p4_audio_link.h"

#include <string.h>

typedef struct {
    int16_t left;
    int16_t right;
} p4_audio_link_frame_t;

static p4_audio_link_frame_t s_ring[P4_AUDIO_LINK_RING_CAPACITY_FRAMES];
static uint32_t s_read_index;
static uint32_t s_write_index;
static uint32_t s_used_frames;
static uint32_t s_last_sequence;
static bool s_have_sequence;
static p4_audio_link_stats_t s_stats;

/* Streaming deframer: reassembles P4HP blocks from an arbitrary byte stream
   (I2S carries zero filler between blocks and delivery chunks split blocks at
   any offset). Sized for the largest header-declared block we accept. */
#define P4_AUDIO_LINK_DEFRAME_MAX_FRAMES 512u
#define P4_AUDIO_LINK_DEFRAME_BUF_BYTES \
    (sizeof(p4_audio_link_block_header_t) + \
     (size_t)P4_AUDIO_LINK_DEFRAME_MAX_FRAMES * 2u * sizeof(int16_t))

static uint8_t s_deframe_buf[P4_AUDIO_LINK_DEFRAME_BUF_BYTES];
static size_t s_deframe_len;

static uint32_t p4_audio_link_crc32(const uint8_t *data, size_t len)
{
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

static void p4_audio_link_push_frame(int16_t left, int16_t right)
{
    if (s_used_frames >= P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {
        s_read_index = (s_read_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
        s_used_frames--;
        s_stats.overruns++;
    }

    s_ring[s_write_index].left = left;
    s_ring[s_write_index].right = right;
    s_write_index = (s_write_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    s_used_frames++;
}

esp_err_t p4_audio_link_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    memset(&s_stats, 0, sizeof(s_stats));
    s_stats.ring_capacity_frames = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    s_read_index = 0u;
    s_write_index = 0u;
    s_used_frames = 0u;
    s_last_sequence = 0u;
    s_have_sequence = false;
    s_deframe_len = 0u;
    return ESP_OK;
}

bool p4_audio_link_receive_block(const uint8_t *block, size_t block_len)
{
    if (!block || block_len < sizeof(p4_audio_link_block_header_t)) {
        return false;
    }

    p4_audio_link_block_header_t hdr;
    memcpy(&hdr, block, sizeof(hdr));
    if (hdr.magic != P4_AUDIO_LINK_BLOCK_MAGIC ||
        hdr.header_bytes != sizeof(p4_audio_link_block_header_t) ||
        hdr.frames == 0u) {
        return false;
    }

    const size_t payload_bytes = (size_t)hdr.frames * 2u * sizeof(int16_t);
    const size_t expected_len = (size_t)hdr.header_bytes + payload_bytes;
    if (block_len < expected_len) {
        return false;
    }

    const uint8_t *payload = block + hdr.header_bytes;
    uint32_t crc = p4_audio_link_crc32(payload, payload_bytes);
    if (crc != hdr.payload_crc32) {
        s_stats.crc_errors++;
        return false;
    }

    if (s_have_sequence && hdr.sequence != s_last_sequence + 1u) {
        s_stats.sequence_gaps++;
    }
    s_have_sequence = true;
    s_last_sequence = hdr.sequence;

    for (uint16_t i = 0; i < hdr.frames; ++i) {
        int16_t left;
        int16_t right;
        memcpy(&left, payload + ((size_t)i * 2u * sizeof(int16_t)), sizeof(left));
        memcpy(&right, payload + (((size_t)i * 2u + 1u) * sizeof(int16_t)), sizeof(right));
        p4_audio_link_push_frame(left, right);
    }

    s_stats.received_blocks++;
    s_stats.sample_rate = hdr.sample_rate;
    s_stats.ring_frames = s_used_frames;
    return true;
}

size_t p4_audio_link_read_frames(int16_t *dst_interleaved_stereo, size_t frames)
{
    if (!dst_interleaved_stereo || frames == 0u) {
        return 0u;
    }

    size_t read_frames = 0u;
    while (read_frames < frames && s_used_frames > 0u) {
        dst_interleaved_stereo[read_frames * 2u] = s_ring[s_read_index].left;
        dst_interleaved_stereo[read_frames * 2u + 1u] = s_ring[s_read_index].right;
        s_read_index = (s_read_index + 1u) % P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
        s_used_frames--;
        read_frames++;
    }

    if (read_frames < frames) {
        memset(&dst_interleaved_stereo[read_frames * 2u],
               0,
               (frames - read_frames) * 2u * sizeof(int16_t));
        s_stats.underruns++;
    }

    s_stats.ring_frames = s_used_frames;
    return read_frames;
}

void p4_audio_link_get_stats(p4_audio_link_stats_t *out)
{
    if (!out) {
        return;
    }
    s_stats.ring_frames = s_used_frames;
    s_stats.ring_capacity_frames = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    *out = s_stats;
}

static void deframe_consume(size_t bytes)
{
    if (bytes >= s_deframe_len) {
        s_deframe_len = 0u;
        return;
    }
    memmove(s_deframe_buf, &s_deframe_buf[bytes], s_deframe_len - bytes);
    s_deframe_len -= bytes;
}

static size_t deframe_find_magic(void)
{
    /* P4_AUDIO_LINK_BLOCK_MAGIC little endian on the wire: 'P' '4' 'H' 'P' */
    for (size_t i = 0; i + 4u <= s_deframe_len; ++i) {
        if (s_deframe_buf[i] == 0x50u && s_deframe_buf[i + 1u] == 0x34u &&
            s_deframe_buf[i + 2u] == 0x48u && s_deframe_buf[i + 3u] == 0x50u) {
            return i;
        }
    }
    return SIZE_MAX;
}

static void deframe_process(void)
{
    for (;;) {
        const size_t magic_at = deframe_find_magic();
        if (magic_at == SIZE_MAX) {
            /* No magic; keep the last 3 bytes in case a magic straddles the
               next chunk boundary. */
            if (s_deframe_len > 3u) {
                deframe_consume(s_deframe_len - 3u);
            }
            return;
        }
        if (magic_at > 0u) {
            deframe_consume(magic_at);
        }
        if (s_deframe_len < sizeof(p4_audio_link_block_header_t)) {
            return;
        }

        p4_audio_link_block_header_t hdr;
        memcpy(&hdr, s_deframe_buf, sizeof(hdr));
        if (hdr.header_bytes != sizeof(p4_audio_link_block_header_t) ||
            hdr.frames == 0u || hdr.frames > P4_AUDIO_LINK_DEFRAME_MAX_FRAMES) {
            deframe_consume(1u);
            continue;
        }

        const size_t total =
            (size_t)hdr.header_bytes + (size_t)hdr.frames * 2u * sizeof(int16_t);
        if (s_deframe_len < total) {
            return;
        }

        if (p4_audio_link_receive_block(s_deframe_buf, total)) {
            deframe_consume(total);
        } else {
            /* CRC failure already counted; resync past this magic. */
            deframe_consume(1u);
        }
    }
}

void p4_audio_link_feed_bytes(const uint8_t *data, size_t len)
{
    while (data && len > 0u) {
        const size_t space = sizeof(s_deframe_buf) - s_deframe_len;
        size_t chunk = len < space ? len : space;
        memcpy(&s_deframe_buf[s_deframe_len], data, chunk);
        s_deframe_len += chunk;
        data += chunk;
        len -= chunk;

        deframe_process();
        if (s_deframe_len == sizeof(s_deframe_buf)) {
            /* Cannot happen for header-validated blocks (total <= buf size);
               guarantees forward progress if it ever does. */
            deframe_consume(1u);
        }
    }
}
