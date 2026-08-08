#include "trace_adapter.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

static void compare_snapshot(unsigned index)
{
    trace_event_t p4[128] = {0};
    trace_event_t s3[128] = {0};
    const size_t p4_count = p4_trace_snapshot(p4, 128u);
    const size_t s3_count = s3_trace_snapshot(s3, 128u);
    if (p4_count != s3_count ||
        memcmp(p4, s3, p4_count * sizeof(p4[0])) != 0) {
        fprintf(stderr, "snapshot mismatch after input %u: p4=%zu s3=%zu\n",
                index, p4_count, s3_count);
        exit(1);
    }
    checks += 2u + (unsigned long)p4_count;
}

static void compare_message(trace_midi_t midi, unsigned index)
{
    trace_event_t p4 = {0};
    trace_event_t s3 = {0};
    const bool p4_emitted = p4_trace_feed(&midi, &p4);
    const bool s3_emitted = s3_trace_feed(&midi, &s3);
    if (p4_emitted != s3_emitted ||
        (p4_emitted && memcmp(&p4, &s3, sizeof(p4)) != 0)) {
        fprintf(stderr,
                "trace mismatch at %u MIDI=%02X %02X %02X emitted=%u/%u "
                "P4=(%u,%u,%d) S3=(%u,%u,%d)\n",
                index, midi.status, midi.data1, midi.data2,
                p4_emitted ? 1u : 0u, s3_emitted ? 1u : 0u,
                p4.type, p4.id, p4.value, s3.type, s3.id, s3.value);
        exit(1);
    }
    checks += p4_emitted ? 5u : 2u;
}

static uint32_t lcg(uint32_t *state)
{
    *state = (*state * 1664525u) + 1013904223u;
    return *state;
}

int main(void)
{
    p4_trace_reset();
    s3_trace_reset();

    const trace_midi_t known[] = {
        {0x90u, 0x0Bu, 0x7Fu}, {0x90u, 0x0Bu, 0x00u},
        {0x91u, 0x0Cu, 0x7Fu}, {0x91u, 0x0Cu, 0x00u},
        {0xB1u, 0x23u, 65u},   {0xB0u, 0x00u, 0x12u},
        {0xB0u, 0x20u, 0x34u}, {0xB0u, 0x13u, 0x22u},
        {0xB0u, 0x33u, 0x11u}, {0xB6u, 0x1Fu, 0x40u},
        {0xB6u, 0x3Fu, 0x00u}, {0x96u, 0x41u, 0x7Fu},
        {0x96u, 0x41u, 0x00u}, {0x97u, 0x00u, 0x7Fu},
        {0x97u, 0x00u, 0x00u}, {0xF0u, 0x00u, 0x00u},
    };
    for (unsigned i = 0u; i < sizeof(known) / sizeof(known[0]); ++i) {
        compare_message(known[i], i);
    }
    compare_snapshot((unsigned)(sizeof(known) / sizeof(known[0])));

    uint32_t state = 0x50414A4Fu;
    for (unsigned i = 0u; i < 100000u; ++i) {
        const uint32_t a = lcg(&state);
        const uint32_t b = lcg(&state);
        trace_midi_t midi = {
            .status = (uint8_t)(0x80u | ((a >> 24) & 0x6Fu)),
            .data1 = (uint8_t)((a >> 8) & 0x7Fu),
            .data2 = (uint8_t)(b & 0x7Fu),
        };
        compare_message(midi, i + 1000u);
        if ((i % 257u) == 0u) {
            compare_snapshot(i + 1000u);
        }
    }
    compare_snapshot(101000u);

    CHECK(checks > 200000u);
    printf("S3/P4 semantic trace equivalence: %lu checks passed\n", checks);
    return 0;
}
