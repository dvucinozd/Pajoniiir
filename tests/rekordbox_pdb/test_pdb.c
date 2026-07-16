/*
 * test_pdb.c  —  PC test harness for the Rekordbox PDB parser
 *
 * Builds on Linux/macOS/Windows with:
 *   make              — build
 *   make test         — build + run API contract tests (no file needed)
 *
 * Run with a real Rekordbox USB drive:
 *   ./test_pdb F:\PIONEER\rekordbox\export.pdb
 *   ./test_pdb F:\PIONEER\rekordbox\export.pdb --limit 20
 */

/* REKORDBOX_PDB_STANDALONE_TEST passed via -D on compiler command line (see Makefile) */
#include "rekordbox_pdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test helpers ─────────────────────────────────────────────────────────── */

static int g_tests_run    = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST(name)        do { g_tests_run++; printf("  %-55s", name); } while (0)
#define PASS()            do { g_tests_passed++; printf("PASS\n"); } while (0)
#define FAIL(msg)         do { g_tests_failed++; printf("FAIL  (%s)\n", msg); } while (0)
#define CHECK(cond, msg)  do { if (cond) PASS(); else FAIL(msg); } while (0)

/* ── API contract tests (no file required) ───────────────────────────────── */

static void test_api_contracts(void)
{
    printf("\n=== API contract tests ===\n");

    pdb_t *pdb = NULL;

    TEST("pdb_open NULL path returns error");
    CHECK(pdb_open(NULL, &pdb) != ESP_OK, "should fail");

    TEST("pdb_open NULL out returns error");
    CHECK(pdb_open("/some/path", NULL) != ESP_OK, "should fail");

    TEST("pdb_open nonexistent file returns error");
    CHECK(pdb_open("/nonexistent/export.pdb", &pdb) != ESP_OK, "should fail");

    TEST("pdb_track_count(NULL) == 0");
    CHECK(pdb_track_count(NULL) == 0, "should be 0");

    TEST("pdb_get_track(NULL, 0, ...) returns error");
    pdb_track_t t;
    CHECK(pdb_get_track(NULL, 0, &t) != ESP_OK, "should fail");

    TEST("pdb_close(NULL) does not crash");
    pdb_close(NULL);
    PASS();
}

static void test_devicesql_utf16_to_utf8(void)
{
    printf("\n=== DeviceSQL UTF-16 decoding tests ===\n");

    const uint16_t codepoints[] = {
        '/', 'C', 'o', 'n', 't', 'e', 'n', 't', 's', '/',
        0x5468, 0x9632, 0x7FA9, 0x548C,
        '/', 't', 'e', 's', 't', '.', 'm', 'p', '3', 0
    };
    uint8_t raw[4 + sizeof(codepoints)] = {0};
    raw[0] = 0x90; /* long string, UTF-16, little-endian */
    uint16_t total_len = (uint16_t)sizeof(raw);
    raw[1] = (uint8_t)(total_len & 0xFF);
    raw[2] = (uint8_t)(total_len >> 8);
    raw[3] = 0;
    for (size_t i = 0; i < sizeof(codepoints) / sizeof(codepoints[0]); i++) {
        raw[4 + i * 2] = (uint8_t)(codepoints[i] & 0xFF);
        raw[5 + i * 2] = (uint8_t)(codepoints[i] >> 8);
    }

    char decoded[128];
    TEST("UTF-16 DeviceSQL preserves non-ASCII as UTF-8");
    CHECK(pdb_test_decode_devicesql_string(raw, sizeof(raw), decoded, sizeof(decoded)) == ESP_OK &&
          strcmp(decoded, "/Contents/\xE5\x91\xA8\xE9\x98\xB2\xE7\xBE\xA9\xE5\x92\x8C/test.mp3") == 0,
          decoded);
}

static void put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void test_malformed_page_zero_row_groups(void)
{
    const char *path = "test_malformed_page_zero.pdb";
    uint8_t data[64] = {0};

    /* A one-page file whose only table points at page zero.  The packed nrows
     * value claims far more row-slot groups than the 64-byte page can hold.
     * The second group has only 28 bytes remaining and used to underflow the
     * backwards size_t cursor before the following rowpf read. */
    put_le32(data + 4, (uint32_t)sizeof(data)); /* page_size */
    put_le32(data + 8, 1u);                    /* num_tables */
    put_le32(data + 12, 0xFFFFFFFFu);          /* page_next */
    put_le32(data + 24, 0x1FFFu);              /* packed nrows */
    put_le32(data + 28, 0u);                   /* tracks table type */
    put_le32(data + 36, 0u);                   /* first_page */

    FILE *fp = fopen(path, "wb");
    TEST("malformed page-zero row groups do not underflow cursor");
    if (!fp) {
        FAIL("cannot create malformed fixture");
        return;
    }
    bool wrote = fwrite(data, 1u, sizeof(data), fp) == sizeof(data);
    bool closed = fclose(fp) == 0;
    if (!wrote || !closed) {
        remove(path);
        FAIL("cannot write malformed fixture");
        return;
    }

    pdb_t *pdb = NULL;
    esp_err_t rc = pdb_open(path, &pdb);
    bool safe_empty_result = rc == ESP_OK && pdb != NULL && pdb_track_count(pdb) == 0;
    pdb_close(pdb);
    remove(path);
    CHECK(safe_empty_result, "malformed page should be safely truncated");
}

/* ── Real-file integration test ──────────────────────────────────────────── */

static void test_real_file(const char *pdb_path, int limit)
{
    printf("\n=== Real-file test: %s ===\n", pdb_path);

    pdb_t *pdb = NULL;
    esp_err_t rc = pdb_open(pdb_path, &pdb);
    if (rc != ESP_OK) {
        printf("FAIL: pdb_open returned %d\n", rc);
        g_tests_failed++;
        return;
    }

    int n = pdb_track_count(pdb);
    printf("Tracks loaded: %d\n", n);

    /* Basic sanity */
    TEST("track count > 0");
    CHECK(n > 0, "expected at least one track");

    /* Dump tracks */
    printf("\n%-4s %-6s %-5s %-25s %-35s %-40s\n",
           "#", "ID", "BPM", "Artist", "Title", "ANLZ path");
    printf("%s\n", "--------------------------------------------------------"
                   "-----------------------------------------------------");

    int show = (limit > 0 && limit < n) ? limit : n;
    int tracks_with_anlz = 0;
    int tracks_with_artist = 0;

    for (int i = 0; i < n; i++) {
        pdb_track_t t;
        if (pdb_get_track(pdb, i, &t) != ESP_OK) continue;

        if (t.anlz_path[0] != '\0') tracks_with_anlz++;
        if (t.artist[0]    != '\0') tracks_with_artist++;

        if (i < show) {
            printf("%-4d %-6u %-5u %-25.24s %-35.34s %-40.39s\n",
                   i + 1, t.track_id, t.bpm,
                   t.artist[0] ? t.artist : "(none)",
                   t.title[0]  ? t.title  : "(no title)",
                   t.anlz_path[0] ? t.anlz_path : "(no anlz)");
        }
    }

    if (show < n)
        printf("  ... (%d more tracks not shown)\n", n - show);

    printf("\nSummary: %d tracks, %d with artist name, %d with ANLZ path\n",
           n, tracks_with_artist, tracks_with_anlz);

    /* Validate ANLZ paths have expected prefix */
    TEST("all tracks with ANLZ path start with /PIONEER/USBANLZ");
    int bad_anlz = 0;
    for (int i = 0; i < n; i++) {
        pdb_track_t t;
        if (pdb_get_track(pdb, i, &t) != ESP_OK) continue;
        if (t.anlz_path[0] != '\0' &&
            strncmp(t.anlz_path, "/PIONEER/USBANLZ", 16) != 0) {
            bad_anlz++;
        }
    }
    CHECK(bad_anlz == 0, "unexpected ANLZ path prefix");

    /* Validate file paths have expected prefix */
    TEST("all tracks with file path start with /Contents or /");
    int bad_path = 0;
    for (int i = 0; i < n; i++) {
        pdb_track_t t;
        if (pdb_get_track(pdb, i, &t) != ESP_OK) continue;
        if (t.file_path[0] != '\0' && t.file_path[0] != '/') bad_path++;
    }
    CHECK(bad_path == 0, "unexpected file path format");

    pdb_close(pdb);
}

/* ── main ─────────────────────────────────────────────────────────────────── */

int main(int argc, char *argv[])
{
    printf("=== Rekordbox PDB parser test harness ===\n");

    /* Always run API contract tests */
    test_api_contracts();
    test_devicesql_utf16_to_utf8();
    test_malformed_page_zero_row_groups();

    /* Real-file test if a path is provided */
    if (argc >= 2) {
        int limit = 0;
        for (int i = 2; i < argc; i++) {
            if (strcmp(argv[i], "--limit") == 0 && i + 1 < argc) {
                limit = atoi(argv[++i]);
            }
        }
        test_real_file(argv[1], limit);
    } else {
        printf("\n(No PDB path given — skipping real-file test)\n");
        printf("Usage: %s <path/to/export.pdb> [--limit N]\n", argv[0]);
    }

    printf("\n=== Results: %d/%d passed", g_tests_passed, g_tests_run);
    if (g_tests_failed > 0) printf(" (%d FAILED)", g_tests_failed);
    printf(" ===\n");

    return g_tests_failed > 0 ? 1 : 0;
}
