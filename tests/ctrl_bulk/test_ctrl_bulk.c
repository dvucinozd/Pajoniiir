/* Roundtrip test for the 0xA6 bulk frame layer: frames built by the S3-side
 * builder must parse and decode identically on the P4 side, byte-by-byte over
 * a simulated UART stream, including corruption and resync behaviour. */

#include "../../firmware/main-deck-p4/components/control_link/include/control_link.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* S3 side, wrapped in s3_bulk_shim.c to avoid header collisions. */
size_t s3_build_descriptor_frame(uint8_t *out, size_t cap, uint8_t seq,
                                 uint16_t vid, uint16_t pid, uint16_t caps,
                                 const char *product);

/* Feed a byte stream; returns the number of completed valid frames and stores
 * the last decoded descriptor in *rep (when any frame decoded). */
static int feed_stream(ctrl_bulk_parser_t *p, const uint8_t *data, size_t len,
                       ctrl_descriptor_report_t *rep, int *errors)
{
    int frames = 0;
    for (size_t i = 0; i < len; i++) {
        int r = ctrl_bulk_parser_feed(p, data[i]);
        if (r > 0) {
            frames++;
            assert(ctrl_bulk_decode_descriptor(p->buf, (size_t)r, rep));
        } else if (r < 0 && errors) {
            (*errors)++;
        }
    }
    return frames;
}

static void test_roundtrip(void)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = s3_build_descriptor_frame(frame, sizeof(frame), 42,
                                           0x2B73, 0x0045,
                                           CTRL_DESC_CAP_MIDI_IN |
                                           CTRL_DESC_CAP_MIDI_OUT |
                                           CTRL_DESC_CAP_USB_AUDIO,
                                           "Pioneer DDJ-FLX4");
    assert(len == (size_t)(CTRL_BULK_HEADER_LEN + CTRL_DESC_PAYLOAD_LEN +
                           CTRL_BULK_CRC_LEN));
    assert(frame[0] == CTRL_BULK_FRAME_START);
    assert(frame[1] == CTRL_BULK_TYPE_CONTROLLER_DESCRIPTOR);
    assert(frame[2] == 42);
    assert(frame[3] == CTRL_DESC_PAYLOAD_LEN);

    ctrl_bulk_parser_t parser;
    ctrl_bulk_parser_reset(&parser);
    ctrl_descriptor_report_t rep;
    memset(&rep, 0, sizeof(rep));

    assert(feed_stream(&parser, frame, len, &rep, NULL) == 1);
    assert(rep.vid == 0x2B73);
    assert(rep.pid == 0x0045);
    assert(rep.caps == (CTRL_DESC_CAP_MIDI_IN | CTRL_DESC_CAP_MIDI_OUT |
                        CTRL_DESC_CAP_USB_AUDIO));
    assert(strcmp(rep.product, "Pioneer DDJ-FLX4") == 0);
    printf("  build -> parse -> decode roundtrip                PASS\n");
}

static void test_product_truncation(void)
{
    const char *long_name =
        "An Extremely Long Controller Product Name Beyond The Field";
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = s3_build_descriptor_frame(frame, sizeof(frame), 0,
                                           0x1111, 0x2222, 0, long_name);
    assert(len > 0);

    ctrl_bulk_parser_t parser;
    ctrl_bulk_parser_reset(&parser);
    ctrl_descriptor_report_t rep;
    assert(feed_stream(&parser, frame, len, &rep, NULL) == 1);
    assert(strlen(rep.product) == CTRL_DESC_PRODUCT_MAX);
    assert(strncmp(rep.product, long_name, CTRL_DESC_PRODUCT_MAX) == 0);
    printf("  product truncation to %d bytes                    PASS\n",
           CTRL_DESC_PRODUCT_MAX);
}

static void test_garbage_and_resync(void)
{
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = s3_build_descriptor_frame(frame, sizeof(frame), 7,
                                           0x2B73, 0x0045, 3, "FLX4");

    /* Garbage (incl. 0xA5 event bytes) before the frame is ignored. */
    uint8_t stream[16 + CTRL_BULK_MAX_FRAME];
    const uint8_t garbage[] = { 0x00, 0xA5, 0x01, 0x10, 0x7F, 0x00, 0x33, 0xFF };
    memcpy(stream, garbage, sizeof(garbage));
    memcpy(stream + sizeof(garbage), frame, len);

    ctrl_bulk_parser_t parser;
    ctrl_bulk_parser_reset(&parser);
    ctrl_descriptor_report_t rep;
    int errors = 0;
    assert(feed_stream(&parser, stream, sizeof(garbage) + len, &rep, &errors) == 1);
    assert(errors == 0);
    assert(rep.vid == 0x2B73);

    /* A corrupted frame reports an error, then the next good frame parses. */
    uint8_t bad[CTRL_BULK_MAX_FRAME];
    memcpy(bad, frame, len);
    bad[CTRL_BULK_HEADER_LEN + 1] ^= 0xFF;

    ctrl_bulk_parser_reset(&parser);
    errors = 0;
    assert(feed_stream(&parser, bad, len, &rep, &errors) == 0);
    assert(errors == 1);
    assert(feed_stream(&parser, frame, len, &rep, &errors) == 1);
    assert(rep.pid == 0x0045);
    printf("  garbage skip + corrupt-frame resync               PASS\n");
}

static void test_bad_length_and_decode_guards(void)
{
    ctrl_bulk_parser_t parser;
    ctrl_bulk_parser_reset(&parser);

    /* Oversized length byte aborts at the header. */
    const uint8_t oversize[] = { CTRL_BULK_FRAME_START, 0x01, 0x00, 0xFF };
    int errors = 0;
    ctrl_descriptor_report_t rep;
    assert(feed_stream(&parser, oversize, sizeof(oversize), &rep, &errors) == 0);
    assert(errors == 1);

    /* Decode rejects wrong type and wrong length. */
    uint8_t frame[CTRL_BULK_MAX_FRAME];
    size_t len = s3_build_descriptor_frame(frame, sizeof(frame), 1,
                                           1, 2, 0, "x");
    assert(!ctrl_bulk_decode_descriptor(frame, len - 1, &rep));
    uint8_t wrong_type[CTRL_BULK_MAX_FRAME];
    memcpy(wrong_type, frame, len);
    wrong_type[1] = 0x05;
    assert(!ctrl_bulk_decode_descriptor(wrong_type, len, &rep));

    /* Builder refuses a too-small buffer. */
    assert(s3_build_descriptor_frame(frame, 10, 0, 1, 2, 0, "x") == 0);
    printf("  length/type/buffer guards                         PASS\n");
}

int main(void)
{
    printf("=== ctrl_bulk S3<->P4 frame tests ===\n");
    test_roundtrip();
    test_product_truncation();
    test_garbage_and_resync();
    test_bad_length_and_decode_guards();
    printf("ctrl_bulk tests passed\n");
    return 0;
}
