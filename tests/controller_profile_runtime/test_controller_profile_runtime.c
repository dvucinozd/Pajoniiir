/* Host test for the S3 dynamic controller-profile runtime wrapper.
 *
 * cp_profile_parse / cp_runtime_process are proven equivalent to flx4_map by
 * the controller_profile golden parity suite; this test proves the wrapper
 * plumbs the active FLX4 fixture through correctly and handles
 * activate/clear/parse-failure without disturbing an active profile. */

#include "controller_profile_runtime.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Expected semantic values (control_link.h): BUTTON=0x01, PITCH=0x03,
 * deck1.play=0x10, deck1.tempo=0x15, ch1_volume=0x50. */
#define SEM_BUTTON 0x01
#define SEM_PITCH  0x03

#define FLX4_FIXTURE "../../controllers/pioneer_ddj_flx4/profile.s3bin"

static uint8_t g_blob[16384];
static size_t g_blob_len;

static void load_fixture(void)
{
    FILE *f = fopen(FLX4_FIXTURE, "rb");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", FLX4_FIXTURE);
        exit(1);
    }
    g_blob_len = fread(g_blob, 1, sizeof(g_blob), f);
    fclose(f);
    assert(g_blob_len > 32);
}

typedef struct {
    int count;
    uint8_t last_type;
    uint8_t last_id;
    int16_t last_value;
} capture_t;

static bool snap_cb(uint8_t type, uint8_t id, int16_t value, void *ctx)
{
    capture_t *c = (capture_t *)ctx;
    c->count++;
    c->last_type = type;
    c->last_id = id;
    c->last_value = value;
    return true;
}

int main(void)
{
    printf("=== controller_profile_runtime tests ===\n");
    load_fixture();
    controller_profile_runtime_init();

    uint8_t type = 0, id = 0;
    int16_t value = 0;

    /* No profile active -> map returns false. */
    assert(!controller_profile_runtime_active());
    assert(!controller_profile_runtime_map(0x90, 0x0B, 0x7F, &type, &id, &value));

    /* Activate the FLX4 fixture. */
    assert(controller_profile_runtime_activate(g_blob, g_blob_len, 0x2B73, 0x0045));
    assert(controller_profile_runtime_active());

    /* Deck 1 Play -> BUTTON deck1.play=0x10 value 1/0. */
    assert(controller_profile_runtime_map(0x90, 0x0B, 0x7F, &type, &id, &value));
    assert(type == SEM_BUTTON && id == 0x10 && value == 1);
    assert(controller_profile_runtime_map(0x90, 0x0B, 0x00, &type, &id, &value));
    assert(value == 0);

    /* Deck 1 tempo 14-bit: no emit on MSB alone, emit on LSB. */
    assert(!controller_profile_runtime_map(0xB0, 0x00, 0x40, &type, &id, &value));
    assert(controller_profile_runtime_map(0xB0, 0x20, 0x01, &type, &id, &value));
    assert(type == SEM_PITCH && id == 0x15 && value == ((0x40 << 7) | 0x01));

    /* Unmatched status -> no event. */
    assert(!controller_profile_runtime_map(0x92, 0x0B, 0x7F, &type, &id, &value));

    /* Feed a replayable control (ch1 volume 14-bit) and confirm the snapshot
     * re-emits it. */
    assert(!controller_profile_runtime_map(0xB0, 0x13, 0x55, &type, &id, &value));
    assert(controller_profile_runtime_map(0xB0, 0x33, 0x22, &type, &id, &value));
    assert(type == SEM_PITCH && id == 0x50);
    capture_t cap = { 0 };
    size_t n = controller_profile_runtime_emit_snapshot(snap_cb, &cap);
    assert(n >= 1 && cap.count == (int)n);
    printf("  activate + map + snapshot (FLX4 fixture)          PASS\n");

    /* A failed parse must NOT disturb the active profile. */
    static uint8_t garbage[64];
    memset(garbage, 0xAB, sizeof(garbage));
    assert(!controller_profile_runtime_activate(garbage, sizeof(garbage), 1, 2));
    assert(controller_profile_runtime_active());
    assert(controller_profile_runtime_map(0x90, 0x0B, 0x7F, &type, &id, &value));
    assert(id == 0x10);
    printf("  failed parse keeps prior profile active           PASS\n");

    /* Clear (and the NULL-blob clear path) drops the profile. */
    controller_profile_runtime_clear();
    assert(!controller_profile_runtime_active());
    assert(!controller_profile_runtime_map(0x90, 0x0B, 0x7F, &type, &id, &value));
    assert(controller_profile_runtime_activate(NULL, 0, 0, 0)); /* NULL == clear, true */
    assert(!controller_profile_runtime_active());
    printf("  clear + NULL-blob clear                           PASS\n");

    printf("controller_profile_runtime tests passed\n");
    return 0;
}
