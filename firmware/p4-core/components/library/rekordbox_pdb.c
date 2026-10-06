/*
 * rekordbox_pdb.c  —  Pioneer Hardware Database (PDB) parser
 *
 * Parses export.pdb from a Rekordbox-formatted USB drive.
 * Reads the Tracks, Artists, and Albums tables and resolves artist/album names.
 *
 * Format references:
 *   https://djl-analysis.deepsymmetry.org/rekordbox-export-analysis/
 *   https://github.com/Deep-Symmetry/crate-digger
 *
 * All multi-byte fields in PDB files are little-endian.
 * Runs on both ESP32-P4 (ESP-IDF VFS) and PC (REKORDBOX_PDB_STANDALONE_TEST).
 */

#include "rekordbox_pdb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef REKORDBOX_PDB_STANDALONE_TEST
#include "esp_heap_caps.h"
#include "media_io_gate.h"
#include "library_validation_gate.h"
#else
#define media_io_gate_begin() ((void)0)
#define media_io_gate_end() ((void)0)
#define media_io_gate_is_available() (true)
#define library_validation_gate_checkpoint() (true)
#endif

static void pdb_close_file(FILE *fp)
{
    if (!fp) return;
    media_io_gate_begin();
    fclose(fp);
    media_io_gate_end();
}

/* Keep every backend ownership interval bounded. The MSC disconnect callback
 * can close media_io_gate asynchronously; the next chunk then fails before
 * touching a VFS handle that is being removed. */
static bool pdb_read_at(FILE *fp, size_t offset, uint8_t *dst, size_t len)
{
    while (len) {
        size_t bytes = len > 8192u ? 8192u : len;
        media_io_gate_begin();
        bool ok = media_io_gate_is_available() &&
                  fseek(fp, (long)offset, SEEK_SET) == 0 &&
                  fread(dst, 1, bytes, fp) == bytes;
        media_io_gate_end();
        if (!ok) return false;
        offset += bytes;
        dst += bytes;
        len -= bytes;
    }
    return true;
}

static const char *TAG = "pdb";

static void pdb_copy_str(char *dst, size_t dst_len, const char *src)
{
    if (!dst || dst_len == 0) {
        return;
    }
    dst[0] = '\0';
    if (!src) {
        return;
    }
    size_t i = 0;
    while (i + 1u < dst_len && src[i] != '\0') {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ── PDB format constants ─────────────────────────────────────────────────── */

/* Table type IDs (crate-digger page_type: 5=keys, 13=artwork — do not mix
 * them up: artwork rows also start with a uint32 id, so reading artwork as
 * keys yields path fragments as "key names") */
#define TABLE_TYPE_TRACKS   0x00u
#define TABLE_TYPE_ARTISTS  0x02u
#define TABLE_TYPE_ALBUMS   0x03u
#define TABLE_TYPE_KEYS     0x05u
#define TABLE_TYPE_PLAYLIST_TREE    0x07u
#define TABLE_TYPE_PLAYLIST_ENTRIES 0x08u
#define TABLE_TYPE_ARTWORK          0x0Du

/* Page layout */
#define PAGE_HEAP_OFFSET    0x28u   /* heap start relative to page base       */
#define PAGE_NROWS_OFF      0x18u   /* packed uint32; lower 13 bits = nrows   */
#define PAGE_NEXT_OFF       0x0Cu   /* uint32: next page number               */

/* Track row field offsets (relative to row start) */
#define TRACK_OFF_KEY_ID    0x20u   /* uint32: id in the Keys table           */
#define TRACK_OFF_ARTWORK_ID 0x1Cu
#define TRACK_OFF_TEMPO     0x38u   /* uint32: BPM × 100                      */
#define TRACK_OFF_ALBUM_ID  0x40u   /* uint32                                 */
#define TRACK_OFF_ARTIST_ID 0x44u   /* uint32                                 */
#define TRACK_OFF_TRACK_ID  0x48u   /* uint32                                 */
#define TRACK_OFF_DURATION  0x54u   /* uint16: seconds                        */
#define TRACK_OFF_STR_OFFS  0x5Eu   /* 21 × uint16 string offsets (rel. to row)*/

/* Track row minimum valid size (must reach last string-offset slot at idx 20) */
#define TRACK_ROW_MIN_SIZE  (TRACK_OFF_STR_OFFS + 21u * 2u)

/* Track row subtype marker at offset 0 of every valid track row */
#define TRACK_ROW_SUBTYPE   0x0024u

/* String-offset table indices */
#define STR_IDX_ANLZ_PATH   14u     /* /PIONEER/USBANLZ/.../ANLZ0000.DAT      */
#define STR_IDX_TITLE       17u
#define STR_IDX_FILENAME    19u
#define STR_IDX_FILE_PATH   20u

/* Name-table row layout offsets (Artists / Albums) */
#define NAME_ROW_ID_OFF     4u      /* uint32 row ID (skip 4B link field)     */
#define NAME_ROW_STR_OFF    10u     /* DeviceSQL string (skip id+empty+pad)   */
#define NAME_ROW_MIN_SIZE   14u

/* Key-table row layout: uint32 id, uint32 id copy, DeviceSQL name */
#define KEY_ROW_ID_OFF      0u
#define KEY_ROW_STR_OFF     8u
#define KEY_ROW_MIN_SIZE    9u

/* Limits — embedded memory budget */
#define PDB_MAX_TRACKS      1024u
#define PDB_MAX_NAMES        512u
#define PDB_MAX_PLAYLISTS          256u
#define PDB_MAX_PLAYLIST_ENTRIES  8192u
#define PDB_MAX_ARTWORKS          1024u

/* ── Little-endian read helpers ─────────────────────────────────────────── */

static inline uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u));
}

static inline uint32_t rd_le32(const uint8_t *p)
{
    return ((uint32_t)p[0])         |
           ((uint32_t)p[1] <<  8u)  |
           ((uint32_t)p[2] << 16u)  |
           ((uint32_t)p[3] << 24u);
}

static bool utf8_append_codepoint(char *dst, size_t dst_sz, size_t *out_i, uint32_t cp)
{
    if (!dst || dst_sz == 0 || !out_i || cp == 0u) {
        return false;
    }
    size_t i = *out_i;
    if (cp <= 0x7Fu) {
        if (i + 1u >= dst_sz) return false;
        dst[i++] = (char)cp;
    } else if (cp <= 0x7FFu) {
        if (i + 2u >= dst_sz) return false;
        dst[i++] = (char)(0xC0u | (cp >> 6));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        if (i + 3u >= dst_sz) return false;
        dst[i++] = (char)(0xE0u | (cp >> 12));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0x10FFFFu) {
        if (i + 4u >= dst_sz) return false;
        dst[i++] = (char)(0xF0u | (cp >> 18));
        dst[i++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        return false;
    }
    *out_i = i;
    return true;
}

static uint16_t read_utf16_unit(const uint8_t *p, bool le)
{
    return le
        ? (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8u))
        : (uint16_t)(((uint16_t)p[0] << 8u) | (uint16_t)p[1]);
}

/* ── DeviceSQL string decoder ─────────────────────────────────────────────
 *
 * Decodes a DeviceSQL string at abs_off in data[0..data_len).
 * Writes up to dst_sz-1 bytes into dst (always NUL-terminates).
 * Returns total bytes consumed by the field.
 *
 * String formats:
 *   0x00, 0x40               — empty/null (1 byte)
 *   flag where (flag & 1)    — short ASCII:
 *                               total_field_len = flag >> 1 (includes flag byte)
 *                               string data = (flag >> 1) - 1 chars
 *   flag where !(flag & 1)   — long string: flag(1) + total_len(2LE) + pad(1) + data
 *                               W bit (0x10): UTF-16  E bit (0x80): little-endian
 * ─────────────────────────────────────────────────────────────────────────── */
static size_t decode_devicesql(const uint8_t *data, size_t data_len,
                                size_t abs_off,
                                char *dst, size_t dst_sz)
{
    if (dst_sz == 0) return 1;
    dst[0] = '\0';

    if (abs_off >= data_len) return 1;
    uint8_t flag = data[abs_off];

    /* Empty / null */
    if (flag == 0x40u || flag == 0x00u) return 1;

    /* Short ASCII: odd flag byte */
    if (flag & 1u) {
        size_t total  = (size_t)(flag >> 1u);
        size_t nbytes = (total > 0u) ? total - 1u : 0u;
        if (nbytes > 0u && abs_off + 1u + nbytes <= data_len) {
            size_t copy = (nbytes < dst_sz - 1u) ? nbytes : dst_sz - 1u;
            memcpy(dst, data + abs_off + 1u, copy);
            dst[copy] = '\0';
            /* strip trailing embedded NULs */
            while (copy > 0u && dst[copy - 1u] == '\0') copy--;
            dst[copy] = '\0';
        }
        return total > 0u ? total : 1u;
    }

    /* Long string: flag + uint16 total_len + pad + string data */
    if (abs_off + 3u >= data_len) return 1;
    size_t total_len  = (size_t)rd_le16(data + abs_off + 1u);
    size_t data_start = abs_off + 4u;
    size_t data_bytes = (total_len >= 4u) ? total_len - 4u : 0u;

    if (data_bytes == 0u || data_start + data_bytes > data_len)
        return total_len > 0u ? total_len : 1u;

    bool wide = (flag & 0x10u) != 0u;   /* W bit: UTF-16 */
    bool le   = (flag & 0x80u) != 0u;   /* E bit: little-endian */

    if (wide) {
        /* DeviceSQL UTF-16 → UTF-8. FatFs LFN expects UTF-8 paths. */
        size_t nchars = data_bytes / 2u;
        size_t out_i  = 0u;
        const uint8_t *p = data + data_start;
        for (size_t i = 0u; i < nchars && out_i < dst_sz - 1u; i++) {
            uint16_t wc = read_utf16_unit(p + i * 2u, le);
            if (wc == 0u) break;
            uint32_t cp = wc;
            if (wc >= 0xD800u && wc <= 0xDBFFu && i + 1u < nchars) {
                uint16_t lo = read_utf16_unit(p + (i + 1u) * 2u, le);
                if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                    cp = 0x10000u + ((((uint32_t)wc - 0xD800u) << 10) | ((uint32_t)lo - 0xDC00u));
                    i++;
                }
            }
            if (cp == '\\') cp = '/';
            if (!utf8_append_codepoint(dst, dst_sz, &out_i, cp)) break;
        }
        dst[out_i] = '\0';
    } else {
        size_t copy = (data_bytes < dst_sz - 1u) ? data_bytes : dst_sz - 1u;
        memcpy(dst, data + data_start, copy);
        dst[copy] = '\0';
        while (copy > 0u && dst[copy - 1u] == '\0') copy--;
        dst[copy] = '\0';
    }

    return total_len > 0u ? total_len : 1u;
}

#ifdef REKORDBOX_PDB_STANDALONE_TEST
esp_err_t pdb_test_decode_devicesql_string(const uint8_t *data, size_t data_len,
                                           char *dst, size_t dst_sz)
{
    if (!data || !dst || dst_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    decode_devicesql(data, data_len, 0, dst, dst_sz);
    return ESP_OK;
}
#endif

/* ── Name-table entry ────────────────────────────────────────────────────── */

typedef struct {
    uint32_t id;
    char     name[PDB_STR_NAME_MAX];
} name_entry_t;

/* ── Internal PDB handle ─────────────────────────────────────────────────── */

struct pdb_s {
    uint8_t  *data;          /* One bounded page, freed after build_index. */
    FILE *source;
    bool read_failed;
    pdb_import_stats_t stats;
    size_t    data_len;
    uint32_t  page_size;
    uint32_t  num_tables;
    uint32_t  total_pages;

    struct {
        uint32_t type;
        uint32_t first_page;
    } tables[32];

    /* Parsed tracks */
    pdb_track_t *tracks;
    int          track_count;

    pdb_playlist_t *playlists;
    int playlist_count;
    pdb_playlist_entry_t *entries;
    int entry_count;
    pdb_artwork_t *artworks;
    int artwork_count;

    /* Name lookup tables (used only during build_index, freed afterwards) */
    name_entry_t *artists;
    int           artist_count;
    name_entry_t *albums;
    int           album_count;
    name_entry_t *keys;
    int           key_count;
};

/* ── Page helpers ────────────────────────────────────────────────────────── */

static inline size_t page_base_off(const struct pdb_s *p, uint32_t pn)
{
    (void)p;
    (void)pn;
    return 0; /* The walker has loaded this page into the bounded buffer. */
}

static uint32_t page_nrows(const struct pdb_s *p, uint32_t pn)
{
    size_t off = page_base_off(p, pn) + PAGE_NROWS_OFF;
    if (off + 4u > p->data_len) return 0u;
    return rd_le32(p->data + off) & 0x1FFFu;
}

static uint32_t page_next(const struct pdb_s *p, uint32_t pn)
{
    size_t off = page_base_off(p, pn) + PAGE_NEXT_OFF;
    if (off + 4u > p->data_len) return 0xFFFFFFFFu;
    return rd_le32(p->data + off);
}

/* ── Row-slot iterator ───────────────────────────────────────────────────── *
 *
 * Row slots are stored in groups of ≤16 at the END of each page, growing
 * backwards.  For each group (reading backwards from page end):
 *   [ptr-2]          tranrf       (2B, ignored)
 *   [ptr-4]          rowpf        (16-bit bitmask: bit i = row i is present)
 *   [ptr-4-2*(i+1)]  heap_off[i]  (2B each, relative to page heap start)
 *
 * After processing a group of m rows, ptr advances by -(4 + m*2).
 * ─────────────────────────────────────────────────────────────────────────── */

typedef bool (*row_cb_t)(const struct pdb_s *p, uint32_t page_num,
                          uint32_t heap_off, void *user);

static void iter_page_rows(const struct pdb_s *p, uint32_t page_num,
                            row_cb_t cb, void *user)
{
    uint32_t n = page_nrows(p, page_num);
    if (n == 0u) return;

    size_t pb  = page_base_off(p, page_num);
    size_t ptr = pb + (size_t)p->page_size;   /* reading cursor, grows backwards */

    for (uint32_t idx = 0u; idx < n; ) {
        uint32_t m = (n - idx < 16u) ? (n - idx) : 16u;
        size_t group_bytes = 4u + (size_t)m * 2u;

        /* Validate the whole row-slot group before reading any of it.  Merely
         * checking for the 4-byte group header is insufficient: a malformed
         * nrows value can leave fewer than m slots in the page.  Subtracting
         * group_bytes in that state underflows ptr on page zero and turns the
         * next rowpf read into an out-of-bounds access.  Use a difference after
         * establishing ptr >= pb so the guard itself cannot overflow. */
        if (ptr > p->data_len || ptr < pb || ptr - pb < group_bytes) break;
        uint16_t rowpf = rd_le16(p->data + ptr - 4u);

        for (uint32_t i = 0u; i < m; i++) {
            size_t slot = ptr - 4u - 2u * (i + 1u);
            /* Guard both ends: a malformed nrows/page can drive the slot cursor
             * below the page base (reading a neighbouring page's bytes as a row
             * offset) or, on size_t underflow, past the buffer end. */
            if (slot < pb || slot + 2u > p->data_len) continue;
            uint32_t ho      = (uint32_t)rd_le16(p->data + slot);
            bool     present = (rowpf & (1u << i)) != 0u;
            if (present) {
                if (!cb(p, page_num, ho, user)) return;
            }
        }

        ptr -= group_bytes;
        idx += m;
    }
}

/* ── Table walker helper ─────────────────────────────────────────────────── */

static void walk_table(struct pdb_s *p, uint32_t table_type,
                        row_cb_t cb, void *user)
{
    if (p->read_failed) return;
    for (uint32_t i = 0u; i < p->num_tables; i++) {
        if (p->tables[i].type != table_type) continue;
        uint32_t page_num = p->tables[i].first_page;
        uint32_t visited = 0u;
        while (page_num != 0xFFFFFFFFu && page_num < p->total_pages) {
            /* A corrupted next-page chain can form a cycle; without this
               guard the walk spins forever and trips the task watchdog. */
            if (visited++ >= p->total_pages) {
                PDB_LOGW(TAG, "Page chain loop in table 0x%02X; truncating walk",
                         table_type);
                break;
            }
            if (!pdb_read_at(p->source,
                             (size_t)page_num * p->page_size,
                             p->data, p->page_size)) {
                p->read_failed = true;
                return;
            }
            iter_page_rows(p, page_num, cb, user);
            page_num = page_next(p, page_num);
        }
        return;
    }
}

/* ── Name-table parser (Artists / Albums) ────────────────────────────────── */

typedef struct {
    name_entry_t *arr;
    struct pdb_s *owner;
    int          *count;
    int           max;
    /* Row layout — Artists/Albums and Keys place id/name differently */
    size_t        id_off;
    size_t        str_off;
    size_t        min_size;
} name_ctx_t;

static bool name_cb(const struct pdb_s *p, uint32_t page_num,
                     uint32_t heap_off, void *user)
{
    name_ctx_t *ctx = (name_ctx_t *)user;
    if (*ctx->count >= ctx->max) {
        ctx->owner->stats.names_truncated = true;
        return false;
    }

    size_t row = page_base_off(p, page_num) + PAGE_HEAP_OFFSET + (size_t)heap_off;
    if (row + ctx->min_size > p->data_len) return true;

    uint32_t row_id = rd_le32(p->data + row + ctx->id_off);
    if (row_id == 0u) return true;

    name_entry_t *e = &ctx->arr[*ctx->count];
    e->id = row_id;
    decode_devicesql(p->data, p->data_len,
                     row + ctx->str_off,
                     e->name, PDB_STR_NAME_MAX);

    if (e->name[0] != '\0') (*ctx->count)++;
    return true;
}

static void parse_name_table(struct pdb_s *p, uint32_t table_type,
                               name_entry_t **out, int *out_count, int max,
                               size_t id_off, size_t str_off, size_t min_size)
{
    *out       = NULL;
    *out_count = 0;

    *out = (name_entry_t *)malloc((size_t)max * sizeof(name_entry_t));
    if (!*out) {
        p->stats.names_truncated = true;
        return;
    }
    memset(*out, 0, (size_t)max * sizeof(name_entry_t));

    name_ctx_t ctx = { *out, p, out_count, max, id_off, str_off, min_size };
    walk_table(p, table_type, name_cb, &ctx);

    PDB_LOGI(TAG, "Name table 0x%02X: %d entries", table_type, *out_count);
}

/* ── Name lookup (linear scan — ≤512 entries, called only at parse time) ─── */

static const char *lookup_name(const name_entry_t *arr, int count, uint32_t id)
{
    if (!arr || id == 0u) return NULL;
    for (int i = 0; i < count; i++) {
        if (arr[i].id == id) return arr[i].name;
    }
    return NULL;
}

/* ── Track parser ────────────────────────────────────────────────────────── */

typedef struct {
    struct pdb_s *p;
    int           count;
    bool          truncated;
} track_ctx_t;

static bool track_cb(const struct pdb_s *p, uint32_t page_num,
                      uint32_t heap_off, void *user)
{
    track_ctx_t *ctx = (track_ctx_t *)user;
    size_t row = page_base_off(p, page_num) + PAGE_HEAP_OFFSET + (size_t)heap_off;
    if (row + TRACK_ROW_MIN_SIZE > p->data_len) return true;

    /* Verify track row subtype at offset 0 */
    if (rd_le16(p->data + row) != TRACK_ROW_SUBTYPE) return true;
    ctx->p->stats.total_tracks++;
    if (ctx->count >= (int)PDB_MAX_TRACKS) {
        ctx->truncated = true;
        ctx->p->stats.tracks_truncated = true;
        return true;
    }

    pdb_track_t *t = &p->tracks[ctx->count];
    memset(t, 0, sizeof(*t));

    t->track_id  = rd_le32(p->data + row + TRACK_OFF_TRACK_ID);
    t->artwork_id = rd_le32(p->data + row + TRACK_OFF_ARTWORK_ID);
    uint32_t bpm100 = rd_le32(p->data + row + TRACK_OFF_TEMPO);
    /* Round to nearest, matching the ANLZ beat-grid path (rekordbox_anlz.c),
     * so a track's coarse (PDB) and precise (ANLZ) BPM agree on the integer. */
    t->bpm       = (uint16_t)((bpm100 + 50u) / 100u);
    t->duration_s = rd_le16(p->data + row + TRACK_OFF_DURATION);

    uint32_t artist_id = rd_le32(p->data + row + TRACK_OFF_ARTIST_ID);
    uint32_t album_id  = rd_le32(p->data + row + TRACK_OFF_ALBUM_ID);
    uint32_t key_id    = rd_le32(p->data + row + TRACK_OFF_KEY_ID);

    /* Read string fields from offset table.
     * Each entry is a uint16 at (row + TRACK_OFF_STR_OFFS + idx*2).
     * The value is the offset from row start to the DeviceSQL string. */
#define READ_STR(buf, idx) do {                                               \
        size_t op = row + TRACK_OFF_STR_OFFS + (size_t)(idx) * 2u;           \
        if (op + 2u <= p->data_len) {                                         \
            uint32_t soff = (uint32_t)rd_le16(p->data + op);                 \
            if (soff > 0u)                                                    \
                decode_devicesql(p->data, p->data_len,                        \
                                 row + soff, buf, sizeof(buf));               \
        }                                                                     \
    } while (0)

    char title[PDB_STR_MAX]    = {0};
    char filename[PDB_STR_MAX] = {0};

    READ_STR(title,        STR_IDX_TITLE);
    READ_STR(filename,     STR_IDX_FILENAME);
    READ_STR(t->file_path, STR_IDX_FILE_PATH);
    READ_STR(t->anlz_path, STR_IDX_ANLZ_PATH);

#undef READ_STR

    /* Title: prefer tagged title, fall back to filename */
    pdb_copy_str(t->title,
                 sizeof(t->title),
                 title[0] != '\0' ? title : filename);

    /* Resolve artist/album names */
    const char *a = lookup_name(p->artists, p->artist_count, artist_id);
    if (a) pdb_copy_str(t->artist, sizeof(t->artist), a);

    const char *al = lookup_name(p->albums, p->album_count, album_id);
    if (al) pdb_copy_str(t->album, sizeof(t->album), al);

    const char *k = lookup_name(p->keys, p->key_count, key_id);
    if (k) pdb_copy_str(t->key, sizeof(t->key), k);

    ctx->count++;
    return true;
}

/* Optional Rekordbox browse tables. Their bounded arrays live in PSRAM when
 * available, and are retained only in the parsed PDB handle. */
static void *pdb_alloc_optional(size_t count, size_t item_size)
{
#ifndef REKORDBOX_PDB_STANDALONE_TEST
    void *ptr = heap_caps_calloc(count, item_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return ptr ? ptr : calloc(count, item_size);
#else
    return calloc(count, item_size);
#endif
}

static bool pdb_has_table(const pdb_t *p, uint32_t type)
{
    for (uint32_t i = 0u; i < p->num_tables; ++i) {
        if (p->tables[i].type == type) return true;
    }
    return false;
}

static bool playlist_tree_cb(const pdb_t *p, uint32_t page, uint32_t heap_off,
                             void *user)
{
    pdb_t *owner = (pdb_t *)user;
    size_t row = page_base_off(p, page) + PAGE_HEAP_OFFSET + (size_t)heap_off;
    if (row + 21u > p->data_len) { owner->stats.playlist_invalid_rows++; return true; }
    uint32_t id = rd_le32(p->data + row + 12u);
    if (!id) { owner->stats.playlist_invalid_rows++; return true; }
    if (owner->playlist_count >= (int)PDB_MAX_PLAYLISTS) {
        owner->stats.playlists_truncated = true;
        return false;
    }
    pdb_playlist_t *node = &owner->playlists[owner->playlist_count];
    node->id = id;
    node->parent_id = rd_le32(p->data + row);
    node->sort_order = rd_le32(p->data + row + 8u);
    node->is_folder = rd_le32(p->data + row + 16u) != 0u;
    decode_devicesql(p->data, p->data_len, row + 20u,
                     node->name, sizeof(node->name));
    if (!node->name[0]) { owner->stats.playlist_invalid_rows++; return true; }
    owner->playlist_count++;
    return true;
}

static bool playlist_entry_cb(const pdb_t *p, uint32_t page, uint32_t heap_off,
                              void *user)
{
    pdb_t *owner = (pdb_t *)user;
    size_t row = page_base_off(p, page) + PAGE_HEAP_OFFSET + (size_t)heap_off;
    if (row + 12u > p->data_len) { owner->stats.playlist_invalid_rows++; return true; }
    uint32_t track = rd_le32(p->data + row + 4u);
    uint32_t list = rd_le32(p->data + row + 8u);
    if (!track || !list) { owner->stats.playlist_invalid_rows++; return true; }
    if (owner->entry_count >= (int)PDB_MAX_PLAYLIST_ENTRIES) {
        owner->stats.playlists_truncated = true;
        return false;
    }
    pdb_playlist_entry_t *entry = &owner->entries[owner->entry_count++];
    entry->playlist_id = list;
    entry->track_id = track;
    entry->entry_index = rd_le32(p->data + row);
    return true;
}

static int playlist_entry_cmp(const void *a, const void *b)
{
    const pdb_playlist_entry_t *x = (const pdb_playlist_entry_t *)a;
    const pdb_playlist_entry_t *y = (const pdb_playlist_entry_t *)b;
    if (x->playlist_id != y->playlist_id) return x->playlist_id < y->playlist_id ? -1 : 1;
    if (x->entry_index != y->entry_index) return x->entry_index < y->entry_index ? -1 : 1;
    return 0;
}

static int playlist_raw_index(const pdb_t *p, uint32_t id)
{
    for (int i = 0; i < p->playlist_count; ++i) {
        if (p->playlists[i].id == id) return i;
    }
    return -1;
}

static bool playlist_chain_valid(const pdb_t *p, int index)
{
    const pdb_playlist_t *node = &p->playlists[index];
    if (!node->id || !node->name[0]) return false;
    for (int i = 0; i < index; ++i) {
        if (p->playlists[i].id == node->id) return false;
    }
    uint32_t parent = node->parent_id;
    for (int depth = 0; parent != 0u && depth <= p->playlist_count; ++depth) {
        int idx = playlist_raw_index(p, parent);
        if (idx < 0 || !p->playlists[idx].is_folder ||
            p->playlists[idx].id == node->id) return false;
        /* A duplicate ancestor is never a valid destination. */
        for (int prev = 0; prev < idx; ++prev) {
            if (p->playlists[prev].id == parent) return false;
        }
        parent = p->playlists[idx].parent_id;
    }
    return parent == 0u;
}

static bool playlist_contains_track(const pdb_t *p, uint32_t track_id)
{
    for (int i = 0; i < p->track_count; ++i) {
        if (p->tracks[i].track_id == track_id) return true;
    }
    return false;
}

static void filter_playlist_rows(pdb_t *p)
{
    bool valid[PDB_MAX_PLAYLISTS] = {0};
    for (int i = 0; i < p->playlist_count; ++i)
        valid[i] = playlist_chain_valid(p, i);
    int keep = 0;
    for (int i = 0; i < p->playlist_count; ++i) {
        if (!valid[i]) {
            p->stats.playlist_invalid_rows++;
            continue;
        }
        p->playlists[keep++] = p->playlists[i];
    }
    p->playlist_count = keep;
    keep = 0;
    for (int i = 0; i < p->entry_count; ++i) {
        const pdb_playlist_entry_t *e = &p->entries[i];
        int idx = playlist_raw_index(p, e->playlist_id);
        if (idx < 0 || p->playlists[idx].is_folder ||
            !playlist_contains_track(p, e->track_id)) {
            p->stats.playlist_invalid_rows++;
            continue;
        }
        p->entries[keep++] = *e;
    }
    p->entry_count = keep;
}

static void parse_playlists(pdb_t *p)
{
    if (!pdb_has_table(p, TABLE_TYPE_PLAYLIST_TREE)) return;
    p->playlists = (pdb_playlist_t *)pdb_alloc_optional(PDB_MAX_PLAYLISTS,
                                                        sizeof(pdb_playlist_t));
    if (!p->playlists) { p->stats.playlists_truncated = true; return; }
    walk_table(p, TABLE_TYPE_PLAYLIST_TREE, playlist_tree_cb, p);
    if (!p->playlist_count) return;
    if (!pdb_has_table(p, TABLE_TYPE_PLAYLIST_ENTRIES)) {
        filter_playlist_rows(p);
        return;
    }
    p->entries = (pdb_playlist_entry_t *)pdb_alloc_optional(PDB_MAX_PLAYLIST_ENTRIES,
                                                             sizeof(pdb_playlist_entry_t));
    if (!p->entries) {
        p->stats.playlists_truncated = true;
        filter_playlist_rows(p);
        return;
    }
    walk_table(p, TABLE_TYPE_PLAYLIST_ENTRIES, playlist_entry_cb, p);
    qsort(p->entries, (size_t)p->entry_count, sizeof(pdb_playlist_entry_t),
          playlist_entry_cmp);
    filter_playlist_rows(p);
}

static bool artwork_cb(const pdb_t *p, uint32_t page, uint32_t heap_off,
                       void *user)
{
    pdb_t *owner = (pdb_t *)user;
    size_t row = page_base_off(p, page) + PAGE_HEAP_OFFSET + (size_t)heap_off;
    if (row + 5u > p->data_len) { owner->stats.artwork_invalid_rows++; return true; }
    uint32_t id = rd_le32(p->data + row);
    if (!id) { owner->stats.artwork_invalid_rows++; return true; }
    if (owner->artwork_count >= (int)PDB_MAX_ARTWORKS) {
        owner->stats.artwork_truncated = true;
        return false;
    }
    char path[PDB_PATH_MAX] = {0};
    decode_devicesql(p->data, p->data_len, row + 4u, path, sizeof(path));
    size_t len = strlen(path);
    if (path[0] != '/' || strstr(path, "/../") || len == 0u ||
        len >= PDB_ARTWORK_PATH_MAX ||
        len + 1u >= sizeof(path)) {
        owner->stats.artwork_invalid_rows++;
        return true;
    }
    pdb_artwork_t *art = &owner->artworks[owner->artwork_count++];
    art->id = id;
    pdb_copy_str(art->path, sizeof(art->path), path);
    return true;
}

static int artwork_cmp(const void *a, const void *b)
{
    uint32_t x = ((const pdb_artwork_t *)a)->id;
    uint32_t y = ((const pdb_artwork_t *)b)->id;
    return x < y ? -1 : x > y ? 1 : 0;
}

static void parse_artworks(pdb_t *p)
{
    if (!pdb_has_table(p, TABLE_TYPE_ARTWORK)) return;
    p->artworks = (pdb_artwork_t *)pdb_alloc_optional(PDB_MAX_ARTWORKS,
                                                      sizeof(pdb_artwork_t));
    if (!p->artworks) { p->stats.artwork_truncated = true; return; }
    walk_table(p, TABLE_TYPE_ARTWORK, artwork_cb, p);
    qsort(p->artworks, (size_t)p->artwork_count, sizeof(pdb_artwork_t), artwork_cmp);
    int keep = 0;
    for (int i = 0; i < p->artwork_count; ++i) {
        if (keep && p->artworks[i].id == p->artworks[keep - 1].id) {
            p->stats.artwork_invalid_rows++;
            continue;
        }
        p->artworks[keep++] = p->artworks[i];
    }
    p->artwork_count = keep;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

esp_err_t pdb_open(const char *pdb_path, pdb_t **out)
{
    if (!pdb_path || !out) return ESP_ERR_INVALID_ARG;
    *out = NULL;

    /* Read the header before allocating, then walk one page at a time. */
    media_io_gate_begin();
    FILE *fp = media_io_gate_is_available() ? fopen(pdb_path, "rb") : NULL;
    media_io_gate_end();
    if (!fp) {
        PDB_LOGE(TAG, "Cannot open: %s", pdb_path);
        return ESP_ERR_NOT_FOUND;
    }

    media_io_gate_begin();
    long fsize = media_io_gate_is_available() &&
                 fseek(fp, 0, SEEK_END) == 0 ? ftell(fp) : -1;
    media_io_gate_end();

    if (fsize < 28) {
        PDB_LOGE(TAG, "File too short (%ld bytes)", fsize);
        pdb_close_file(fp);
        return ESP_FAIL;
    }

    struct pdb_s *p = (struct pdb_s *)calloc(1u, sizeof(struct pdb_s));
    if (!p) { pdb_close_file(fp); return ESP_ERR_NO_MEM; }
    p->source = fp;
    uint8_t header[28];
    if (!pdb_read_at(fp, 0, header, sizeof(header))) {
        pdb_close(p);
        return ESP_FAIL;
    }
    if (!library_validation_gate_checkpoint()) {
        pdb_close(p);
        return ESP_FAIL;
    }
    p->page_size = rd_le32(header + 4u);
    p->num_tables = rd_le32(header + 8u);
    if (p->page_size < PAGE_HEAP_OFFSET || p->page_size > 65536u ||
        p->page_size > (size_t)fsize) {
        PDB_LOGE(TAG, "Bad page_size=%u", p->page_size);
        pdb_close(p);
        return ESP_FAIL;
    }

    size_t data_size = p->page_size;
#ifndef REKORDBOX_PDB_STANDALONE_TEST
    p->data = (uint8_t *)heap_caps_malloc(data_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p->data) {
        p->data = (uint8_t *)malloc(data_size);
    }
#else
    p->data = (uint8_t *)malloc(data_size);
#endif
    if (!p->data) { pdb_close(p); return ESP_ERR_NO_MEM; }

    if (!pdb_read_at(fp, 0, p->data, data_size)) {
        PDB_LOGE(TAG, "Read error");
        pdb_close(p);
        return ESP_FAIL;
    }
    p->data_len = data_size;
    p->total_pages = (uint32_t)((size_t)fsize / p->page_size);

    if (p->num_tables > 32u) p->num_tables = 32u;

    /* Table pointers: 16 bytes each, starting at file offset 0x1C */
    for (uint32_t i = 0u; i < p->num_tables; i++) {
        size_t off = 0x1Cu + (size_t)i * 16u;
        if (off + 16u > p->data_len) { p->num_tables = i; break; }
        p->tables[i].type       = rd_le32(p->data + off + 0u);
        p->tables[i].first_page = rd_le32(p->data + off + 8u);
    }

    PDB_LOGI(TAG, "Opened PDB: %ld B, page=%u, tables=%u, pages=%u",
             fsize, p->page_size, p->num_tables, p->total_pages);

    /* Build name lookup tables BEFORE parsing tracks */
    parse_name_table(p, TABLE_TYPE_ARTISTS,
                     &p->artists, &p->artist_count, (int)PDB_MAX_NAMES,
                     NAME_ROW_ID_OFF, NAME_ROW_STR_OFF, NAME_ROW_MIN_SIZE);
    parse_name_table(p, TABLE_TYPE_ALBUMS,
                     &p->albums,  &p->album_count,  (int)PDB_MAX_NAMES,
                     NAME_ROW_ID_OFF, NAME_ROW_STR_OFF, NAME_ROW_MIN_SIZE);
    parse_name_table(p, TABLE_TYPE_KEYS,
                     &p->keys,    &p->key_count,    (int)PDB_MAX_NAMES,
                     KEY_ROW_ID_OFF, KEY_ROW_STR_OFF, KEY_ROW_MIN_SIZE);
    if (p->read_failed) {
        pdb_close(p);
        return ESP_FAIL;
    }

    /* Allocate track array */
    p->tracks = (pdb_track_t *)calloc(PDB_MAX_TRACKS, sizeof(pdb_track_t));
    if (!p->tracks) {
        pdb_close(p);
        return ESP_ERR_NO_MEM;
    }

    /* Parse tracks */
    track_ctx_t ctx = { .p = p, .count = 0, .truncated = false };
    walk_table(p, TABLE_TYPE_TRACKS, track_cb, &ctx);
    if (p->read_failed) {
        pdb_close(p);
        return ESP_FAIL;
    }
    p->track_count = ctx.count;
    if (ctx.truncated) {
        PDB_LOGW(TAG, "Track index truncated at %u entries", PDB_MAX_TRACKS);
    }

    parse_playlists(p);
    parse_artworks(p);
    if (p->read_failed) {
        pdb_close(p);
        return ESP_FAIL;
    }

    PDB_LOGI(TAG, "Loaded: %d tracks, %d artists, %d albums, %d keys",
             p->track_count, p->artist_count, p->album_count, p->key_count);

    /* Free intermediary data — no longer needed after index is built */
    pdb_close_file(p->source); p->source = NULL;
    free(p->data);     p->data          = NULL; p->data_len    = 0;
    free(p->artists);  p->artists       = NULL; p->artist_count = 0;
    free(p->albums);   p->albums        = NULL; p->album_count  = 0;
    free(p->keys);     p->keys          = NULL; p->key_count    = 0;

    *out = p;
    return ESP_OK;
}

void pdb_close(pdb_t *pdb)
{
    if (!pdb) return;
    pdb_close_file(pdb->source);
    free(pdb->data);
    free(pdb->tracks);
    free(pdb->artists);
    free(pdb->albums);
    free(pdb->keys);
    free(pdb->playlists);
    free(pdb->entries);
    free(pdb->artworks);
    free(pdb);
}

int pdb_track_count(const pdb_t *pdb)
{
    return pdb ? pdb->track_count : 0;
}

void pdb_get_import_stats(const pdb_t *pdb, pdb_import_stats_t *stats)
{
    if (stats) *stats = pdb ? pdb->stats : (pdb_import_stats_t){0};
}

esp_err_t pdb_get_track(const pdb_t *pdb, int index, pdb_track_t *out)
{
    if (!pdb || !out || index < 0 || index >= pdb->track_count)
        return ESP_ERR_INVALID_ARG;
    *out = pdb->tracks[index];
    return ESP_OK;
}

int pdb_playlist_count(const pdb_t *pdb)
{
    return pdb ? pdb->playlist_count : 0;
}

esp_err_t pdb_get_playlist(const pdb_t *pdb, int index, pdb_playlist_t *out)
{
    if (!pdb || !out || index < 0 || index >= pdb->playlist_count)
        return ESP_ERR_INVALID_ARG;
    *out = pdb->playlists[index];
    return ESP_OK;
}

int pdb_playlist_entry_count(const pdb_t *pdb)
{
    return pdb ? pdb->entry_count : 0;
}

esp_err_t pdb_get_playlist_entry(const pdb_t *pdb, int index,
                                 pdb_playlist_entry_t *out)
{
    if (!pdb || !out || index < 0 || index >= pdb->entry_count)
        return ESP_ERR_INVALID_ARG;
    *out = pdb->entries[index];
    return ESP_OK;
}

int pdb_artwork_count(const pdb_t *pdb)
{
    return pdb ? pdb->artwork_count : 0;
}

esp_err_t pdb_get_artwork(const pdb_t *pdb, int index, pdb_artwork_t *out)
{
    if (!pdb || !out || index < 0 || index >= pdb->artwork_count)
        return ESP_ERR_INVALID_ARG;
    *out = pdb->artworks[index];
    return ESP_OK;
}

const char *pdb_artwork_path(const pdb_t *pdb, uint32_t artwork_id)
{
    if (!pdb || !artwork_id || !pdb->artworks) return NULL;
    int lo = 0, hi = pdb->artwork_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (pdb->artworks[mid].id < artwork_id) lo = mid + 1;
        else hi = mid;
    }
    return lo < pdb->artwork_count && pdb->artworks[lo].id == artwork_id
        ? pdb->artworks[lo].path : NULL;
}
