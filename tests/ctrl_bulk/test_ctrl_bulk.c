/* 0xA6 bulk frame + profile transfer tests.
 *
 * ctrl_bulk.c and cp_xfer.c are byte-identical on the S3 and P4 sides (the
 * runner asserts the file copies match), so exercising one copy proves both.
 * The profile-transfer path is driven end-to-end with the real committed FLX4
 * fixture: build frames -> stream through the parser -> reassemble in the
 * cp_xfer receiver -> the reassembled bytes must equal the fixture AND parse as
 * a valid S3CP profile. */

#include "control_link.h"
#include "controller_profile.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FLX4_FIXTURE "../../controllers/pioneer_ddj_flx4/profile.s3bin"

static uint8_t g_fixture[16384];
static size_t g_fixture_len;

static void load_fixture(void)
{
    FILE *f = fopen(FLX4_FIXTURE, "rb");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", FLX4_FIXTURE);
        exit(1);
    }
    g_fixture_len = fread(g_fixture, 1, sizeof(g_fixture), f);
    fclose(f);
    assert(g_fixture_len > CTRL_BULK_HEADER_LEN);
}

/* ── descriptor roundtrip (Faza 4 regression) ──────────────────────────────── */

static void test_descriptor_roundtrip(void)
{
    ctrl_descriptor_report_t in = { 0 };
    in.vid = 0x2B73;
    in.pid = 0x0045;
    in.caps = CTRL_DESC_CAP_MIDI_IN | CTRL_DESC_CAP_MIDI_OUT | CTRL_DESC_CAP_USB_AUDIO;
    snprintf(in.product, sizeof(in.product), "Pioneer DDJ-FLX4");

    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = ctrl_bulk_build_descriptor_frame(frame, sizeof(frame), 3, &in);
    assert(len > 0);

    ctrl_bulk_parser_t p;
    ctrl_bulk_parser_reset(&p);
    int r = 0;
    for (size_t i = 0; i < len; i++) {
        r = ctrl_bulk_parser_feed(&p, frame[i]);
    }
    assert(r == (int)len);
    ctrl_descriptor_report_t out;
    assert(ctrl_bulk_decode_descriptor(p.buf, (size_t)r, &out));
    assert(out.vid == in.vid && out.pid == in.pid && out.caps == in.caps);
    assert(strcmp(out.product, "Pioneer DDJ-FLX4") == 0);
    printf("  descriptor roundtrip                              PASS\n");
}

/* ── ACK / NACK / STATUS / simple frame codecs ─────────────────────────────── */

static void test_control_frames(void)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    ctrl_bulk_parser_t p;
    int r = 0;

    /* ACK */
    size_t len = ctrl_bulk_build_profile_ack(frame, sizeof(frame), 1,
                                             CTRL_BULK_TYPE_PROFILE_END);
    ctrl_bulk_parser_reset(&p);
    for (size_t i = 0; i < len; i++) r = ctrl_bulk_parser_feed(&p, frame[i]);
    assert(r == (int)len);
    uint8_t acked = 0;
    assert(ctrl_bulk_decode_profile_ack(p.buf, (size_t)r, &acked));
    assert(acked == CTRL_BULK_TYPE_PROFILE_END);

    /* NACK */
    len = ctrl_bulk_build_profile_nack(frame, sizeof(frame), 2,
                                       CTRL_BULK_TYPE_PROFILE_CHUNK,
                                       CTRL_PROFILE_NACK_OFFSET);
    ctrl_bulk_parser_reset(&p);
    for (size_t i = 0; i < len; i++) r = ctrl_bulk_parser_feed(&p, frame[i]);
    assert(r == (int)len);
    uint8_t nacked = 0, reason = 0;
    assert(ctrl_bulk_decode_profile_nack(p.buf, (size_t)r, &nacked, &reason));
    assert(nacked == CTRL_BULK_TYPE_PROFILE_CHUNK);
    assert(reason == CTRL_PROFILE_NACK_OFFSET);
    /* decode as the wrong type must fail */
    assert(!ctrl_bulk_decode_profile_ack(p.buf, (size_t)r, &acked));

    /* STATUS */
    len = ctrl_bulk_build_profile_status(frame, sizeof(frame), 3,
                                         CTRL_PROFILE_STATE_ACTIVE, 0x2B73, 0x0045);
    ctrl_bulk_parser_reset(&p);
    for (size_t i = 0; i < len; i++) r = ctrl_bulk_parser_feed(&p, frame[i]);
    assert(r == (int)len);
    uint8_t state = 0; uint16_t vid = 0, pid = 0;
    assert(ctrl_bulk_decode_profile_status(p.buf, (size_t)r, &state, &vid, &pid));
    assert(state == CTRL_PROFILE_STATE_ACTIVE && vid == 0x2B73 && pid == 0x0045);

    /* simple (END) */
    len = ctrl_bulk_build_profile_simple(frame, sizeof(frame), 4,
                                         CTRL_BULK_TYPE_PROFILE_END);
    ctrl_bulk_parser_reset(&p);
    for (size_t i = 0; i < len; i++) r = ctrl_bulk_parser_feed(&p, frame[i]);
    assert(r == (int)len);
    assert(p.buf[1] == CTRL_BULK_TYPE_PROFILE_END && p.buf[3] == 0);

    printf("  ACK/NACK/STATUS/simple codecs                     PASS\n");
}

/* ── Full profile transfer with the real FLX4 fixture ──────────────────────── */

static uint8_t g_rx_buf[16384];

/* Stream a blob P4->S3: BEGIN, CHUNKs, END, driving the receiver via the
 * parser exactly as the RX task would. Returns the terminal nack reason
 * (0 = ok); stops early on the first non-zero. */
static uint8_t transfer(cp_xfer_rx_t *rx, const uint8_t *blob, uint32_t size,
                        uint32_t crc, uint16_t vid, uint16_t pid,
                        size_t chunk_size)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    ctrl_bulk_parser_t p;
    ctrl_bulk_parser_reset(&p);

    uint8_t begin_reason = 0xFF;
    size_t len = ctrl_bulk_build_profile_begin(frame, sizeof(frame), 0, size,
                                               crc, vid, pid);
    for (size_t i = 0; i < len; i++) {
        int r = ctrl_bulk_parser_feed(&p, frame[i]);
        if (r > 0) {
            uint32_t t, c; uint16_t v, d;
            assert(ctrl_bulk_decode_profile_begin(p.buf, (size_t)r, &t, &c, &v, &d));
            begin_reason = cp_xfer_rx_begin(rx, t, c, v, d);
        }
    }
    if (begin_reason != CTRL_PROFILE_NACK_NONE) {
        return begin_reason;
    }

    for (uint32_t off = 0; off < size; off += chunk_size) {
        size_t n = size - off;
        if (n > chunk_size) n = chunk_size;
        len = ctrl_bulk_build_profile_chunk(frame, sizeof(frame), 0, off,
                                            blob + off, n);
        assert(len > 0);
        for (size_t i = 0; i < len; i++) {
            int r = ctrl_bulk_parser_feed(&p, frame[i]);
            if (r > 0) {
                uint32_t o; const uint8_t *dp; size_t dl;
                assert(ctrl_bulk_decode_profile_chunk(p.buf, (size_t)r, &o, &dp, &dl));
                uint8_t reason = cp_xfer_rx_chunk(rx, o, dp, dl);
                if (reason != CTRL_PROFILE_NACK_NONE) {
                    return reason;
                }
            }
        }
    }

    return cp_xfer_rx_end(rx);
}

static void test_profile_transfer(void)
{
    cp_xfer_rx_t rx;
    uint32_t crc = cp_xfer_crc32(g_fixture, g_fixture_len);

    /* Several chunk sizes, including the max, to prove offset reassembly. */
    const size_t sizes[] = { 1, 17, 64, CTRL_PROFILE_CHUNK_MAX };
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        cp_xfer_rx_init(&rx, g_rx_buf, sizeof(g_rx_buf));
        uint8_t reason = transfer(&rx, g_fixture, (uint32_t)g_fixture_len, crc,
                                  0x2B73, 0x0045, sizes[s]);
        assert(reason == CTRL_PROFILE_NACK_NONE);
        assert(rx.state == CTRL_PROFILE_STATE_STORED);
        assert(rx.total == g_fixture_len);
        assert(memcmp(rx.buf, g_fixture, g_fixture_len) == 0);

        /* Reassembled bytes must be a valid S3CP profile. */
        static cp_profile_t prof;
        assert(cp_profile_parse(rx.buf, rx.total, &prof) == CP_OK);
        assert(prof.vid == 0x2B73 && prof.pid == 0x0045);
    }
    printf("  fixture transfer -> reassemble -> parse           PASS\n");
}

static void test_transfer_nacks(void)
{
    cp_xfer_rx_t rx;
    uint8_t small_buf[64];
    uint8_t d[4] = { 1, 2, 3, 4 };

    /* total_size beyond receiver capacity -> SIZE */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_begin(&rx, 1000, 0, 0, 0) == CTRL_PROFILE_NACK_SIZE);
    assert(rx.state == CTRL_PROFILE_STATE_ERROR);

    /* zero size -> SIZE */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_begin(&rx, 0, 0, 0, 0) == CTRL_PROFILE_NACK_SIZE);

    /* chunk before begin -> STATE */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_chunk(&rx, 0, d, 4) == CTRL_PROFILE_NACK_STATE);

    /* out-of-order offset -> OFFSET */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_begin(&rx, 8, 0, 0, 0) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_chunk(&rx, 0, d, 4) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_chunk(&rx, 6, d, 2) == CTRL_PROFILE_NACK_OFFSET); /* gap */
    assert(rx.state == CTRL_PROFILE_STATE_ERROR);

    /* correct size but wrong CRC -> CRC */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_begin(&rx, 4, 0xDEADBEEF, 0, 0) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_chunk(&rx, 0, d, 4) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_end(&rx) == CTRL_PROFILE_NACK_CRC);

    /* END before all bytes -> CRC (incomplete) */
    cp_xfer_rx_init(&rx, small_buf, sizeof(small_buf));
    assert(cp_xfer_rx_begin(&rx, 8, 0, 0, 0) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_chunk(&rx, 0, d, 4) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_end(&rx) == CTRL_PROFILE_NACK_CRC);

    /* A fresh BEGIN recovers from ERROR. */
    assert(cp_xfer_rx_begin(&rx, 4, cp_xfer_crc32(d, 4), 0, 0) ==
           CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_chunk(&rx, 0, d, 4) == CTRL_PROFILE_NACK_NONE);
    assert(cp_xfer_rx_end(&rx) == CTRL_PROFILE_NACK_NONE);
    assert(rx.state == CTRL_PROFILE_STATE_STORED);

    printf("  transfer NACK paths + recovery                    PASS\n");
}

int main(void)
{
    printf("=== ctrl_bulk + profile transfer tests ===\n");
    load_fixture();
    test_descriptor_roundtrip();
    test_control_frames();
    test_profile_transfer();
    test_transfer_nacks();
    printf("ctrl_bulk tests passed\n");
    return 0;
}
