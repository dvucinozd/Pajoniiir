/* Golden parity test: the compiled FLX4 dynamic profile must behave exactly
 * like the built-in flx4_map / flx4_led_midi C implementations.
 *
 * Input parity:  brute-force sweep of 3-byte MIDI messages through both
 *                mappers; every message must agree on match/no-match and on
 *                the emitted (type, id, value).
 * LED parity:    every (led, deck, state) combination must produce the same
 *                MIDI OUT bytes (or agree there is no mapping).
 * Snapshot parity: after the sweep, both replay snapshots must contain the
 *                same event multiset.
 */

#include "controller_profile.h"
#include "flx4_map.h"
#include "flx4_led_midi.h"
#include "control_link.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIXTURE_PATH "../../controllers/pioneer_ddj_flx4/profile.s3bin"

static const uint8_t k_statuses[] = {
    0x90, 0x91, 0x92, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9A, 0x9F,
    0xB0, 0xB1, 0xB2, 0xB4, 0xB6,
};
static const uint8_t k_data2[] = { 0x00, 0x01, 0x3F, 0x40, 0x41, 0x7F };

static size_t load_fixture(uint8_t *buf, size_t max)
{
    FILE *f = fopen(FIXTURE_PATH, "rb");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", FIXTURE_PATH);
        exit(1);
    }
    size_t len = fread(buf, 1, max, f);
    fclose(f);
    return len;
}

/* ── Input parity ──────────────────────────────────────────────────────────── */

static unsigned long g_messages;
static unsigned long g_matched;

static void check_message(const cp_profile_t *profile, cp_runtime_t *rt,
                          flx4_map_state_t *ref_state,
                          uint8_t status, uint8_t data1, uint8_t data2)
{
    flx4_midi_message_t msg = { 0 };
    msg.len = 3;
    msg.status = status;
    msg.data1 = data1;
    msg.data2 = data2;

    flx4_control_event_t ref_ev = { 0 };
    cp_event_t dyn_ev = { 0 };

    bool ref = flx4_map_message(ref_state, &msg, &ref_ev);
    bool dyn = cp_runtime_process(profile, rt, status, data1, data2, &dyn_ev);

    g_messages++;
    if (ref != dyn) {
        fprintf(stderr,
                "match mismatch: %02X %02X %02X ref=%d dyn=%d\n",
                status, data1, data2, ref, dyn);
        exit(1);
    }
    if (ref) {
        g_matched++;
        if (ref_ev.type != dyn_ev.type || ref_ev.id != dyn_ev.id ||
            ref_ev.value != dyn_ev.value) {
            fprintf(stderr,
                    "event mismatch: %02X %02X %02X "
                    "ref=(%u,0x%02X,%d) dyn=(%u,0x%02X,%d)\n",
                    status, data1, data2,
                    ref_ev.type, ref_ev.id, ref_ev.value,
                    dyn_ev.type, dyn_ev.id, dyn_ev.value);
            exit(1);
        }
    }
}

static void test_input_parity(const cp_profile_t *profile, cp_runtime_t *rt,
                              flx4_map_state_t *ref_state)
{
    for (size_t s = 0; s < sizeof(k_statuses); s++) {
        for (int d1 = 0; d1 < 0x80; d1++) {
            for (size_t v = 0; v < sizeof(k_data2); v++) {
                check_message(profile, rt, ref_state,
                              k_statuses[s], (uint8_t)d1, k_data2[v]);
            }
        }
    }
    printf("  input parity: %lu messages, %lu matched            PASS\n",
           g_messages, g_matched);
}

/* ── Snapshot parity ───────────────────────────────────────────────────────── */

typedef struct {
    cp_event_t events[64];
    size_t count;
} capture_t;

static bool capture_cp(uint8_t type, uint8_t id, int16_t value, void *ctx)
{
    capture_t *cap = (capture_t *)ctx;
    assert(cap->count < sizeof(cap->events) / sizeof(cap->events[0]));
    cap->events[cap->count].type = type;
    cap->events[cap->count].id = id;
    cap->events[cap->count].value = value;
    cap->count++;
    return true;
}

static int event_cmp(const void *a, const void *b)
{
    const cp_event_t *ea = (const cp_event_t *)a;
    const cp_event_t *eb = (const cp_event_t *)b;
    if (ea->type != eb->type) return (int)ea->type - (int)eb->type;
    if (ea->id != eb->id) return (int)ea->id - (int)eb->id;
    return (int)ea->value - (int)eb->value;
}

static void test_snapshot_parity(const cp_profile_t *profile,
                                 const cp_runtime_t *rt,
                                 const flx4_map_state_t *ref_state)
{
    capture_t ref_cap = { 0 };
    capture_t dyn_cap = { 0 };

    flx4_map_emit_snapshot(ref_state, capture_cp, &ref_cap);
    cp_runtime_emit_snapshot(profile, rt, capture_cp, &dyn_cap);

    if (ref_cap.count != dyn_cap.count) {
        fprintf(stderr, "snapshot count mismatch: ref=%zu dyn=%zu\n",
                ref_cap.count, dyn_cap.count);
        exit(1);
    }

    qsort(ref_cap.events, ref_cap.count, sizeof(cp_event_t), event_cmp);
    qsort(dyn_cap.events, dyn_cap.count, sizeof(cp_event_t), event_cmp);
    for (size_t i = 0; i < ref_cap.count; i++) {
        if (event_cmp(&ref_cap.events[i], &dyn_cap.events[i]) != 0) {
            fprintf(stderr,
                    "snapshot mismatch at %zu: ref=(%u,0x%02X,%d) "
                    "dyn=(%u,0x%02X,%d)\n", i,
                    ref_cap.events[i].type, ref_cap.events[i].id,
                    ref_cap.events[i].value,
                    dyn_cap.events[i].type, dyn_cap.events[i].id,
                    dyn_cap.events[i].value);
            exit(1);
        }
    }
    printf("  snapshot parity: %zu events                        PASS\n",
           ref_cap.count);
}

/* ── LED parity ────────────────────────────────────────────────────────────── */

static void test_led_parity(const cp_profile_t *profile)
{
    static const uint8_t states[] = { 0, 1, 2, 0x40, 0x7F };
    unsigned long checked = 0;
    unsigned long mapped = 0;

    for (uint8_t led = 0; led < LED_REMOTE_COUNT; led++) {
        for (uint8_t deck = 0; deck <= 1; deck++) {
            for (size_t s = 0; s < sizeof(states); s++) {
                uint8_t packet[4] = { 0 };
                uint8_t midi[3] = { 0 };

                bool ref = flx4_led_midi_build_packet(led, states[s], deck,
                                                      packet);
                bool dyn = cp_profile_map_led(profile, led, deck, states[s],
                                              midi);
                checked++;
                if (ref != dyn) {
                    fprintf(stderr,
                            "led match mismatch: led=%u deck=%u state=%u "
                            "ref=%d dyn=%d\n",
                            led, deck, states[s], ref, dyn);
                    exit(1);
                }
                if (!ref) {
                    continue;
                }
                mapped++;
                if (packet[1] != midi[0] || packet[2] != midi[1] ||
                    packet[3] != midi[2]) {
                    fprintf(stderr,
                            "led byte mismatch: led=%u deck=%u state=%u "
                            "ref=%02X %02X %02X dyn=%02X %02X %02X\n",
                            led, deck, states[s],
                            packet[1], packet[2], packet[3],
                            midi[0], midi[1], midi[2]);
                    exit(1);
                }
            }
        }
    }
    printf("  LED parity: %lu combos, %lu mapped                 PASS\n",
           checked, mapped);
}

int main(void)
{
    static uint8_t blob[16384];
    static cp_profile_t profile;
    cp_runtime_t rt;
    flx4_map_state_t ref_state;

    printf("=== controller_profile FLX4 golden parity ===\n");

    size_t len = load_fixture(blob, sizeof(blob));
    int rc = cp_profile_parse(blob, len, &profile);
    if (rc != CP_OK) {
        fprintf(stderr, "fixture parse failed: %d\n", rc);
        return 1;
    }
    if (profile.vid != 0x2B73 || profile.pid != 0x0045) {
        fprintf(stderr, "fixture VID/PID unexpected\n");
        return 1;
    }

    cp_runtime_init(&rt);
    flx4_map_init(&ref_state);

    test_input_parity(&profile, &rt, &ref_state);
    test_snapshot_parity(&profile, &rt, &ref_state);
    test_led_parity(&profile);

    printf("controller_profile parity tests passed\n");
    return 0;
}
