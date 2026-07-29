/*
 * Host tests for the unified ANLZ resolver and compact library row-order in library.c.
 *
 * Compiles the real library.c against controllable stubs for the metadata
 * cache, the ANLZ parser, the USB gate and the PDB, plus a counting allocator
 * for the heap-owned metadata fields so leaks and double-frees are detectable.
 *
 * Covers: warm cache hit (no parse/write), cold cache miss (one DAT + one EXT +
 * one write), cache rejection fallback, cache-write failure staying non-fatal,
 * parser failure preserving the previously published current metadata, and
 * sequential replacement with balanced ownership.
 */
#include "library.h"
#include "track_meta_cache.h"
#include "rekordbox_pdb.h"
#include "media_io_gate.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdbool.h>

static int s_failures = 0;
/* Counted so tests/run_p4_host_tests.ps1 can pin how much this suite actually
 * executes: a deleted or commented-out test lowers the number and fails the run. */
static unsigned s_checks = 0;

#define CHECK(cond)                                                            \
    do {                                                                       \
        s_checks++;                                                            \
        if (!(cond)) {                                                         \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                 \
            s_failures++;                                                      \
        }                                                                      \
    } while (0)

/* ── counting allocator for the heap-owned metadata fields ─────────────────── */
static int s_alloc_balance = 0;

static void *cnt_alloc(size_t sz)
{
    void *p = malloc(sz);
    if (p) {
        s_alloc_balance++;
    }
    return p;
}

static void cnt_free(void *p)
{
    if (p) {
        free(p);
        s_alloc_balance--;
    }
}

static void make_meta(anlz_metadata_t *m, uint16_t bpm, uint16_t beat_count, bool with_high)
{
    memset(m, 0, sizeof(*m));
    m->bpm = bpm;
    m->has_waveform_low = true;
    memset(m->waveform_low, (int)(bpm & 0xFF), sizeof(m->waveform_low));
    m->has_vbr = true;
    for (unsigned i = 0; i < ANLZ_VBR_TABLE_LEN; i++) {
        m->vbr[i] = i;
    }
    if (beat_count > 0) {
        m->beats = cnt_alloc((size_t)beat_count * sizeof(anlz_beat_t));
        m->beat_count = beat_count;
        for (uint16_t i = 0; i < beat_count; i++) {
            m->beats[i].time_ms = (uint32_t)(i + 1) * 1000u;
            m->beats[i].beat_phase = i % 4u;
            m->beats[i].bpm_x100 = (uint16_t)(bpm * 100u);
        }
    }
    if (with_high) {
        m->waveform_high_len = 64u;
        m->waveform_high = cnt_alloc(64u);
        if (m->waveform_high) {
            memset(m->waveform_high, 0xAB, 64u);
        }
    }
}

/* ── controllable stub state ───────────────────────────────────────────────── */
static int s_cache_load_calls, s_parse_dat_calls, s_parse_ext_calls, s_save_calls;
static esp_err_t s_cache_load_result = ESP_ERR_NOT_FOUND;
static esp_err_t s_parse_dat_result = ESP_OK;
static esp_err_t s_parse_ext_result = ESP_OK;
static esp_err_t s_save_result = ESP_OK;
static uint16_t s_payload_bpm = 128;
static uint16_t s_payload_beats = 4;

static void reset_state(uint16_t bpm, uint16_t beats)
{
    s_cache_load_calls = s_parse_dat_calls = s_parse_ext_calls = s_save_calls = 0;
    s_cache_load_result = ESP_ERR_NOT_FOUND;
    s_parse_dat_result = s_parse_ext_result = s_save_result = ESP_OK;
    s_payload_bpm = bpm;
    s_payload_beats = beats;
}

/* ── external symbols library.c depends on ─────────────────────────────────── */
esp_err_t track_meta_cache_load(uint32_t track_key, const char *dat_path,
                                const char *ext_path, bool include_high_waveform,
                                anlz_metadata_t *out_meta)
{
    (void)track_key; (void)dat_path; (void)ext_path;
    s_cache_load_calls++;
    memset(out_meta, 0, sizeof(*out_meta));
    if (s_cache_load_result != ESP_OK) {
        return s_cache_load_result;
    }
    make_meta(out_meta, s_payload_bpm, s_payload_beats, include_high_waveform);
    return ESP_OK;
}

esp_err_t track_meta_cache_save(uint32_t track_key, const char *dat_path,
                                const char *ext_path, const anlz_metadata_t *meta)
{
    (void)track_key; (void)dat_path; (void)ext_path; (void)meta;
    s_save_calls++;
    return s_save_result;
}

esp_err_t anlz_parse_dat(const char *dat_path, anlz_metadata_t *out)
{
    (void)dat_path;
    s_parse_dat_calls++;
    memset(out, 0, sizeof(*out));
    if (s_parse_dat_result != ESP_OK) {
        return s_parse_dat_result;   /* leaves out zeroed */
    }
    make_meta(out, s_payload_bpm, s_payload_beats, false);   /* DAT has no hi-wav */
    return ESP_OK;
}

esp_err_t anlz_parse_ext(const char *ext_path, anlz_metadata_t *meta)
{
    (void)ext_path;
    s_parse_ext_calls++;
    if (s_parse_ext_result != ESP_OK) {
        return s_parse_ext_result;
    }
    if (!meta->waveform_high) {
        meta->waveform_high_len = 64u;
        meta->waveform_high = cnt_alloc(64u);
        if (meta->waveform_high) {
            memset(meta->waveform_high, 0xAB, 64u);
        }
    }
    return ESP_OK;
}

esp_err_t anlz_clone(const anlz_metadata_t *src, anlz_metadata_t *out)
{
    if (!src || !out) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = *src;
    out->beats = NULL;
    out->waveform_high = NULL;
    if (src->beats && src->beat_count) {
        out->beats = cnt_alloc((size_t)src->beat_count * sizeof(anlz_beat_t));
        if (!out->beats) {
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->beats, src->beats, (size_t)src->beat_count * sizeof(anlz_beat_t));
    }
    if (src->waveform_high && src->waveform_high_len) {
        out->waveform_high = cnt_alloc(src->waveform_high_len);
        if (!out->waveform_high) {
            cnt_free(out->beats);
            return ESP_ERR_NO_MEM;
        }
        memcpy(out->waveform_high, src->waveform_high, src->waveform_high_len);
    }
    return ESP_OK;
}

void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) {
        return;
    }
    if (meta->beats) {
        cnt_free(meta->beats);
        meta->beats = NULL;
    }
    if (meta->waveform_high) {
        cnt_free(meta->waveform_high);
        meta->waveform_high = NULL;
    }
    meta->beat_count = 0;
    meta->waveform_high_len = 0;
}

void media_io_gate_begin(void) { }
void media_io_gate_end(void) { }

#define TEST_PDB_MAX_TRACKS 8
static pdb_track_t s_pdb_tracks[TEST_PDB_MAX_TRACKS];
static int s_pdb_track_count;
static esp_err_t s_pdb_open_result = ESP_ERR_NOT_FOUND;

static void reset_pdb_fixture(void)
{
    memset(s_pdb_tracks, 0, sizeof(s_pdb_tracks));
    s_pdb_track_count = 0;
    s_pdb_open_result = ESP_ERR_NOT_FOUND;
}

esp_err_t pdb_open(const char *pdb_path, pdb_t **out)
{
    (void)pdb_path;
    if (!out) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pdb_open_result != ESP_OK) {
        *out = NULL;
        return s_pdb_open_result;
    }
    *out = (pdb_t *)(uintptr_t)1u;
    return ESP_OK;
}
void pdb_close(pdb_t *pdb) { (void)pdb; }
int pdb_track_count(const pdb_t *pdb)
{
    return pdb ? s_pdb_track_count : 0;
}
esp_err_t pdb_get_track(const pdb_t *pdb, int index, pdb_track_t *out)
{
    if (!pdb || !out || index < 0 || index >= s_pdb_track_count) {
        return ESP_ERR_INVALID_ARG;
    }
    *out = s_pdb_tracks[index];
    return ESP_OK;
}

/* ── helpers ───────────────────────────────────────────────────────────────── */
static void make_track(library_track_t *t)
{
    memset(t, 0, sizeof(*t));
    t->track_id = 100u;
    snprintf(t->anlz_path, sizeof(t->anlz_path), "/PIONEER/USBANLZ/x/ANLZ0000.DAT");
    snprintf(t->title, sizeof(t->title), "Test");
}

static uint16_t current_bpm(void)
{
    anlz_metadata_t c;
    if (library_clone_current_anlz(&c) != ESP_OK) {
        return 0;
    }
    uint16_t bpm = c.bpm;
    anlz_free(&c);
    return bpm;
}

static uint16_t current_beats(void)
{
    anlz_metadata_t c;
    if (library_clone_current_anlz(&c) != ESP_OK) {
        return 0;
    }
    uint16_t n = c.beat_count;
    anlz_free(&c);
    return n;
}

/* ── tests ─────────────────────────────────────────────────────────────────── */
static void test_cache_hit(void)
{
    printf("== warm cache hit ==\n");
    reset_state(128, 4);
    s_cache_load_result = ESP_OK;
    library_track_t tr;
    make_track(&tr);

    CHECK(library_load_anlz(&tr) == ESP_OK);
    CHECK(s_cache_load_calls == 1);
    CHECK(s_parse_dat_calls == 0 && s_parse_ext_calls == 0 && s_save_calls == 0);
    CHECK(tr.bpm == 128 && tr.duration_ms == 4000u && tr.has_anlz == 1);
    CHECK(current_bpm() == 128 && current_beats() == 4);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void test_cache_miss(void)
{
    printf("== cold cache miss ==\n");
    reset_state(120, 8);
    s_cache_load_result = ESP_ERR_NOT_FOUND;
    library_track_t tr;
    make_track(&tr);

    CHECK(library_load_anlz(&tr) == ESP_OK);
    CHECK(s_cache_load_calls == 1);
    CHECK(s_parse_dat_calls == 1 && s_parse_ext_calls == 1 && s_save_calls == 1);
    CHECK(tr.bpm == 120 && tr.duration_ms == 8000u);
    CHECK(current_beats() == 8);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void test_cache_rejection_fallback(void)
{
    printf("== cache rejection -> parse fallback ==\n");
    reset_state(125, 5);
    s_cache_load_result = ESP_ERR_INVALID_RESPONSE;
    library_track_t tr;
    make_track(&tr);

    CHECK(library_load_anlz(&tr) == ESP_OK);
    CHECK(s_parse_dat_calls == 1 && s_save_calls == 1);
    CHECK(current_beats() == 5);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void test_write_failure_non_fatal(void)
{
    printf("== cache-write failure stays non-fatal ==\n");
    reset_state(130, 4);
    s_cache_load_result = ESP_ERR_NOT_FOUND;
    s_save_result = ESP_FAIL;
    library_track_t tr;
    make_track(&tr);

    CHECK(library_load_anlz(&tr) == ESP_OK);   /* parse OK, save failed, still OK */
    CHECK(s_save_calls == 1);
    CHECK(current_bpm() == 130);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void test_failure_retires_previous(void)
{
    printf("== parser failure retires the previous current metadata ==\n");
    reset_state(128, 4);
    s_cache_load_result = ESP_OK;
    library_track_t tr;
    make_track(&tr);
    CHECK(library_load_anlz(&tr) == ESP_OK);   /* current = A (128, 4) */
    CHECK(current_bpm() == 128 && current_beats() == 4);

    /* The caller now loads the track anyway when analysis data is unavailable,
     * so holding on to A would draw the previous track's beatgrid, waveform and
     * cues over the newly loaded one. The failed resolve must retire A instead,
     * leaving consumers on their existing "no metadata" path. */
    reset_state(200, 2);
    s_cache_load_result = ESP_ERR_NOT_FOUND;
    s_parse_dat_result = ESP_FAIL;
    library_track_t tr2;
    make_track(&tr2);
    CHECK(library_load_anlz(&tr2) == ESP_FAIL);
    CHECK(current_bpm() == 0 && current_beats() == 0);   /* A retired, not kept */

    /* Retiring must free A rather than leak it, without needing an explicit
     * library_free_current_anlz() from the caller. */
    CHECK(s_alloc_balance == 0);

    library_free_current_anlz();                /* idempotent after a retire */
    CHECK(s_alloc_balance == 0);
}

static void test_missing_anlz_path_keeps_pdb_summary(void)
{
    printf("== track with no ANLZ path keeps its PDB summary ==\n");
    reset_state(128, 4);
    s_cache_load_result = ESP_OK;

    /* A PDB row that carries no analysis file at all. The audio path, BPM and
     * duration come from the row and must survive untouched, so the caller can
     * still play the track; only has_anlz stays clear. */
    library_track_t tr;
    make_track(&tr);
    tr.anlz_path[0] = '\0';
    tr.bpm = 174;
    tr.duration_ms = 210000u;

    CHECK(library_load_anlz(&tr) == ESP_ERR_NOT_FOUND);
    CHECK(s_cache_load_calls == 0);      /* never touches cache or USB */
    CHECK(s_parse_dat_calls == 0);
    CHECK(tr.bpm == 174 && tr.duration_ms == 210000u);
    CHECK(tr.has_anlz == 0 && tr.has_waveform == 0 && tr.has_pvbr == 0);
    CHECK(current_bpm() == 0);           /* nothing published */
    CHECK(s_alloc_balance == 0);
}

static void test_sequential_replacement(void)
{
    printf("== sequential replacement frees the old object ==\n");
    reset_state(100, 4);
    s_cache_load_result = ESP_OK;
    library_track_t tr;
    make_track(&tr);
    CHECK(library_load_anlz(&tr) == ESP_OK);   /* A */

    reset_state(140, 6);
    s_cache_load_result = ESP_OK;
    CHECK(library_load_anlz(&tr) == ESP_OK);   /* B replaces A, A freed */
    CHECK(current_bpm() == 140 && current_beats() == 6);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);   /* no leak, no double free */
}

int main(void)
{
    printf("=== library_anlz tests ===\n");
    test_cache_hit();
    test_cache_miss();
    test_cache_rejection_fallback();
    test_write_failure_non_fatal();
    test_failure_retires_previous();
    test_missing_anlz_path_keeps_pdb_summary();
    test_sequential_replacement();

    if (s_failures == 0) {
        printf("library_anlz tests passed\n");
        return 0;
    }
    printf("library_anlz tests FAILED (%d)\n", s_failures);
    return 1;
}
