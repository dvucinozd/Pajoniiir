/* Format/runtime unit tests for the S3CP controller profile parser+matcher. */

#include "controller_profile.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* ── Test blob builder ─────────────────────────────────────────────────────── */

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

typedef struct {
    uint8_t buf[2048];
    size_t len;
    uint16_t input_count;
    uint16_t output_count;
    uint8_t pair_slots;
} blob_builder_t;

static void blob_init(blob_builder_t *b)
{
    memset(b, 0, sizeof(*b));
    b->len = CP_HEADER_SIZE;
}

static void blob_add_input(blob_builder_t *b, uint8_t status, uint8_t data1,
                           uint8_t raw_type, uint8_t pair_slot,
                           uint8_t sem_type, uint8_t sem_id, uint16_t flags,
                           int16_t base_value, uint16_t press_mask,
                           const int8_t lut[4])
{
    uint8_t *p = b->buf + b->len;
    p[0] = status;
    p[1] = data1;
    p[2] = raw_type;
    p[3] = pair_slot;
    p[4] = sem_type;
    p[5] = sem_id;
    wr_u16(p + 6, flags);
    wr_u16(p + 8, (uint16_t)base_value);
    wr_u16(p + 10, press_mask);
    for (int i = 0; i < 4; i++) {
        p[12 + i] = lut ? (uint8_t)lut[i] : (uint8_t)-1;
    }
    b->len += CP_INPUT_ENTRY_SIZE;
    b->input_count++;
}

static void blob_add_output(blob_builder_t *b, uint8_t led, uint8_t deck,
                            uint8_t kind, uint8_t status, uint8_t data1,
                            uint8_t off, uint8_t on, uint8_t blink)
{
    uint8_t *p = b->buf + b->len;
    p[0] = led;
    p[1] = deck;
    p[2] = kind;
    p[3] = status;
    p[4] = data1;
    p[5] = off;
    p[6] = on;
    p[7] = blink;
    wr_u16(p + 8, 0);
    wr_u16(p + 10, 0);
    b->len += CP_OUTPUT_ENTRY_SIZE;
    b->output_count++;
}

static void blob_finish(blob_builder_t *b, uint16_t vid, uint16_t pid,
                        uint32_t flags)
{
    memcpy(b->buf, CP_MAGIC, 4);
    wr_u16(b->buf + 4, CP_VERSION);
    wr_u16(b->buf + 6, CP_HEADER_SIZE);
    wr_u32(b->buf + 8, (uint32_t)b->len);
    wr_u16(b->buf + 16, vid);
    wr_u16(b->buf + 18, pid);
    wr_u32(b->buf + 20, flags);
    wr_u16(b->buf + 24, b->input_count);
    wr_u16(b->buf + 26, b->output_count);
    b->buf[28] = b->pair_slots;
    b->buf[29] = 2;
    wr_u16(b->buf + 30, 0);
    wr_u32(b->buf + 12, cp_crc32(b->buf + 16, b->len - 16));
}

/* ── Tests ─────────────────────────────────────────────────────────────────── */

static void build_basic(blob_builder_t *b)
{
    blob_init(b);
    b->pair_slots = 2;
    /* play button */
    blob_add_input(b, 0x90, 0x0B, CP_IN_NOTE_BUTTON, CP_PAIR_SLOT_NONE,
                   0x01, 0x10, 0, 0, 0, NULL);
    /* packed pad action: mode 0 pad 3 shifted, press bit 0x80 */
    blob_add_input(b, 0x97, 0x03, CP_IN_NOTE_VALUE, CP_PAIR_SLOT_NONE,
                   0x01, 0x25, 0, 0x43, 0x80, NULL);
    /* rel64 jog */
    blob_add_input(b, 0xB0, 0x22, CP_IN_CC_REL64, CP_PAIR_SLOT_NONE,
                   0x02, 0x12, 0, 0, 0, NULL);
    /* 2C browse */
    blob_add_input(b, 0xB6, 0x40, CP_IN_CC_REL_2C, CP_PAIR_SLOT_NONE,
                   0x02, 0x60, 0, 0, 0, NULL);
    /* 14-bit pair on slot 0 (replay) */
    blob_add_input(b, 0xB0, 0x13, CP_IN_CC14_MSB, 0,
                   0x03, 0x50, CP_IN_FLAG_REPLAY, 0, 0, NULL);
    blob_add_input(b, 0xB0, 0x33, CP_IN_CC14_LSB, 0,
                   0x03, 0x50, CP_IN_FLAG_REPLAY, 0, 0, NULL);
    /* cc7 abs (replay) */
    blob_add_input(b, 0xB4, 0x02, CP_IN_CC7_ABS, CP_PAIR_SLOT_NONE,
                   0x03, 0x78, CP_IN_FLAG_REPLAY, 0, 0, NULL);
    /* state pair on slot 1 */
    static const int8_t lut[4] = { -1, 0, 1, 2 };
    blob_add_input(b, 0x94, 0x10, CP_IN_NOTE_STATE_PAIR, 1,
                   0x01, 0x77, 0, 0, 0, lut);
    blob_add_input(b, 0x95, 0x11, CP_IN_NOTE_STATE_PAIR, 1,
                   0x01, 0x77, CP_IN_FLAG_PAIR_MEMBER_B, 0, 0, lut);
    /* outputs: note per deck + cc passthrough + deck-any */
    blob_add_output(b, 1, 0, CP_OUT_NOTE_ONOFF, 0x90, 0x0B, 0x00, 0x7F, 0x11);
    blob_add_output(b, 1, 1, CP_OUT_NOTE_ONOFF, 0x91, 0x0B, 0x00, 0x7F, 0x11);
    blob_add_output(b, 5, 0, CP_OUT_CC_VALUE, 0xB0, 0x02, 0, 0, 0);
    blob_add_output(b, 41, CP_DECK_ANY, CP_OUT_NOTE_ONOFF, 0x96, 0x00,
                    0x00, 0x7F, 0x7F);
    blob_finish(b, 0x2B73, 0x0045, 0x0F);
}

static void test_parse_validation(void)
{
    blob_builder_t b;
    cp_profile_t profile;

    build_basic(&b);
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_OK);
    assert(profile.vid == 0x2B73 && profile.pid == 0x0045);
    assert(profile.input_count == 9 && profile.output_count == 4);
    assert(profile.pair_slot_count == 2);

    /* bad magic */
    build_basic(&b);
    b.buf[0] = 'X';
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_ERR_MAGIC);

    /* bad version */
    build_basic(&b);
    b.buf[4] = 9;
    wr_u32(b.buf + 12, cp_crc32(b.buf + 16, b.len - 16));
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_ERR_VERSION);

    /* corrupted payload -> CRC */
    build_basic(&b);
    b.buf[CP_HEADER_SIZE + 1] ^= 0xFF;
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_ERR_CRC);

    /* truncated */
    build_basic(&b);
    assert(cp_profile_parse(b.buf, b.len - 1, &profile) == CP_ERR_SIZE);
    assert(cp_profile_parse(b.buf, CP_HEADER_SIZE - 1, &profile) == CP_ERR_SIZE);

    /* pair slot out of declared range */
    build_basic(&b);
    b.buf[28] = 1; /* declare 1 slot while an entry references slot 1 */
    wr_u32(b.buf + 12, cp_crc32(b.buf + 16, b.len - 16));
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_ERR_BOUNDS);

    assert(cp_profile_parse(NULL, 0, &profile) == CP_ERR_ARG);
    printf("  parse validation                                  PASS\n");
}

static void expect_event(const cp_event_t *ev, uint8_t type, uint8_t id,
                         int16_t value)
{
    assert(ev->type == type);
    assert(ev->id == id);
    assert(ev->value == value);
}

static void test_runtime_mapping(void)
{
    blob_builder_t b;
    cp_profile_t profile;
    cp_runtime_t rt;
    cp_event_t ev;

    build_basic(&b);
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_OK);
    cp_runtime_init(&rt);

    /* note button both edges */
    assert(cp_runtime_process(&profile, &rt, 0x90, 0x0B, 0x7F, &ev));
    expect_event(&ev, 0x01, 0x10, 1);
    assert(cp_runtime_process(&profile, &rt, 0x90, 0x0B, 0x00, &ev));
    expect_event(&ev, 0x01, 0x10, 0);
    assert(cp_runtime_process(&profile, &rt, 0x80, 0x0B, 0x40, &ev));
    expect_event(&ev, 0x01, 0x10, 0);

    /* packed value: press sets mask, release keeps base */
    assert(cp_runtime_process(&profile, &rt, 0x97, 0x03, 0x7F, &ev));
    expect_event(&ev, 0x01, 0x25, 0x43 | 0x80);
    assert(cp_runtime_process(&profile, &rt, 0x97, 0x03, 0x00, &ev));
    expect_event(&ev, 0x01, 0x25, 0x43);

    /* rel64: 64 drops, 65 -> +1, 63 -> -1 */
    assert(!cp_runtime_process(&profile, &rt, 0xB0, 0x22, 64, &ev));
    assert(cp_runtime_process(&profile, &rt, 0xB0, 0x22, 65, &ev));
    expect_event(&ev, 0x02, 0x12, 1);
    assert(cp_runtime_process(&profile, &rt, 0xB0, 0x22, 63, &ev));
    expect_event(&ev, 0x02, 0x12, -1);

    /* two's complement: 0x00/0x40 drop, 0x01 -> +1, 0x7F -> -1 */
    assert(!cp_runtime_process(&profile, &rt, 0xB6, 0x40, 0x00, &ev));
    assert(!cp_runtime_process(&profile, &rt, 0xB6, 0x40, 0x40, &ev));
    assert(cp_runtime_process(&profile, &rt, 0xB6, 0x40, 0x01, &ev));
    expect_event(&ev, 0x02, 0x60, 1);
    assert(cp_runtime_process(&profile, &rt, 0xB6, 0x40, 0x7F, &ev));
    expect_event(&ev, 0x02, 0x60, -1);

    /* 14-bit: no emit until both halves seen */
    assert(!cp_runtime_process(&profile, &rt, 0xB0, 0x13, 0x40, &ev));
    assert(cp_runtime_process(&profile, &rt, 0xB0, 0x33, 0x01, &ev));
    expect_event(&ev, 0x03, 0x50, (0x40 << 7) | 0x01);
    assert(cp_runtime_process(&profile, &rt, 0xB0, 0x13, 0x20, &ev));
    expect_event(&ev, 0x03, 0x50, (0x20 << 7) | 0x01);

    /* cc7 abs */
    assert(cp_runtime_process(&profile, &rt, 0xB4, 0x02, 0x55, &ev));
    expect_event(&ev, 0x03, 0x78, 0x55);

    /* state pair: A -> 0, A+B -> 2, B -> 1, none -> no emit */
    assert(cp_runtime_process(&profile, &rt, 0x94, 0x10, 0x7F, &ev));
    expect_event(&ev, 0x01, 0x77, 0);
    assert(cp_runtime_process(&profile, &rt, 0x95, 0x11, 0x7F, &ev));
    expect_event(&ev, 0x01, 0x77, 2);
    assert(cp_runtime_process(&profile, &rt, 0x94, 0x10, 0x00, &ev));
    expect_event(&ev, 0x01, 0x77, 1);
    assert(!cp_runtime_process(&profile, &rt, 0x95, 0x11, 0x00, &ev));

    /* unmatched message */
    assert(!cp_runtime_process(&profile, &rt, 0x92, 0x0B, 0x7F, &ev));

    printf("  runtime input mapping                             PASS\n");
}

typedef struct {
    cp_event_t events[16];
    size_t count;
} capture_t;

static bool capture_cb(uint8_t type, uint8_t id, int16_t value, void *ctx)
{
    capture_t *cap = (capture_t *)ctx;
    if (cap->count < sizeof(cap->events) / sizeof(cap->events[0])) {
        cap->events[cap->count].type = type;
        cap->events[cap->count].id = id;
        cap->events[cap->count].value = value;
        cap->count++;
    }
    return true;
}

static void test_snapshot(void)
{
    blob_builder_t b;
    cp_profile_t profile;
    cp_runtime_t rt;
    cp_event_t ev;
    capture_t cap = { 0 };

    build_basic(&b);
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_OK);
    cp_runtime_init(&rt);

    /* nothing seen -> empty snapshot */
    assert(cp_runtime_emit_snapshot(&profile, &rt, capture_cb, &cap) == 0);

    /* half a 14-bit pair -> still empty */
    assert(!cp_runtime_process(&profile, &rt, 0xB0, 0x13, 0x30, &ev));
    assert(cp_runtime_emit_snapshot(&profile, &rt, capture_cb, &cap) == 0);

    /* complete pair + cc7 -> exactly two events, pair reported once */
    assert(cp_runtime_process(&profile, &rt, 0xB0, 0x33, 0x02, &ev));
    assert(cp_runtime_process(&profile, &rt, 0xB4, 0x02, 0x11, &ev));
    cap.count = 0;
    assert(cp_runtime_emit_snapshot(&profile, &rt, capture_cb, &cap) == 2);
    expect_event(&cap.events[0], 0x03, 0x50, (0x30 << 7) | 0x02);
    expect_event(&cap.events[1], 0x03, 0x78, 0x11);

    printf("  snapshot replay dedup                             PASS\n");
}

static void test_led_mapping(void)
{
    blob_builder_t b;
    cp_profile_t profile;
    uint8_t midi[3];

    build_basic(&b);
    assert(cp_profile_parse(b.buf, b.len, &profile) == CP_OK);

    /* note per deck: off/on/blink */
    assert(cp_profile_map_led(&profile, 1, 0, 0, midi));
    assert(midi[0] == 0x90 && midi[1] == 0x0B && midi[2] == 0x00);
    assert(cp_profile_map_led(&profile, 1, 1, 1, midi));
    assert(midi[0] == 0x91 && midi[1] == 0x0B && midi[2] == 0x7F);
    assert(cp_profile_map_led(&profile, 1, 0, 2, midi));
    assert(midi[2] == 0x11);

    /* cc passthrough */
    assert(cp_profile_map_led(&profile, 5, 0, 0x42, midi));
    assert(midi[0] == 0xB0 && midi[1] == 0x02 && midi[2] == 0x42);

    /* deck any */
    assert(cp_profile_map_led(&profile, 41, 0, 1, midi));
    assert(midi[0] == 0x96 && midi[1] == 0x00 && midi[2] == 0x7F);
    assert(cp_profile_map_led(&profile, 41, 1, 1, midi));
    assert(midi[0] == 0x96);

    /* unknown led / deck */
    assert(!cp_profile_map_led(&profile, 99, 0, 1, midi));
    assert(!cp_profile_map_led(&profile, 5, 1, 1, midi));

    printf("  LED output mapping                                PASS\n");
}

int main(void)
{
    printf("=== controller_profile format/runtime tests ===\n");
    test_parse_validation();
    test_runtime_mapping();
    test_snapshot();
    test_led_mapping();
    printf("controller_profile tests passed\n");
    return 0;
}
