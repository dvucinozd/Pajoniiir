// Host test for dj_link_pdb: a synthetic export.pdb (two chained Tracks
// pages, >16 rows on a page, a deleted row, short-ASCII and UTF-16LE strings)
// looked up by id, then cross-checked row by row against
// library/rekordbox_pdb.c built with REKORDBOX_PDB_STANDALONE_TEST.
#define _DEFAULT_SOURCE
#include "dj_link_pdb.h"
#include "rekordbox_pdb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
            failures++;                                                    \
        }                                                                  \
    } while (0)

#define PAGE 4096u
#define PAGES 5u

static uint8_t s_pdb[PAGE * PAGES];

static void wr16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void wr32(uint8_t *p, uint32_t v)
{
    wr16(p, (uint16_t)v);
    wr16(p + 2, (uint16_t)(v >> 16));
}

typedef struct {
    uint8_t *page;
    size_t heap;        // next free heap byte (relative to the heap start)
    uint32_t nrows;
    uint16_t offs[64];
    bool present[64];
} page_builder_t;

// Short ASCII DeviceSQL string; returns its length.
static size_t put_ascii(uint8_t *dst, const char *s)
{
    size_t n = strlen(s);
    dst[0] = (uint8_t)(((n + 1u) << 1) | 1u);
    memcpy(dst + 1, s, n);
    return n + 1u;
}

// Long UTF-16LE DeviceSQL string from ASCII / Latin-1 bytes ('\xE9' = é).
static size_t put_utf16(uint8_t *dst, const char *s)
{
    size_t n = strlen(s);
    size_t i;
    dst[0] = 0x90;
    wr16(dst + 1, (uint16_t)(4u + 2u * n));
    dst[3] = 0;
    for (i = 0; i < n; i++) {
        wr16(dst + 4 + 2 * i, (uint8_t)s[i]);
    }
    return 4u + 2u * n;
}

static void add_track(page_builder_t *b, uint32_t id, bool present, uint32_t bpm100,
                      uint16_t dur, const char *title, const char *filename,
                      const char *path, bool path_utf16, const char *anlz)
{
    uint8_t *row = b->page + 0x28 + b->heap;
    size_t pos = 0x88; // strings after the fixed part
    memset(row, 0, 0x88);
    wr16(row, 0x0024);
    wr32(row + 0x38, bpm100);
    wr32(row + 0x48, id);
    wr16(row + 0x54, dur);
    wr16(row + 0x5E + 2 * 14, (uint16_t)pos);
    pos += put_ascii(row + pos, anlz);
    if (title[0]) {
        wr16(row + 0x5E + 2 * 17, (uint16_t)pos);
        pos += put_ascii(row + pos, title);
    } else {
        wr16(row + 0x5E + 2 * 17, (uint16_t)pos);
        row[pos++] = 0x03; // short ASCII, empty
    }
    wr16(row + 0x5E + 2 * 18, (uint16_t)pos);
    pos += put_ascii(row + pos, "X"); /* distinct unknown string, never title */
    wr16(row + 0x5E + 2 * 19, (uint16_t)pos);
    pos += put_ascii(row + pos, filename);
    wr16(row + 0x5E + 2 * 20, (uint16_t)pos);
    pos += path_utf16 ? put_utf16(row + pos, path) : put_ascii(row + pos, path);
    b->offs[b->nrows] = (uint16_t)b->heap;
    b->present[b->nrows] = present;
    b->nrows++;
    b->heap += (pos + 3u) & ~3u;
    // Heap and row-slot groups must not meet.
    if (0x28 + b->heap + 4u * (b->nrows / 16u + 1u) + 2u * b->nrows > PAGE) {
        printf("fixture page overflow\n");
        exit(2);
    }
}

static void finish_page(page_builder_t *b, uint32_t next)
{
    size_t ptr = PAGE;
    uint32_t idx = 0;
    wr32(b->page + 0x0C, next);
    wr32(b->page + 0x18, b->nrows); // nrows in the low 13 bits
    while (idx < b->nrows) {
        uint32_t m = b->nrows - idx < 16u ? b->nrows - idx : 16u;
        uint16_t rowpf = 0;
        uint32_t i;
        for (i = 0; i < m; i++) {
            wr16(b->page + ptr - 4 - 2 * (i + 1), b->offs[idx + i]);
            if (b->present[idx + i]) {
                rowpf |= (uint16_t)(1u << i);
            }
        }
        wr16(b->page + ptr - 4, rowpf);
        ptr -= 4u + 2u * m;
        idx += m;
    }
}

static void build_pdb(uint32_t page2_next)
{
    page_builder_t b;
    char title[32];
    char path[64];
    uint32_t i;

    memset(s_pdb, 0, sizeof(s_pdb));
    wr32(s_pdb + 4, PAGE);
    wr32(s_pdb + 8, 2);
    // Table 0: artists (type 2) at page 4 (empty), table 1: tracks 1 -> 2.
    wr32(s_pdb + 0x1C + 0, 2);
    wr32(s_pdb + 0x1C + 8, 4);
    wr32(s_pdb + 0x1C + 16 + 0, 0);
    wr32(s_pdb + 0x1C + 16 + 8, 1);
    wr32(s_pdb + 0x1C + 16 + 12, 2);
    wr32(s_pdb + 4 * PAGE + 0x0C, 0xFFFFFFFFu);

    memset(&b, 0, sizeof(b));
    b.page = s_pdb + PAGE;
    for (i = 0; i < 18; i++) {
        snprintf(title, sizeof(title), "Track %u", (unsigned)(100 + i));
        snprintf(path, sizeof(path), "/Contents/A/T%u.mp3", (unsigned)(100 + i));
        add_track(&b, 100 + i, i != 5, 12000 + i * 10, (uint16_t)(180 + i), title, "t.mp3", path,
                  false, "/PIONEER/USBANLZ/P000/0000/ANLZ0000.DAT");
    }
    finish_page(&b, 2);

    memset(&b, 0, sizeof(b));
    b.page = s_pdb + 2 * PAGE;
    for (i = 0; i < 12; i++) {
        snprintf(title, sizeof(title), "B%u", (unsigned)(200 + i));
        snprintf(path, sizeof(path), "\\Contents\\Caf\xE9\\B%u.flac", (unsigned)(200 + i));
        add_track(&b, 200 + i, true, 17350, (uint16_t)(300 + i), i == 11 ? "" : title,
                  "fallback.flac", path, true, "/PIONEER/USBANLZ/P001/0000/ANLZ0000.DAT");
    }
    finish_page(&b, page2_next);
}

typedef struct {
    int reads;
    int fail_after;  // 0 = never
} reader_t;

static bool mem_read(void *ctx, uint32_t offset, uint8_t *dst, size_t len)
{
    reader_t *r = (reader_t *)ctx;
    r->reads++;
    if (r->fail_after != 0 && r->reads > r->fail_after) {
        return false;
    }
    if ((size_t)offset + len > sizeof(s_pdb)) {
        return false;
    }
    memcpy(dst, s_pdb + offset, len);
    return true;
}

static uint8_t s_page_buf[PAGE];

static dj_link_pdb_result_t find(uint32_t id, dj_link_pdb_track_t *t, reader_t *r)
{
    return dj_link_pdb_find_track(mem_read, r, sizeof(s_pdb), id, s_page_buf, sizeof(s_page_buf), t);
}

static void test_lookup(void)
{
    dj_link_pdb_track_t t;
    reader_t r = { 0, 0 };

    build_pdb(0xFFFFFFFFu);
    CHECK(find(100, &t, &r) == DJ_LINK_PDB_FOUND);
    CHECK(strcmp(t.file_path, "/Contents/A/T100.mp3") == 0);
    CHECK(strcmp(t.title, "Track 100") == 0);
    CHECK(t.bpm100 == 12000 && t.duration_s == 180);
    CHECK(strcmp(t.anlz_path, "/PIONEER/USBANLZ/P000/0000/ANLZ0000.DAT") == 0);

    // Row 17 is in the second row group of page 1.
    CHECK(find(117, &t, &r) == DJ_LINK_PDB_FOUND);
    CHECK(strcmp(t.file_path, "/Contents/A/T117.mp3") == 0);

    // Page 2: UTF-16LE path, backslashes to '/', é to UTF-8.
    r.reads = 0;
    CHECK(find(211, &t, &r) == DJ_LINK_PDB_FOUND);
    CHECK(strcmp(t.file_path, "/Contents/Caf\xC3\xA9/B211.flac") == 0);
    CHECK(strcmp(t.title, "fallback.flac") == 0); // empty title
    CHECK(t.bpm100 == 17350 && t.duration_s == 311);
    CHECK(r.reads == 4); // header, table pointers, page 1, page 2

    // Deleted row and unknown id.
    CHECK(find(105, &t, &r) == DJ_LINK_PDB_NOT_FOUND);
    CHECK(find(999, &t, &r) == DJ_LINK_PDB_NOT_FOUND);
    CHECK(find(0, &t, &r) == DJ_LINK_PDB_NOT_FOUND);
}

static void test_errors(void)
{
    dj_link_pdb_track_t t;
    reader_t r = { 0, 0 };
    uint8_t small[1024];

    build_pdb(0xFFFFFFFFu);
    CHECK(dj_link_pdb_find_track(mem_read, &r, sizeof(s_pdb), 100, small, sizeof(small), &t) ==
          DJ_LINK_PDB_BUFFER_SMALL);
    CHECK(dj_link_pdb_find_track(mem_read, &r, 20, 100, s_page_buf, sizeof(s_page_buf), &t) ==
          DJ_LINK_PDB_BAD_FILE);

    r.reads = 0;
    r.fail_after = 3; // header, table pointers and page 1 succeed; page 2 fails
    CHECK(find(200, &t, &r) == DJ_LINK_PDB_READ_ERROR);
    r.fail_after = 0;

    // A next-page loop (2 -> 1) ends after every page was read once.
    build_pdb(1);
    r.reads = 0;
    CHECK(find(999, &t, &r) == DJ_LINK_PDB_NOT_FOUND);
    CHECK(r.reads <= 2 + (int)PAGES);

    // No Tracks table.
    build_pdb(0xFFFFFFFFu);
    wr32(s_pdb + 0x1C + 16, 7);
    CHECK(find(100, &t, &r) == DJ_LINK_PDB_BAD_FILE);

    // Absurd page size.
    build_pdb(0xFFFFFFFFu);
    wr32(s_pdb + 4, 8);
    CHECK(find(100, &t, &r) == DJ_LINK_PDB_BAD_FILE);

    // A valid long string must not become a shorter, different audio path.
    build_pdb(0xFFFFFFFFu);
    uint8_t *row=s_pdb+PAGE+0x28;
    wr16(row + 0x5E + 2*20,0x900);
    char long_path[300];memset(long_path,'x',sizeof(long_path)-1);long_path[299]=0;
    put_utf16(row+0x900,long_path);
    CHECK(find(100,&t,&r)==DJ_LINK_PDB_FOUND && !t.file_path[0]);
}

// Every track the library parser indexes must resolve identically here.
static void test_cross_check(void)
{
    const char *tmpl="link_pdb_fixture.tmp";
    FILE *fp;
    pdb_t *pdb = NULL;
    int i;
    int n;

    build_pdb(0xFFFFFFFFu);
    fp = fopen(tmpl, "wb");
    CHECK(fp && fwrite(s_pdb, 1, sizeof(s_pdb), fp) == sizeof(s_pdb));
    if(!fp)return;
    fclose(fp);

    CHECK(pdb_open(tmpl, &pdb) == ESP_OK);
    n = pdb ? pdb_track_count(pdb) : 0;
    CHECK(n == 29); // 18 + 12 rows, one deleted
    for (i = 0; i < n; i++) {
        pdb_track_t lib;
        dj_link_pdb_track_t t;
        reader_t r = { 0, 0 };
        CHECK(pdb_get_track(pdb, i, &lib) == ESP_OK);
        CHECK(find(lib.track_id, &t, &r) == DJ_LINK_PDB_FOUND);
        CHECK(strcmp(lib.file_path, t.file_path) == 0);
        CHECK(strcmp(lib.anlz_path, t.anlz_path) == 0);
        CHECK(strcmp(lib.title, t.title) == 0);
        CHECK(lib.duration_s == t.duration_s);
        CHECK(lib.bpm == (t.bpm100 + 50u) / 100u);
    }
    if (pdb) {
        pdb_close(pdb);
    }
    unlink(tmpl);
}

/* v303: the cached export.pdb is reused only while the peer serves the
 * same file (size + mtime from the NFS GETATTR). */
static void test_stamp(void)
{
    dj_link_pdb_stamp_t a = { true, 0x0a000002u, 2u, 81920u, 1700000000u, 0u };
    dj_link_pdb_stamp_t b = a;
    CHECK(dj_link_pdb_stamp_matches(&a, &b));
    b.mtime_s++;                        /* edited at the source */
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    b = a;
    b.size += 4096u;                    /* a page added */
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    b = a;
    b.peer = 3u;
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    b = a;
    b.ip++;
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    b = a;
    a.valid = false;                    /* nothing cached */
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    a.valid = true;
    a.mtime_s = 0u;                     /* the server has no mtime */
    b.mtime_s = 0u;
    CHECK(!dj_link_pdb_stamp_matches(&a, &b));
    CHECK(!dj_link_pdb_stamp_matches(NULL, &b));
}

static void test_extension(void)
{
    char ext[8];
    dj_link_pdb_extension("/Contents/a.b/Track.mp3", ext, sizeof(ext));
    CHECK(strcmp(ext, "MP3") == 0);
    dj_link_pdb_extension("/Contents/x.flac", ext, sizeof(ext));
    CHECK(strcmp(ext, "FLAC") == 0);
    dj_link_pdb_extension("/Contents/a.b/noext", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
    dj_link_pdb_extension("/x.toolongext", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
    dj_link_pdb_extension("/x.", ext, sizeof(ext));
    CHECK(ext[0] == '\0');
}

int main(void)
{
    test_lookup();
    test_errors();
    test_cross_check();
    test_extension();
    test_stamp();
    if (failures == 0) {
        printf("all dj_link_pdb tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
