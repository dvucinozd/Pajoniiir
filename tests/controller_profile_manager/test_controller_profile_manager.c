/* Host tests for the P4 controller profile manager.
 *
 * Uses the committed FLX4 fixture (compiled by
 * tools/controller_profile/compile_profile.py and parity-proven against the
 * S3 parser) as a cross-format guard: if the P4-side header reader ever
 * drifts from the S3CP format, this breaks.
 */

#include "controller_profile_manager.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define make_dir(p) _mkdir(p)
#else
#include <sys/stat.h>
#include <unistd.h>
#define make_dir(p) mkdir((p), 0777)
#endif

#define FLX4_FIXTURE "../../controllers/pioneer_ddj_flx4/profile.s3bin"
#define ROOT "cpm_root"

static uint8_t g_blob[CPM_MAX_PROFILE_SIZE];
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
    assert(g_blob_len > CPM_HEADER_SIZE);
}

static void write_file(const char *path, const uint8_t *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    assert(f);
    assert(fwrite(data, 1, len, f) == len);
    fclose(f);
}

static void wr_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static void test_meta_parse(void)
{
    controller_profile_meta_t meta;

    /* Real FLX4 fixture: the committed compiler output must parse. */
    assert(controller_profile_meta_parse(g_blob, g_blob_len, &meta) == ESP_OK);
    assert(meta.valid);
    assert(meta.vid == 0x2B73 && meta.pid == 0x0045);
    assert(meta.input_count == 291 && meta.output_count == 129);
    assert(meta.size == g_blob_len);

    /* Corrupt payload -> CRC failure. */
    static uint8_t bad[CPM_MAX_PROFILE_SIZE];
    memcpy(bad, g_blob, g_blob_len);
    bad[CPM_HEADER_SIZE + 3] ^= 0xFF;
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);
    assert(!meta.valid);

    /* Bad magic. */
    memcpy(bad, g_blob, g_blob_len);
    bad[0] = 'X';
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    /* Bad version. */
    memcpy(bad, g_blob, g_blob_len);
    bad[4] = 9;
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    /* Truncated / size mismatch. */
    assert(controller_profile_meta_parse(g_blob, g_blob_len - 1, &meta) ==
           ESP_ERR_INVALID_ARG);
    assert(controller_profile_meta_parse(g_blob, CPM_HEADER_SIZE - 1, &meta) ==
           ESP_ERR_INVALID_ARG);

    /* Declared size beyond CPM_MAX_PROFILE_SIZE is rejected up front. */
    memcpy(bad, g_blob, g_blob_len);
    wr_u32(bad + 8, CPM_MAX_PROFILE_SIZE + 1);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    assert(controller_profile_meta_parse(NULL, 0, &meta) == ESP_ERR_INVALID_ARG);
    printf("  meta parse (fixture cross-format guard)          PASS\n");
}

static void build_tree(void)
{
    (void)make_dir(ROOT);
    (void)make_dir(ROOT "/pioneer_ddj_flx4");
    (void)make_dir(ROOT "/corrupt_ctrl");
    (void)make_dir(ROOT "/not_a_profile");

    write_file(ROOT "/pioneer_ddj_flx4/profile.s3bin", g_blob, g_blob_len);

    static uint8_t bad[CPM_MAX_PROFILE_SIZE];
    memcpy(bad, g_blob, g_blob_len);
    bad[20] ^= 0xFF; /* flip a payload byte -> CRC mismatch */
    write_file(ROOT "/corrupt_ctrl/profile.s3bin", bad, g_blob_len);
    /* not_a_profile stays empty: must be skipped entirely. */
}

static void test_scan_and_match(void)
{
    controller_profile_registry_t reg;

    build_tree();
    assert(controller_profile_scan_dir(ROOT, &reg) == ESP_OK);
    assert(reg.count == 2);
    assert(reg.active_index == -1);
    assert(!reg.controller_present);

    int flx4 = -1;
    int corrupt = -1;
    for (int i = 0; i < reg.count; i++) {
        if (strcmp(reg.profiles[i].id, "pioneer_ddj_flx4") == 0) {
            flx4 = i;
        } else if (strcmp(reg.profiles[i].id, "corrupt_ctrl") == 0) {
            corrupt = i;
        }
    }
    assert(flx4 >= 0 && corrupt >= 0);
    assert(reg.profiles[flx4].valid);
    assert(reg.profiles[flx4].vid == 0x2B73);
    assert(strstr(reg.profiles[flx4].path, "profile.s3bin") != NULL);
    assert(!reg.profiles[corrupt].valid);

    /* Exact match ignores invalid entries. */
    assert(controller_profile_registry_match(&reg, 0x2B73, 0x0045) == flx4);
    assert(controller_profile_registry_match(&reg, 0x2B73, 0x9999) == -1);

    /* Descriptor selects the active profile... */
    assert(controller_profile_registry_on_descriptor(&reg, 0x2B73, 0x0045) == flx4);
    assert(reg.controller_present);
    assert(reg.connected_vid == 0x2B73 && reg.connected_pid == 0x0045);
    assert(reg.matched_index == flx4);
    assert(reg.active_index == -1);
    assert(reg.transfer_state == CPM_TRANSFER_MATCHED);

    /* ...and an unknown controller clears it while staying "present". */
    assert(controller_profile_registry_on_descriptor(&reg, 0x1234, 0x5678) == -1);
    assert(reg.controller_present);
    assert(reg.matched_index == -1);
    assert(reg.active_index == -1);
    assert(reg.transfer_state == CPM_TRANSFER_UNSUPPORTED);

    /* Missing root reports NOT_FOUND. */
    assert(controller_profile_scan_dir("no_such_dir_xyz", &reg) ==
           ESP_ERR_NOT_FOUND);
    assert(controller_profile_scan_dir(NULL, &reg) == ESP_ERR_INVALID_ARG);

    printf("  scan + registry match/descriptor                 PASS\n");
}

static void test_descriptor_match_waits_for_activate_ack(void)
{
    controller_profile_registry_t reg;

    build_tree();
    assert(controller_profile_scan_dir(ROOT, &reg) == ESP_OK);
    int flx4 = controller_profile_registry_match(&reg, 0x2B73, 0x0045);
    assert(flx4 >= 0);

    assert(controller_profile_registry_on_descriptor(&reg, 0x2B73, 0x0045) == flx4);
    assert(reg.controller_present);
    assert(reg.matched_index == flx4);
    assert(reg.active_index == -1);
    assert(reg.transfer_state == CPM_TRANSFER_MATCHED);

    controller_profile_registry_mark_transfer_started(&reg, flx4);
    assert(reg.transfer_state == CPM_TRANSFER_TRANSFERRING);
    assert(reg.active_index == -1);

    controller_profile_registry_mark_transfer_active(&reg, flx4);
    assert(reg.transfer_state == CPM_TRANSFER_ACTIVE);
    assert(reg.active_index == flx4);

    assert(controller_profile_registry_on_descriptor(&reg, 0x1234, 0x5678) == -1);
    assert(reg.controller_present);
    assert(reg.matched_index == -1);
    assert(reg.active_index == -1);
    assert(reg.transfer_state == CPM_TRANSFER_UNSUPPORTED);

    printf("  descriptor match waits for activate ACK           PASS\n");
}

static void cleanup_tree(void)
{
    remove(ROOT "/pioneer_ddj_flx4/profile.s3bin");
    remove(ROOT "/corrupt_ctrl/profile.s3bin");
#ifdef _WIN32
    (void)_rmdir(ROOT "/pioneer_ddj_flx4");
    (void)_rmdir(ROOT "/corrupt_ctrl");
    (void)_rmdir(ROOT "/not_a_profile");
    (void)_rmdir(ROOT);
#else
    (void)rmdir(ROOT "/pioneer_ddj_flx4");
    (void)rmdir(ROOT "/corrupt_ctrl");
    (void)rmdir(ROOT "/not_a_profile");
    (void)rmdir(ROOT);
#endif
}

int main(void)
{
    printf("=== controller_profile_manager tests ===\n");
    load_fixture();
    test_meta_parse();
    test_scan_and_match();
    test_descriptor_match_waits_for_activate_ack();
    cleanup_tree();
    printf("controller_profile_manager tests passed\n");
    return 0;
}
