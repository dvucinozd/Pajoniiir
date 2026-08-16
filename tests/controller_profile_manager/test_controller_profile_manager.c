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
#define GENERIC_FIXTURE "../../controllers/generic_midi_ci/profile.s3bin"
#define ROOT "cpm_root"
#define INSTALL_ROOT "cpm_install_root"

static uint8_t g_blob[CPM_MAX_PROFILE_SIZE];
static size_t g_blob_len;
static uint8_t g_generic_blob[CPM_MAX_PROFILE_SIZE];
static size_t g_generic_blob_len;

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

    f = fopen(GENERIC_FIXTURE, "rb");
    if (!f) {
        fprintf(stderr, "cannot open fixture %s\n", GENERIC_FIXTURE);
        exit(1);
    }
    g_generic_blob_len = fread(g_generic_blob, 1, sizeof(g_generic_blob), f);
    fclose(f);
    assert(g_generic_blob_len > CPM_HEADER_SIZE);
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

static void wr_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)(v >> 8);
}

static void refresh_crc(uint8_t *blob, size_t len)
{
    wr_u32(blob + 12, controller_profile_crc32(blob + 16, len - 16));
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

    /* Structural constraints must match the authoritative S3 parser too. */
    memcpy(bad, g_blob, g_blob_len);
    wr_u16(bad + 24, CPM_MAX_INPUTS + 1);
    refresh_crc(bad, g_blob_len);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    memcpy(bad, g_blob, g_blob_len);
    wr_u16(bad + 24, 0);
    wr_u16(bad + 26, 0);
    refresh_crc(bad, g_blob_len);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    memcpy(bad, g_blob, g_blob_len);
    bad[CPM_HEADER_SIZE + 2] = CPM_MAX_RAW_TYPE + 1;
    refresh_crc(bad, g_blob_len);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    memcpy(bad, g_blob, g_blob_len);
    bad[CPM_HEADER_SIZE + 2] = 4; /* CC14 MSB requires a valid pair slot. */
    bad[CPM_HEADER_SIZE + 3] = CPM_PAIR_SLOT_NONE;
    refresh_crc(bad, g_blob_len);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    memcpy(bad, g_blob, g_blob_len);
    uint16_t fixture_inputs = (uint16_t)(g_blob[24] |
                                         ((uint16_t)g_blob[25] << 8));
    size_t first_output = CPM_HEADER_SIZE +
                          (size_t)fixture_inputs * CPM_INPUT_ENTRY_SIZE;
    bad[first_output + 2] = CPM_MAX_OUTPUT_KIND + 1;
    refresh_crc(bad, g_blob_len);
    assert(controller_profile_meta_parse(bad, g_blob_len, &meta) ==
           ESP_ERR_INVALID_ARG);

    assert(controller_profile_meta_parse(NULL, 0, &meta) == ESP_ERR_INVALID_ARG);
    printf("  meta parse (fixture cross-format guard)          PASS\n");
}

static void build_tree(void)
{
    (void)make_dir(ROOT);
    (void)make_dir(ROOT "/pioneer_ddj_flx4");
    (void)make_dir(ROOT "/generic_midi_ci");
    (void)make_dir(ROOT "/corrupt_ctrl");
    (void)make_dir(ROOT "/not_a_profile");

    write_file(ROOT "/pioneer_ddj_flx4/profile.s3bin", g_blob, g_blob_len);
    write_file(ROOT "/generic_midi_ci/profile.s3bin",
               g_generic_blob, g_generic_blob_len);

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
    assert(reg.count == 3);
    assert(reg.active_index == -1);
    assert(!reg.controller_present);

    int flx4 = -1;
    int corrupt = -1;
    int generic = -1;
    for (int i = 0; i < reg.count; i++) {
        if (strcmp(reg.profiles[i].id, "pioneer_ddj_flx4") == 0) {
            flx4 = i;
        } else if (strcmp(reg.profiles[i].id, "corrupt_ctrl") == 0) {
            corrupt = i;
        } else if (strcmp(reg.profiles[i].id, "generic_midi_ci") == 0) {
            generic = i;
        }
    }
    assert(flx4 >= 0 && corrupt >= 0 && generic >= 0);
    assert(reg.profiles[flx4].valid);
    assert(reg.profiles[flx4].vid == 0x2B73);
    assert(strstr(reg.profiles[flx4].path, "profile.s3bin") != NULL);
    assert(!reg.profiles[corrupt].valid);
    assert(reg.profiles[generic].valid);
    assert(reg.profiles[generic].vid == 0x1209);
    assert(reg.profiles[generic].pid == 0xC0DE);

    /* Exact match ignores invalid entries. */
    assert(controller_profile_registry_match(&reg, 0x2B73, 0x0045) == flx4);
    assert(controller_profile_registry_match(&reg, 0x2B73, 0x9999) == -1);
    assert(controller_profile_registry_match(&reg, 0x1209, 0xC0DE) == generic);

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

static void assert_file_equals(const char *path, const uint8_t *data, size_t len)
{
    static uint8_t actual[CPM_MAX_PROFILE_SIZE + 1];
    FILE *f = fopen(path, "rb");
    assert(f);
    size_t got = fread(actual, 1, sizeof(actual), f);
    fclose(f);
    assert(got == len);
    assert(memcmp(actual, data, len) == 0);
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

static void test_rescan_preserves_descriptor_and_requires_reactivation(void)
{
    controller_profile_registry_t live;
    controller_profile_registry_t scanned;

    build_tree();
    assert(controller_profile_scan_dir(ROOT, &live) == ESP_OK);
    int flx4 = controller_profile_registry_on_descriptor(&live, 0x2B73, 0x0045);
    assert(flx4 >= 0);
    live.connected_caps = 0x0007;
    snprintf(live.connected_product, sizeof(live.connected_product),
             "Pioneer DDJ-FLX4");
    controller_profile_registry_mark_transfer_active(&live, flx4);
    assert(live.transfer_state == CPM_TRANSFER_ACTIVE);

    assert(controller_profile_scan_dir(ROOT, &scanned) == ESP_OK);
    controller_profile_registry_apply_rescan(&live, &scanned);
    assert(live.controller_present);
    assert(live.connected_vid == 0x2B73 && live.connected_pid == 0x0045);
    assert(live.connected_caps == 0x0007);
    assert(strcmp(live.connected_product, "Pioneer DDJ-FLX4") == 0);
    assert(live.matched_index >= 0);
    assert(live.active_index == -1);
    assert(live.transfer_state == CPM_TRANSFER_MATCHED);
    printf("  rescan preserves descriptor and forces reactivation PASS\n");
}

static void test_disconnect_clears_live_state_and_preserves_profiles(void)
{
    controller_profile_registry_t reg;

    build_tree();
    assert(controller_profile_scan_dir(ROOT, &reg) == ESP_OK);
    int flx4 = controller_profile_registry_on_descriptor(&reg, 0x2B73, 0x0045);
    assert(flx4 >= 0);
    reg.connected_caps = 0x0007;
    snprintf(reg.connected_product, sizeof(reg.connected_product),
             "Pioneer DDJ-FLX4");
    controller_profile_registry_mark_transfer_active(&reg, flx4);
    uint8_t profile_count = reg.count;

    assert(controller_profile_registry_on_disconnect(&reg));
    assert(!reg.controller_present);
    assert(reg.connected_vid == 0u && reg.connected_pid == 0u);
    assert(reg.connected_caps == 0u);
    assert(reg.connected_product[0] == '\0');
    assert(reg.matched_index == -1);
    assert(reg.active_index == -1);
    assert(reg.transfer_state == CPM_TRANSFER_IDLE);
    assert(reg.count == profile_count);
    assert(!controller_profile_registry_on_disconnect(&reg));
    assert(!controller_profile_registry_on_disconnect(NULL));

    printf("  disconnect clears live state, preserves profiles      PASS\n");
}

static void test_descriptor_epoch_policy(void)
{
    controller_profile_registry_t reg = {0};
    reg.controller_present = true;
    reg.connected_vid = 0x2B73;
    reg.connected_pid = 0x0045;
    reg.connected_epoch = 10u;

    assert(controller_profile_descriptor_is_fresh(&reg, 0x2B73, 0x0045, 10u));
    assert(!controller_profile_descriptor_is_fresh(&reg, 0x1209, 0xC0DE, 10u));
    assert(!controller_profile_descriptor_is_fresh(&reg, 0x2B73, 0x0045, 9u));
    assert(controller_profile_descriptor_is_fresh(&reg, 0x1209, 0xC0DE, 11u));
    assert(!controller_profile_descriptor_is_fresh(&reg, 0x2B73, 0x0045, 0u));
    assert(!controller_profile_descriptor_is_fresh(NULL, 0, 0, 1u));

    reg.connected_epoch = UINT32_MAX;
    assert(controller_profile_descriptor_is_fresh(&reg, 0x1209, 0xC0DE, 1u));
    reg.controller_present = false;
    assert(controller_profile_descriptor_is_fresh(&reg, 0x1209, 0xC0DE, 1u));
    printf("  descriptor identity/epoch ordering policy          PASS\n");
}

static void test_profile_id_validation(void)
{
    assert(controller_profile_id_valid("pioneer_ddj_flx4"));
    assert(controller_profile_id_valid("DDJ-FLX4_2"));
    assert(!controller_profile_id_valid(NULL));
    assert(!controller_profile_id_valid(""));
    assert(!controller_profile_id_valid("../escape"));
    assert(!controller_profile_id_valid("has.dot"));
    assert(!controller_profile_id_valid("has/slash"));
    assert(!controller_profile_id_valid("has space"));

    char too_long[CPM_ID_MAX + 1];
    memset(too_long, 'a', sizeof(too_long) - 1);
    too_long[sizeof(too_long) - 1] = '\0';
    assert(!controller_profile_id_valid(too_long));
    printf("  strict profile ID validation                       PASS\n");
}

static void cleanup_install_tree(void)
{
    remove(INSTALL_ROOT "/test_profile/" CPM_PROFILE_FILENAME);
    remove(INSTALL_ROOT "/test_profile/" CPM_UPLOAD_FILENAME);
    remove(INSTALL_ROOT "/test_profile/" CPM_BACKUP_FILENAME);
#ifdef _WIN32
    (void)_rmdir(INSTALL_ROOT "/test_profile");
    (void)_rmdir(INSTALL_ROOT);
#else
    (void)rmdir(INSTALL_ROOT "/test_profile");
    (void)rmdir(INSTALL_ROOT);
#endif
}

static void test_atomic_install_and_recovery(void)
{
    const char *target = INSTALL_ROOT "/test_profile/" CPM_PROFILE_FILENAME;
    const char *upload = INSTALL_ROOT "/test_profile/" CPM_UPLOAD_FILENAME;
    const char *backup = INSTALL_ROOT "/test_profile/" CPM_BACKUP_FILENAME;
    controller_profile_meta_t meta;
    static uint8_t bad[CPM_MAX_PROFILE_SIZE];

    cleanup_install_tree();
    assert(controller_profile_storage_install(INSTALL_ROOT, "test_profile",
                                              g_blob, g_blob_len, false,
                                              &meta) == ESP_OK);
    assert(meta.valid && strcmp(meta.id, "test_profile") == 0);
    assert(meta.size == g_blob_len);
    assert_file_equals(target, g_blob, g_blob_len);

    /* An explicit overwrite is mandatory and invalid input never touches disk. */
    assert(controller_profile_storage_install(INSTALL_ROOT, "test_profile",
                                              g_blob, g_blob_len, false,
                                              NULL) == ESP_ERR_INVALID_STATE);
    memcpy(bad, g_blob, g_blob_len);
    bad[CPM_HEADER_SIZE] ^= 0x5A;
    assert(controller_profile_storage_install(INSTALL_ROOT, "test_profile",
                                              bad, g_blob_len, true,
                                              NULL) == ESP_ERR_INVALID_ARG);
    assert_file_equals(target, g_blob, g_blob_len);

    assert(controller_profile_storage_install(INSTALL_ROOT, "test_profile",
                                              g_blob, g_blob_len, true,
                                              NULL) == ESP_OK);
    assert_file_equals(target, g_blob, g_blob_len);

    /* Power loss after old target -> backup and during partial upload. */
    assert(rename(target, backup) == 0);
    write_file(upload, g_blob, CPM_HEADER_SIZE / 2);
    assert(controller_profile_storage_recover(INSTALL_ROOT, "test_profile") ==
           ESP_OK);
    assert_file_equals(target, g_blob, g_blob_len);
    assert(fopen(upload, "rb") == NULL);

    /* A torn/corrupt new target must not win over a valid old backup. */
    write_file(target, bad, g_blob_len);
    write_file(backup, g_blob, g_blob_len);
    assert(controller_profile_storage_recover(INSTALL_ROOT, "test_profile") ==
           ESP_OK);
    assert_file_equals(target, g_blob, g_blob_len);
    assert(fopen(backup, "rb") == NULL);

    /* Completed target wins over stale artifacts left around a reboot. */
    write_file(backup, bad, g_blob_len);
    write_file(upload, bad, CPM_HEADER_SIZE);
    assert(controller_profile_storage_recover(INSTALL_ROOT, "test_profile") ==
           ESP_OK);
    assert_file_equals(target, g_blob, g_blob_len);
    assert(fopen(backup, "rb") == NULL);
    assert(fopen(upload, "rb") == NULL);

    assert(controller_profile_storage_install(INSTALL_ROOT, "../escape",
                                              g_blob, g_blob_len, true,
                                              NULL) == ESP_ERR_INVALID_ARG);
    cleanup_install_tree();
    printf("  atomic profile install + interrupted-swap recovery PASS\n");
}

static void cleanup_tree(void)
{
    remove(ROOT "/pioneer_ddj_flx4/profile.s3bin");
    remove(ROOT "/generic_midi_ci/profile.s3bin");
    remove(ROOT "/corrupt_ctrl/profile.s3bin");
#ifdef _WIN32
    (void)_rmdir(ROOT "/pioneer_ddj_flx4");
    (void)_rmdir(ROOT "/generic_midi_ci");
    (void)_rmdir(ROOT "/corrupt_ctrl");
    (void)_rmdir(ROOT "/not_a_profile");
    (void)_rmdir(ROOT);
#else
    (void)rmdir(ROOT "/pioneer_ddj_flx4");
    (void)rmdir(ROOT "/generic_midi_ci");
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
    test_rescan_preserves_descriptor_and_requires_reactivation();
    test_disconnect_clears_live_state_and_preserves_profiles();
    test_descriptor_epoch_policy();
    test_profile_id_validation();
    test_atomic_install_and_recovery();
    cleanup_tree();
    printf("controller_profile_manager tests passed\n");
    return 0;
}
