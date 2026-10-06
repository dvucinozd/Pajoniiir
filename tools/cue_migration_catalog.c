/* Export old Rekordbox rows using the production parser. Parser diagnostics
 * remain on stdout; machine-readable output is a separate file. */
#include "rekordbox_pdb.h"
#include <stdio.h>
#include <stdint.h>

static void json_string(FILE *f, const char *s)
{
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') { fputc('\\', f); fputc(*p, f); }
        else if (*p < 32) fprintf(f, "\\u%04x", *p);
        else fputc(*p, f);
    }
    fputc('"', f);
}

int main(int argc, char **argv)
{
    if (argc != 3) { fprintf(stderr, "usage: cue_migration_catalog export.pdb output.json\n"); return 2; }
    pdb_t *pdb = NULL;
    if (pdb_open(argv[1], &pdb) != ESP_OK) return 3;
    pdb_import_stats_t stats;
    pdb_get_import_stats(pdb, &stats);
    if (stats.tracks_truncated || stats.names_truncated || pdb_track_count(pdb) == 0) {
        pdb_close(pdb); return 4;
    }
    FILE *f = fopen(argv[2], "wb");
    if (!f) { pdb_close(pdb); return 5; }
    fputs("[", f);
    for (int i = 0; i < pdb_track_count(pdb); i++) {
        pdb_track_t t;
        if (pdb_get_track(pdb, i, &t) != ESP_OK) { fclose(f); pdb_close(pdb); return 6; }
        uint32_t key = t.track_id;
        if (!key) {
            key = 2166136261u;
            for (const unsigned char *p = (const unsigned char *)t.file_path; *p; p++) key = (key ^ *p) * 16777619u;
            if (!key) key = 1;
        }
        fprintf(f, "%s{\"legacy_key\":%u,\"track_id\":%u,\"path\":", i ? "," : "", key, t.track_id);
        json_string(f, t.file_path);
        fputs(",\"title\":", f); json_string(f, t.title); fputs("}", f);
    }
    fputs("]\n", f);
    int failed = ferror(f);
    if (fclose(f) != 0) failed = 1;
    pdb_close(pdb);
    return failed ? 7 : 0;
}
