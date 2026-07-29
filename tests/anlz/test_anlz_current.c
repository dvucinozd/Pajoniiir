#define main anlz_legacy_main
#include "test_anlz.c"
#undef main

static const char *TRUNC_DAT = "test_truncated.dat";
static const char *TRUNC_EXT = "test_truncated.ext";

static uint8_t *read_entire_file(const char *path, size_t *out_len)
{
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    if (fseek(fp, 0, SEEK_END) != 0) { fclose(fp); return NULL; }
    long length = ftell(fp);
    if (length < 0 || fseek(fp, 0, SEEK_SET) != 0) { fclose(fp); return NULL; }
    uint8_t *data = malloc((size_t)length);
    if (!data && length > 0) { fclose(fp); return NULL; }
    if (length > 0 && fread(data, 1, (size_t)length, fp) != (size_t)length) {
        free(data);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *out_len = (size_t)length;
    return data;
}

static bool write_prefix(const char *path, const uint8_t *data, size_t length)
{
    FILE *fp = fopen(path, "wb");
    if (!fp) return false;
    bool ok = length == 0u || fwrite(data, 1, length, fp) == length;
    ok = fclose(fp) == 0 && ok;
    return ok;
}

static bool dat_truncation_corpus_rejects_partial_structures(void)
{
    /* Generated layout: PMAI 0..27, PPTH 28..101, PVBR 102..129,
     * PQTZ 130..157, PWAV 158..569, PCOB 570..693. */
    static const size_t cuts[] = {
        0u, 1u, 4u, 8u, 11u, 27u,
        29u, 32u, 35u, 39u, 47u, 49u, 101u,
        103u, 106u, 109u, 113u, 129u,
        131u, 134u, 137u, 141u, 149u, 157u,
        159u, 162u, 165u, 169u, 300u, 569u,
        571u, 574u, 577u, 581u, 600u, 693u,
    };

    build_synthetic_dat();
    size_t full_len = 0u;
    uint8_t *full = read_entire_file(SYNTH_DAT, &full_len);
    if (!full || full_len != 694u) {
        fprintf(stderr, "unexpected synthetic DAT length: %zu\n", full_len);
        free(full);
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        if (!write_prefix(TRUNC_DAT, full, cuts[i])) {
            ok = false;
            break;
        }
        anlz_metadata_t out;
        memset(&out, 0xA5, sizeof(out));
        esp_err_t rc = anlz_parse_dat(TRUNC_DAT, &out);
        if (rc != ESP_ERR_INVALID_SIZE || out.audio_path[0] != '\0' ||
            out.beats != NULL || out.beat_count != 0u ||
            out.waveform_high != NULL || out.cue_count != 0u) {
            fprintf(stderr, "DAT truncation cut %zu returned %d or published partial metadata\n",
                    cuts[i], (int)rc);
            if (rc == ESP_OK) anlz_free(&out);
            ok = false;
            break;
        }
    }

    free(full);
    remove(TRUNC_DAT);
    remove(SYNTH_DAT);
    return ok;
}

static bool ext_truncation_corpus_retains_previous_metadata(void)
{
    static const size_t cuts[] = {
        0u, 1u, 4u, 8u, 11u, 12u, 13u, 64u, 400u, 811u,
    };

    build_synthetic_dat();
    build_synthetic_ext();

    anlz_metadata_t meta = {0};
    if (anlz_parse_dat(SYNTH_DAT, &meta) != ESP_OK) return false;
    anlz_beat_t *const original_beats = meta.beats;
    const uint16_t original_beat_count = meta.beat_count;
    char original_path[ANLZ_PATH_MAX];
    memcpy(original_path, meta.audio_path, sizeof(original_path));

    size_t full_len = 0u;
    uint8_t *full = read_entire_file(SYNTH_EXT, &full_len);
    if (!full || full_len != 812u) {
        fprintf(stderr, "unexpected synthetic EXT length: %zu\n", full_len);
        free(full);
        anlz_free(&meta);
        return false;
    }

    bool ok = true;
    for (size_t i = 0; i < sizeof(cuts) / sizeof(cuts[0]); ++i) {
        if (!write_prefix(TRUNC_EXT, full, cuts[i])) {
            ok = false;
            break;
        }
        esp_err_t rc = anlz_parse_ext(TRUNC_EXT, &meta);
        if (rc != ESP_ERR_INVALID_SIZE || meta.beats != original_beats ||
            meta.beat_count != original_beat_count ||
            strcmp(meta.audio_path, original_path) != 0 ||
            meta.waveform_high != NULL || meta.waveform_high_len != 0u) {
            fprintf(stderr, "EXT truncation cut %zu returned %d or replaced metadata\n",
                    cuts[i], (int)rc);
            ok = false;
            break;
        }
    }

    free(full);
    anlz_free(&meta);
    remove(TRUNC_EXT);
    remove(SYNTH_DAT);
    remove(SYNTH_EXT);
    return ok;
}

int main(int argc, char *argv[])
{
    int rc = anlz_legacy_main(argc, argv);
    if (argc >= 2 || rc != 0) return rc;

    printf("\n=== Strict truncation corpus ===\n");
    TEST("DAT header/section/payload truncations rejected transactionally");
    CHECK(dat_truncation_corpus_rejects_partial_structures(), "DAT truncation corpus failed");

    TEST("EXT truncations retain the previously published metadata");
    CHECK(ext_truncation_corpus_retains_previous_metadata(), "EXT truncation corpus failed");

    remove(SYNTH_UNICODE_DAT);
    printf("TESTS_RUN=%d\n", g_tests_run);
    printf("\nStrict corpus result: %d/%d total tests passed\n",
           g_tests_passed, g_tests_run);
    return g_tests_failed > 0 ? 1 : 0;
}
