#include "track_meta_cache.h"
#include "library_load_trace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int s_failed;
#define CHECK(ok) do { if (!(ok)) { fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #ok); ++s_failed; } } while (0)

void library_load_trace_mark(library_load_phase_t phase, uint32_t key)
{ (void)phase; (void)key; }
void sd_io_gate_begin(void) {}
void sd_io_gate_end(void) {}
void media_io_gate_begin(void) {}
void media_io_gate_end(void) {}
void anlz_free(anlz_metadata_t *meta)
{
    if (!meta) return;
    free(meta->beats);
    free(meta->waveform_high);
    free(meta->color_preview);
    meta->beats = NULL;
    meta->waveform_high = NULL;
    meta->color_preview = NULL;
}

int main(void)
{
#if defined(_WIN32)
    (void)mkdir("tc");
#else
    (void)mkdir("tc", 0775);
#endif
    FILE *source = fopen("tc/track.dat", "wb");
    CHECK(source != NULL);
    if (!source) return 1;
    fputs("DAT", source);
    fclose(source);
    source = fopen("tc/track.ext", "wb");
    CHECK(source != NULL);
    if (!source) return 1;
    fputs("EXT", source);
    fclose(source);

    media_persistent_id_t id = {.valid = true};
    memset(id.bytes, 0x11, sizeof(id.bytes));
    anlz_beat_t beat = {.beat_phase = 1, .time_ms = 250};
    uint8_t high[3] = {4, 5, 6};
    uint8_t color[12] = {20, 0, 0, 40, 1, 2, 30, 0, 0, 1, 40, 2};
    anlz_metadata_t meta = {0};
    meta.bpm = 128;
    meta.beats = &beat;
    meta.beat_count = 1;
    meta.cues[0] = (anlz_cue_t){.type = ANLZ_CUE_SINGLE, .start_ms = 100};
    meta.cue_count = 1;
    meta.memory_cues[0] = (anlz_cue_t){.type = ANLZ_CUE_SINGLE, .start_ms = 100};
    meta.memory_cues[1] = (anlz_cue_t){.type = ANLZ_CUE_LOOP, .index = 1,
                                       .start_ms = 100, .end_ms = 200};
    meta.memory_cue_count = 2;
    meta.memory_cues_truncated = true;
    meta.waveform_high = high;
    meta.waveform_high_len = sizeof(high);
    meta.waveform_span_ms = (uint32_t)(sizeof(high) * 1000u / 150u);
    meta.color_preview = color;
    meta.color_preview_len = sizeof(color);
    meta.color_preview_truncated = true;

    meta.waveform_span_ms++;
    CHECK(track_meta_cache_save(42, &id, "tc/track.dat", "tc/track.ext", &meta) == ESP_ERR_INVALID_ARG);
    meta.waveform_span_ms--;
    meta.waveform_high = NULL;
    CHECK(track_meta_cache_save(42, &id, "tc/track.dat", "tc/track.ext", &meta) == ESP_ERR_INVALID_ARG);
    meta.waveform_high = high;
    CHECK(track_meta_cache_save(42, &id, "tc/track.dat", "tc/track.ext", &meta) == ESP_OK);
    anlz_metadata_t loaded = {0};
    CHECK(track_meta_cache_load(42, &id, "tc/track.dat", "tc/track.ext",
                                true, &loaded) == ESP_OK);
    CHECK(loaded.cue_count == 1 && loaded.cues[0].start_ms == 100);
    CHECK(loaded.memory_cue_count == 2 && loaded.memory_cues_truncated &&
          loaded.memory_cues[0].start_ms == loaded.memory_cues[1].start_ms &&
          loaded.memory_cues[1].end_ms == 200);
    CHECK(loaded.color_preview_len == sizeof(color) &&
          loaded.color_preview_truncated && loaded.color_preview &&
          memcmp(loaded.color_preview, color, sizeof(color)) == 0);
    CHECK(loaded.waveform_high_len == sizeof(high) && loaded.waveform_high &&
          memcmp(loaded.waveform_high, high, sizeof(high)) == 0);
    CHECK(loaded.waveform_span_ms == meta.waveform_span_ms);
    anlz_free(&loaded);

    anlz_metadata_t preview_only = {0};
    CHECK(track_meta_cache_load(42, &id, "tc/track.dat", "tc/track.ext",
                                false, &preview_only) == ESP_OK);
    CHECK(preview_only.waveform_high == NULL &&
          preview_only.color_preview_len == sizeof(color) &&
          preview_only.color_preview &&
          memcmp(preview_only.color_preview, color, sizeof(color)) == 0);
    CHECK(preview_only.waveform_span_ms == meta.waveform_span_ms);
    anlz_free(&preview_only);

    source = fopen("tc/track.dat", "ab");
    CHECK(source != NULL);
    if (source) { fputc('X', source); fclose(source); }
    CHECK(track_meta_cache_load(42, &id, "tc/track.dat", "tc/track.ext",
                                true, &loaded) == ESP_ERR_INVALID_RESPONSE);

    char digest[65];
    memset(digest, '1', 64);
    digest[64] = '\0';
    char dir[96], path[112];
    snprintf(dir, sizeof(dir), "tc/meta/v4/%s", digest);
    snprintf(path, sizeof(path), "%s/meta.bin", dir);
    remove(path);
    rmdir(dir);
    rmdir("tc/meta/v4");
    rmdir("tc/meta");
    remove("tc/track.dat");
    remove("tc/track.ext");
    rmdir("tc");

    if (s_failed) return 1;
    puts("track_meta_cache v5 tests passed");
    return 0;
}
