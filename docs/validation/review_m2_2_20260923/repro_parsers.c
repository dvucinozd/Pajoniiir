/* Review probe: prints observed defects; exit 0 is not a regression PASS. */
#include "rekordbox_anlz.h"
#include "p4_ota_pull_manifest.h"
#include <stdio.h>
#include <string.h>

static void be32(FILE *f, unsigned v) {
    fputc(v >> 24, f); fputc(v >> 16, f); fputc(v >> 8, f); fputc(v, f);
}
static void section(FILE *f, const char *tag, unsigned header, unsigned size) {
    fwrite(tag, 1, 4, f); be32(f, header); be32(f, size);
}
static void fixture(const char *name, int tail) {
    FILE *f = fopen(name, "wb");
    const char *path = "/Contents/test.mp3";
    unsigned path_bytes = (unsigned)(strlen(path) + 1) * 2;
    unsigned file_size = 28 + 16 + path_bytes + 12 + 24 + 8 + 20 + 400 + 24 + 56;
    section(f, "PMAI", 28, file_size + tail);
    for (int i = 0; i < 16; i++) fputc(0, f);
    section(f, "PPTH", 16, 16 + path_bytes); be32(f, path_bytes);
    for (size_t i = 0; i <= strlen(path); i++) { fputc(0, f); fputc(path[i], f); }
    section(f, "PVBR", 12, 12);
    section(f, "PQTZ", 24, 32); be32(f, 0); be32(f, 0x80000); be32(f, 1);
    fputc(0, f); fputc(1, f); fputc(0x2e, f); fputc(0xe0, f); be32(f, 0);
    section(f, "PWAV", 20, 420); be32(f, 400); be32(f, 0);
    for (int i = 0; i < 400; i++) fputc(1, f);
    section(f, "PCOB", 24, 80); be32(f, 1); be32(f, 1); be32(f, 0);
    /* Deep Symmetry ANLZ PCPT: header=28, length=56, hot cue A=1. */
    section(f, "PCPT", 28, 56); be32(f, 1); be32(f, 0); be32(f, 0x100000);
    be32(f, 0xffff0001); be32(f, 0x010003e8); be32(f, 12345); be32(f, 0);
    for (int i = 0; i < 16; i++) fputc(0, f);
    if (tail) fputc('X', f);
    fclose(f);
}
int main(void) {
    anlz_metadata_t m = {0};
    fixture("real-layout.dat", 0);
    int rc = anlz_parse_dat("real-layout.dat", &m);
    printf("PCPT_SPEC rc=%d cues=%u (expected 1 at 12345 ms)\n", rc, m.cue_count);
    anlz_free(&m);
    fixture("malformed-tail.dat", 1);
    rc = anlz_parse_dat("malformed-tail.dat", &m);
    printf("ANLZ_TRAILING_PARTIAL rc=%d (expected rejection)\n", rc);
    anlz_free(&m);
    const char *docs[] = {
        "{\"schema_version\":1,\"release\":\"M2.3\",\"p4\":{\"url\":\"fw.ddjota\",\"size\":200,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}}",
        "{\"schema_version\":1,\"release\":\"M2.3\",\"release\":\"M9\",\"p4\":{\"url\":\"fw.ddjota\",\"size\":200garbage,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}}",
        "{\"schema_version\":1,\"release\":\"M2.3\",\"p4\":{\"url\":\"fw.ddjota\",\"size\":200,\"sha256\":\"0000000000000000000000000000000000000000000000000000000000000000\"}"
    };
    for (int i = 0; i < 3; i++) {
        p4_ota_pull_manifest_t manifest;
        rc = p4_ota_pull_manifest_parse(docs[i], strlen(docs[i]), &manifest);
        printf("OTA_JSON case=%d rc=%d release=%s size=%u\n", i, rc, manifest.release, manifest.size);
    }
    return 0;
}
