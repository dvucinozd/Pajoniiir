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
