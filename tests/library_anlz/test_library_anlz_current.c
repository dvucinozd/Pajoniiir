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

int main(void)
{
    int rc = library_anlz_legacy_main();
    if (rc != 0) return rc;

    test_nonzero_pdb_duration_survives_anlz_enrichment();
    test_zero_pdb_duration_falls_back_to_last_beat();

    if (s_failures == 0) {
        puts("library duration policy tests passed");
        return 0;
    }
    printf("library duration policy tests FAILED (%d)\n", s_failures);
    return 1;
}
