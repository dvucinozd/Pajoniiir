#define main library_anlz_legacy_main
#include "test_library_anlz.c"
#undef main

static void test_nonzero_pdb_duration_survives_anlz_enrichment(void)
{
    printf("== nonzero PDB/audio duration survives ANLZ enrichment ==\n");
    reset_state(126, 5);
    s_cache_load_result = ESP_OK;

    library_track_t track;
    make_track(&track);
    track.duration_ms = 213456u;

    CHECK(library_load_anlz(&track) == ESP_OK);
    CHECK(track.bpm == 126);
    CHECK(track.duration_ms == 213456u);
    CHECK(track.has_anlz == 1);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void test_zero_pdb_duration_falls_back_to_last_beat(void)
{
    printf("== missing PDB/audio duration falls back to final beat ==\n");
    reset_state(122, 7);
    s_cache_load_result = ESP_ERR_NOT_FOUND;

    library_track_t track;
    make_track(&track);
    track.duration_ms = 0u;

    CHECK(library_load_anlz(&track) == ESP_OK);
    CHECK(track.bpm == 122);
    CHECK(track.duration_ms == 7000u);
    CHECK(track.has_anlz == 1);

    library_free_current_anlz();
    CHECK(s_alloc_balance == 0);
}

static void set_pdb_track(int slot, uint32_t id, const char *title,
                          const char *artist, const char *key, uint16_t bpm)
{
    CHECK(slot >= 0 && slot < TEST_PDB_MAX_TRACKS);
    if (slot < 0 || slot >= TEST_PDB_MAX_TRACKS) {
        return;
    }
    pdb_track_t *track = &s_pdb_tracks[slot];
    memset(track, 0, sizeof(*track));
    track->track_id = id;
    track->bpm = bpm;
    track->duration_s = (uint16_t)(180 + slot);
    snprintf(track->title, sizeof(track->title), "%s", title);
    snprintf(track->artist, sizeof(track->artist), "%s", artist);
    snprintf(track->key, sizeof(track->key), "%s", key);
    snprintf(track->file_path, sizeof(track->file_path), "/Contents/%lu.wav",
             (unsigned long)id);
}

static uint32_t row_track_id(int row)
{
    library_track_t track;
    CHECK(library_get(row, &track) == ESP_OK);
    return track.track_id;
}

static void check_row_ids(const uint32_t *expected, int count)
{
    CHECK(library_count() == count);
    for (int row = 0; row < count; ++row) {
        CHECK(row_track_id(row) == expected[row]);
    }
}

static void test_sort_republishes_compact_order_only(void)
{
    printf("== immutable track store with compact sort order ==\n");
    library_clear();
    reset_pdb_fixture();
    s_pdb_open_result = ESP_OK;
    s_pdb_track_count = 5;
    set_pdb_track(0, 1001u, "Zulu",  "Beta",  "10A", 128u);
    set_pdb_track(1, 1002u, "Alpha", "Gamma", "2A",  122u);
    set_pdb_track(2, 1003u, "Echo",  "Alpha", "8B",  130u);
    set_pdb_track(3, 1004u, "Bravo", "Alpha", "8A",  118u);
    set_pdb_track(4, 1005u, "Delta", "Delta", "",    124u);

    CHECK(library_init() == ESP_OK);
    const uint32_t initial[] = {1001u, 1002u, 1003u, 1004u, 1005u};
    check_row_ids(initial, 5);
    library_track_t *track_1001 = library_get_ptr(0);
    CHECK(track_1001 != NULL && track_1001->track_id == 1001u);

    uint32_t generation = library_generation();
    library_sort(1, false); /* title ascending */
    CHECK(library_generation() == generation + 1u);
    const uint32_t title_asc[] = {1002u, 1004u, 1005u, 1003u, 1001u};
    check_row_ids(title_asc, 5);
    CHECK(library_get_ptr(4) == track_1001);

    generation = library_generation();
    library_sort(0, false); /* artist ascending, title tie-break */
    CHECK(library_generation() == generation + 1u);
    const uint32_t artist_asc[] = {1004u, 1003u, 1001u, 1005u, 1002u};
    check_row_ids(artist_asc, 5);
    CHECK(library_get_ptr(2) == track_1001);

    generation = library_generation();
    library_sort(2, true); /* BPM descending */
    CHECK(library_generation() == generation + 1u);
    const uint32_t bpm_desc[] = {1003u, 1001u, 1005u, 1002u, 1004u};
    check_row_ids(bpm_desc, 5);
    CHECK(library_get_ptr(1) == track_1001);

    generation = library_generation();
    library_sort(3, false); /* numeric Camelot ordering; empty last */
    CHECK(library_generation() == generation + 1u);
    const uint32_t key_asc[] = {1002u, 1004u, 1003u, 1001u, 1005u};
    check_row_ids(key_asc, 5);
    CHECK(library_get_ptr(3) == track_1001);

    generation = library_generation();
    library_sort(99, false);
    CHECK(library_generation() == generation); /* unsupported sort is a no-op */
    check_row_ids(key_asc, 5);

    library_clear();
    reset_pdb_fixture();
}

/* Identity accessors must agree with library_get()/library_track_key() on every
 * row and follow the row order across sorts — they exist so highlight, selection
 * and load-by-identity lookups stop copying a ~2.9 KB record per candidate row. */
static void test_identity_accessors_track_row_order(void)
{
    printf("== identity accessors resolve rows without record copies ==\n");
    library_clear();
    reset_pdb_fixture();
    s_pdb_open_result = ESP_OK;
    s_pdb_track_count = 5;
    set_pdb_track(0, 1001u, "Zulu",  "Beta",  "10A", 128u);
    set_pdb_track(1, 1002u, "Alpha", "Gamma", "2A",  122u);
    set_pdb_track(2, 1003u, "Echo",  "Alpha", "8B",  130u);
    set_pdb_track(3, 1004u, "Bravo", "Alpha", "8A",  118u);
    set_pdb_track(4, 1005u, "Delta", "Delta", "",    124u);
    CHECK(library_init() == ESP_OK);

    for (int row = 0; row < 5; ++row) {
        uint32_t key = 0u;
        CHECK(library_get_row_key(row, &key) == ESP_OK);
        CHECK(key == row_track_id(row));
        CHECK(library_find_row_by_key(key) == row);
    }

    /* Out-of-range rows report NOT_FOUND and never leave a stale key behind. */
    uint32_t key = 0xDEADBEEFu;
    CHECK(library_get_row_key(-1, &key) == ESP_ERR_NOT_FOUND);
    CHECK(key == 0u);
    key = 0xDEADBEEFu;
    CHECK(library_get_row_key(5, &key) == ESP_ERR_NOT_FOUND);
    CHECK(key == 0u);
    CHECK(library_get_row_key(0, NULL) == ESP_ERR_INVALID_ARG);

    /* Unknown and zero keys resolve to "no row" rather than row 0. */
    CHECK(library_find_row_by_key(0u) == -1);
    CHECK(library_find_row_by_key(4242u) == -1);

    /* A sort moves rows but not identities: the same key must resolve to its new
     * row, which is exactly what preserve-selection-after-sort depends on. */
    library_sort(1, false); /* title ascending */
    const uint32_t title_asc[] = {1002u, 1004u, 1005u, 1003u, 1001u};
    for (int row = 0; row < 5; ++row) {
        uint32_t sorted_key = 0u;
        CHECK(library_get_row_key(row, &sorted_key) == ESP_OK);
        CHECK(sorted_key == title_asc[row]);
        CHECK(library_find_row_by_key(title_asc[row]) == row);
    }

    /* A cleared catalog resolves nothing at all. */
    library_clear();
    CHECK(library_find_row_by_key(1001u) == -1);
    CHECK(library_get_row_key(0, &key) == ESP_ERR_NOT_FOUND);

    reset_pdb_fixture();
}

int main(void)
{
    int rc = library_anlz_legacy_main();
    if (rc != 0) return rc;

    test_nonzero_pdb_duration_survives_anlz_enrichment();
    test_zero_pdb_duration_falls_back_to_last_beat();
    test_sort_republishes_compact_order_only();
    test_identity_accessors_track_row_order();

    printf("TESTS_RUN=%u\n", s_checks);
    if (s_failures == 0) {
        puts("library duration and compact-order tests passed");
        return 0;
    }
    printf("library duration and compact-order tests FAILED (%d)\n", s_failures);
    return 1;
}
