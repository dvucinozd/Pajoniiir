#include "media_catalog.h"
#include "library.h"
#include "service_log.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static uint32_t generation = 7;
static unsigned reads, stat_calls;
static bool missing, removal, changing_file;

/* Any ANLZ/deck-load side effect makes this read-only contract fail. These
 * definitions also keep the COFF linker usable without ELF section GC. */
int library_count(void) { return 1; }
esp_err_t library_snapshot_rows(library_catalog_row_t *r, size_t c, size_t *n, uint32_t *g)
{ (void)r; (void)c; (void)n; (void)g; assert(false); return ESP_FAIL; }
esp_err_t library_get_row_key(int i, uint32_t *key)
{ (void)i; (void)key; assert(false); return ESP_FAIL; }
int library_find_row_by_key(uint32_t key) { (void)key; assert(false); return -1; }
void library_sort(int field, bool descending) { (void)field; (void)descending; assert(false); }
esp_err_t library_load_anlz(library_track_t *track) { (void)track; assert(false); return ESP_FAIL; }
void library_last_anlz_load_stats(uint32_t *ms, uint8_t *source, bool *written)
{ (void)ms; (void)source; (void)written; assert(false); }
esp_err_t library_clone_current_anlz(anlz_metadata_t *out) { (void)out; assert(false); return ESP_FAIL; }
uint32_t library_current_analysis_span_ms(uint32_t ms) { (void)ms; assert(false); return 0; }
void service_log_event(service_log_event_t e, service_log_severity_t sev, uint8_t count,
    uint32_t a, uint32_t b, uint32_t c, uint32_t d, const char *text)
{ (void)e; (void)sev; (void)count; (void)a; (void)b; (void)c; (void)d; (void)text; assert(false); }

uint32_t library_generation(void) { return generation; }
uint32_t library_track_key(const library_track_t *t) { return t->track_id; }
esp_err_t library_export_digest(uint8_t out[32], uint32_t *gen)
{
    memset(out, 0x12, 32); *gen = generation; return ESP_OK;
}
esp_err_t library_get(int index, library_track_t *out)
{
    reads++;
    if (index != 0) return ESP_ERR_NOT_FOUND;
    memset(out, 0, sizeof(*out));
    out->track_id = 123;
    strcpy(out->path, "/Contents/audio.wav");
    strcpy(out->title, "Migration title");
    return ESP_OK;
}
void media_io_gate_begin(void) {}
void media_io_gate_end(void) {}
bool media_io_gate_is_available(void) { return true; }
int stat(const char *path, struct stat *out)
{
    assert(strcmp(path, "/usb/Contents/audio.wav") == 0);
    stat_calls++;
    if (missing) return -1;
    memset(out, 0, sizeof(*out));
    out->st_size = 44100;
    out->st_mtime = 1700000000 + (changing_file && stat_calls == 2 ? 2 : 0);
    if (removal && stat_calls == 2) generation++;
    return 0;
}

int main(void)
{
    media_catalog_identity_record_t out;
    assert(media_catalog_identity_record(-1, 7, &out) == ESP_ERR_INVALID_ARG);
    assert(media_catalog_identity_record(0, 6, &out) == ESP_ERR_INVALID_STATE);
    assert(reads == 0 && stat_calls == 0);
    assert(media_catalog_identity_record(0, 7, &out) == ESP_OK);
    assert(out.track_key == 123 && out.file_size == 44100 && out.mtime == 1700000000);
    assert(out.persistent_id.valid && !media_catalog_load_in_progress());
    media_persistent_id_t expected;
    uint8_t digest[32]; memset(digest, 0x12, 32);
    assert(media_persistent_id_derive(digest, out.path, out.file_size, out.mtime, &expected));
    assert(media_persistent_id_equal(&expected, &out.persistent_id));
    missing = true;
    assert(media_catalog_identity_record(0, 7, &out) == ESP_ERR_NOT_FOUND);
    assert(!out.persistent_id.valid && out.track_key == 123);
    missing = false; changing_file = true; stat_calls = 0;
    assert(media_catalog_identity_record(0, 7, &out) == ESP_ERR_INVALID_STATE);
    assert(!out.persistent_id.valid);
    changing_file = false; removal = true; stat_calls = 0;
    assert(media_catalog_identity_record(0, 7, &out) == ESP_ERR_INVALID_STATE);
    assert(!out.persistent_id.valid);
    puts("Production catalog identity: stable metadata, missing files and media replacement PASS");
    return 0;
}
