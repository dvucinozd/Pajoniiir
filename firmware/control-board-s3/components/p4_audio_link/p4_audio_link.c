#include "p4_audio_link.h"

#include <string.h>

typedef struct {
    int16_t left;
    int16_t right;
} p4_audio_link_frame_t;

static p4_audio_link_frame_t s_ring[P4_AUDIO_LINK_RING_CAPACITY_FRAMES];
/* SPSC ring with free-running counters: the I2S RX task is the sole writer
   and the FLX4 USB Audio transfer callback is the sole reader. The writer uses
   drop-newest when full, so it never overwrites a slot the reader may currently
   be copying. Cross-context visibility uses acquire/release. */
static uint32_t s_write_count;
static uint32_t s_read_count;
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

static void stats_add(uint32_t *counter, uint32_t delta)
{
    (void)__atomic_fetch_add(counter, delta, __ATOMIC_RELAXED);
}

static void stats_store(uint32_t *counter, uint32_t value)
{
    __atomic_store_n(counter, value, __ATOMIC_RELAXED);
}

static uint32_t stats_load(const uint32_t *counter)
{
    return __atomic_load_n(counter, __ATOMIC_RELAXED);
}

static uint32_t p4_audio_link_crc32_update(uint32_t crc, const uint8_t *data, size_t len)
{
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint32_t)data[i];
        for (unsigned bit = 0; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1) ^ (0xEDB88320u & mask);
        }
    }
    return crc;
}

static uint32_t p4_audio_link_block_crc32(const p4_audio_link_block_header_t *header,
                                          const uint8_t *payload,
                                          size_t payload_bytes)
{
    p4_audio_link_block_header_t protected_header = *header;
    protected_header.block_crc32 = 0u;
    uint32_t crc = 0xFFFFFFFFu;
    crc = p4_audio_link_crc32_update(crc,
                                     (const uint8_t *)&protected_header,
                                     sizeof(protected_header));
    crc = p4_audio_link_crc32_update(crc, payload, payload_bytes);
    return ~crc;
}

static void p4_audio_link_push_frame(int16_t left, int16_t right)
{
    const uint32_t w = s_write_count;
    const uint32_t r = __atomic_load_n(&s_read_count, __ATOMIC_ACQUIRE);
    if (w - r >= P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {
        /* Drop newest. Overwriting the oldest slot is unsafe because the reader
         * may already have sampled its index but not copied both stereo words. */
        stats_add(&s_stats.overruns, 1u);
        return;
    }

    s_ring[w % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].left = left;
    s_ring[w % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].right = right;
    __atomic_store_n(&s_write_count, w + 1u, __ATOMIC_RELEASE);
}

esp_err_t p4_audio_link_init(void)
{
    memset(s_ring, 0, sizeof(s_ring));
    memset(&s_stats, 0, sizeof(s_stats));
    stats_store(&s_stats.ring_capacity_frames, P4_AUDIO_LINK_RING_CAPACITY_FRAMES);
    __atomic_store_n(&s_write_count, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_read_count, 0u, __ATOMIC_RELEASE);
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
    uint32_t crc = p4_audio_link_block_crc32(&hdr, payload, payload_bytes);
    if (crc != hdr.block_crc32) {
        stats_add(&s_stats.crc_errors, 1u);
        return false;
    }

    if (s_have_sequence && hdr.sequence != s_last_sequence + 1u) {
        stats_add(&s_stats.sequence_gaps, 1u);
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

    stats_add(&s_stats.received_blocks, 1u);
    stats_store(&s_stats.sample_rate, hdr.sample_rate);
    return true;
}

static uint32_t ring_used_frames(void)
{
    const uint32_t w = __atomic_load_n(&s_write_count, __ATOMIC_ACQUIRE);
    const uint32_t r = __atomic_load_n(&s_read_count, __ATOMIC_ACQUIRE);
    const uint32_t used = w - r;
    return used > P4_AUDIO_LINK_RING_CAPACITY_FRAMES
               ? P4_AUDIO_LINK_RING_CAPACITY_FRAMES
               : used;
}

size_t p4_audio_link_read_frames(int16_t *dst_interleaved_stereo, size_t frames)
{
    if (!dst_interleaved_stereo || frames == 0u) {
        return 0u;
    }

    uint32_t r = s_read_count;
    const uint32_t w = __atomic_load_n(&s_write_count, __ATOMIC_ACQUIRE);
    uint32_t avail = w - r;
    if (avail > P4_AUDIO_LINK_RING_CAPACITY_FRAMES) {
        /* Defensive recovery for counter corruption/wrap. Normal operation can
         * never lap because the writer drops new frames while the ring is full. */
        r = w - P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
        avail = P4_AUDIO_LINK_RING_CAPACITY_FRAMES;
    }

    size_t read_frames = 0u;
    while (read_frames < frames && avail > 0u) {
        dst_interleaved_stereo[read_frames * 2u] = s_ring[r % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].left;
        dst_interleaved_stereo[read_frames * 2u + 1u] = s_ring[r % P4_AUDIO_LINK_RING_CAPACITY_FRAMES].right;
        r++;
        avail--;
        read_frames++;
    }
    __atomic_store_n(&s_read_count, r, __ATOMIC_RELEASE);

    if (read_frames < frames) {
        memset(&dst_interleaved_stereo[read_frames * 2u],
               0,
               (frames - read_frames) * 2u * sizeof(int16_t));
        stats_add(&s_stats.underruns, 1u);
    }

    return read_frames;
}

void p4_audio_link_get_stats(p4_audio_link_stats_t *out)
{
    if (!out) {
        return;
    }
    p4_audio_link_stats_t snapshot = {
        .received_blocks = stats_load(&s_stats.received_blocks),
        .sequence_gaps = stats_load(&s_stats.sequence_gaps),
        .crc_errors = stats_load(&s_stats.crc_errors),
        .ring_frames = ring_used_frames(),
        .ring_capacity_frames = P4_AUDIO_LINK_RING_CAPACITY_FRAMES,
        .underruns = stats_load(&s_stats.underruns),
        .overruns = stats_load(&s_stats.overruns),
        .sample_rate = stats_load(&s_stats.sample_rate),
    };
    *out = snapshot;
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
