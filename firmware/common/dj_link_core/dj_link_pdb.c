#include "dj_link_pdb.h"

#include <string.h>

// Layout constants shared with library/rekordbox_pdb.c (djl-analysis
// "rekordbox Export Structure").
#define PDB_HEADER_LEN      0x1Cu
#define TABLE_PTR_LEN       16u
#define TABLE_MAX           32u
#define TABLE_TYPE_TRACKS   0x00u
#define PAGE_HEAP_OFFSET    0x28u
#define PAGE_NROWS_OFF      0x18u
#define PAGE_NEXT_OFF       0x0Cu
#define TRACK_OFF_TEMPO     0x38u
#define TRACK_OFF_TRACK_ID  0x48u
#define TRACK_OFF_DURATION  0x54u
#define TRACK_OFF_STR_OFFS  0x5Eu
#define TRACK_ROW_MIN_SIZE  (TRACK_OFF_STR_OFFS + 21u * 2u)
#define TRACK_ROW_SUBTYPE   0x0024u
#define STR_IDX_ANLZ_PATH   14u
#define STR_IDX_TITLE       17u     /* v275: 18 is unknown_string_8 (empty) */
#define STR_IDX_FILENAME    19u
#define STR_IDX_FILE_PATH   20u

static uint16_t rd_le16(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t rd_le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static bool utf8_append(char *dst, size_t cap, size_t *pos, uint32_t cp)
{
    size_t i = *pos;
    if (cp == 0u) {
        return false;
    }
    if (cp <= 0x7Fu) {
        if (i + 1u >= cap) return false;
        dst[i++] = (char)cp;
    } else if (cp <= 0x7FFu) {
        if (i + 2u >= cap) return false;
        dst[i++] = (char)(0xC0u | (cp >> 6));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0xFFFFu) {
        if (i + 3u >= cap) return false;
        dst[i++] = (char)(0xE0u | (cp >> 12));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else if (cp <= 0x10FFFFu) {
        if (i + 4u >= cap) return false;
        dst[i++] = (char)(0xF0u | (cp >> 18));
        dst[i++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
        dst[i++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
        dst[i++] = (char)(0x80u | (cp & 0x3Fu));
    } else {
        return false;
    }
    *pos = i;
    return true;
}

static uint16_t utf16_unit(const uint8_t *p, bool le)
{
    return le ? (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8))
              : (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

// DeviceSQL string at off in data[0..len) -> UTF-8, same rules as
// rekordbox_pdb.c decode_devicesql(): 0x40/0x00 empty, odd flag = short ASCII
// (total = flag >> 1), else flag + u16 total + pad + data with W (0x10,
// UTF-16) and E (0x80, little-endian) bits; UTF-16 '\' becomes '/'.
static void devicesql(const uint8_t *data, size_t len, size_t off, char *dst, size_t cap)
{
    uint8_t flag;
    dst[0] = '\0';
    if (off >= len) {
        return;
    }
    flag = data[off];
    if (flag == 0x40u || flag == 0x00u) {
        return;
    }
    if (flag & 1u) {
        size_t total = (size_t)(flag >> 1);
        size_t n = total > 0u ? total - 1u : 0u;
        if (n > 0u && off + 1u + n <= len) {
            if(n>=cap)return; /* A shortened path can select another file. */
            size_t copy = n;
            memcpy(dst, &data[off + 1u], copy);
            dst[copy] = '\0';
            while (copy > 0u && dst[copy - 1u] == '\0') {
                copy--;
            }
            dst[copy] = '\0';
        }
        return;
    }
    if (off + 3u >= len) {
        return;
    }
    {
        size_t total = (size_t)rd_le16(&data[off + 1u]);
        size_t start = off + 4u;
        size_t n = total >= 4u ? total - 4u : 0u;
        bool wide = (flag & 0x10u) != 0u;
        bool le = (flag & 0x80u) != 0u;
        if (n == 0u || start + n > len) {
            return;
        }
        if (wide) {
            size_t units = n / 2u;
            size_t pos = 0;
            size_t i;
            for (i = 0; i < units; i++) {
                uint16_t wc = utf16_unit(&data[start + i * 2u], le);
                uint32_t cp = wc;
                if (wc == 0u) {
                    break;
                }
                if (wc >= 0xD800u && wc <= 0xDBFFu && i + 1u < units) {
                    uint16_t lo = utf16_unit(&data[start + (i + 1u) * 2u], le);
                    if (lo >= 0xDC00u && lo <= 0xDFFFu) {
                        cp = 0x10000u + ((((uint32_t)wc - 0xD800u) << 10) | ((uint32_t)lo - 0xDC00u));
                        i++;
                    }
                }
                if (cp == '\\') {
                    cp = '/';
                }
                if (!utf8_append(dst, cap, &pos, cp)) {
                    dst[0]='\0';return;
                }
            }
            dst[pos] = '\0';
        } else {
            if(n>=cap)return;
            size_t copy = n;
            memcpy(dst, &data[start], copy);
            dst[copy] = '\0';
            while (copy > 0u && dst[copy - 1u] == '\0') {
                copy--;
            }
            dst[copy] = '\0';
        }
    }
}

static void row_string(const uint8_t *page, size_t page_len, size_t row, uint32_t idx,
                       char *dst, size_t cap)
{
    size_t op = row + TRACK_OFF_STR_OFFS + (size_t)idx * 2u;
    dst[0] = '\0';
    if (op + 2u <= page_len) {
        uint16_t soff = rd_le16(&page[op]);
        if (soff > 0u) {
            devicesql(page, page_len, row + soff, dst, cap);
        }
    }
}

// Matching track row in this page, filled into out. Row-slot groups of <= 16
// sit at the page end, growing backwards: [ptr-4] rowpf bitmask,
// [ptr-4-2*(i+1)] heap offset of row i.
static bool page_find(const uint8_t *page, size_t page_len, uint32_t id, dj_link_pdb_track_t *out)
{
    uint32_t n = rd_le32(&page[PAGE_NROWS_OFF]) & 0x1FFFu;
    size_t ptr = page_len;
    uint32_t idx = 0;
    while (idx < n) {
        uint32_t m = n - idx < 16u ? n - idx : 16u;
        size_t group = 4u + (size_t)m * 2u;
        uint16_t rowpf;
        uint32_t i;
        if (ptr < group) {
            break;
        }
        rowpf = rd_le16(&page[ptr - 4u]);
        for (i = 0; i < m; i++) {
            size_t slot = ptr - 4u - 2u * (i + 1u);
            size_t row;
            char title[DJ_LINK_PDB_TITLE_MAX];
            if (!(rowpf & (1u << i))) {
                continue;
            }
            row = PAGE_HEAP_OFFSET + (size_t)rd_le16(&page[slot]);
            if (row + TRACK_ROW_MIN_SIZE > page_len ||
                rd_le16(&page[row]) != TRACK_ROW_SUBTYPE ||
                rd_le32(&page[row + TRACK_OFF_TRACK_ID]) != id) {
                continue;
            }
            memset(out, 0, sizeof(*out));
            out->rekordbox_id = id;
            out->bpm100 = rd_le32(&page[row + TRACK_OFF_TEMPO]);
            out->duration_s = rd_le16(&page[row + TRACK_OFF_DURATION]);
            row_string(page, page_len, row, STR_IDX_FILE_PATH, out->file_path, sizeof(out->file_path));
            row_string(page, page_len, row, STR_IDX_ANLZ_PATH, out->anlz_path, sizeof(out->anlz_path));
            row_string(page, page_len, row, STR_IDX_TITLE, title, sizeof(title));
            if (title[0] == '\0') {
                row_string(page, page_len, row, STR_IDX_FILENAME, title, sizeof(title));
            }
            memcpy(out->title, title, sizeof(out->title));
            return true;
        }
        ptr -= group;
        idx += m;
    }
    return false;
}

dj_link_pdb_result_t dj_link_pdb_find_track(dj_link_pdb_read_fn read, void *ctx,
                                            uint32_t file_size, uint32_t rekordbox_id,
                                            uint8_t *page_buf, size_t page_buf_len,
                                            dj_link_pdb_track_t *out)
{
    uint8_t header[PDB_HEADER_LEN + TABLE_MAX * TABLE_PTR_LEN];
    uint32_t page_size;
    uint32_t num_tables;
    uint32_t total_pages;
    uint32_t i;
    bool found_table = false;

    if (read == NULL || page_buf == NULL || out == NULL || rekordbox_id == 0u) {
        return DJ_LINK_PDB_NOT_FOUND;
    }
    if (file_size < PDB_HEADER_LEN || !read(ctx, 0, header, PDB_HEADER_LEN)) {
        return file_size < PDB_HEADER_LEN ? DJ_LINK_PDB_BAD_FILE : DJ_LINK_PDB_READ_ERROR;
    }
    page_size = rd_le32(&header[4]);
    num_tables = rd_le32(&header[8]);
    if (page_size < PAGE_HEAP_OFFSET || page_size > DJ_LINK_PDB_PAGE_MAX || page_size > file_size) {
        return DJ_LINK_PDB_BAD_FILE;
    }
    if (page_buf_len < page_size) {
        return DJ_LINK_PDB_BUFFER_SMALL;
    }
    total_pages = file_size / page_size;
    if (num_tables > TABLE_MAX) {
        num_tables = TABLE_MAX;
    }
    // Table pointers live in page 0 right after the header.
    if (PDB_HEADER_LEN + num_tables * TABLE_PTR_LEN > page_size) {
        num_tables = (page_size - PDB_HEADER_LEN) / TABLE_PTR_LEN;
    }
    if (num_tables > 0u &&
        !read(ctx, PDB_HEADER_LEN, &header[PDB_HEADER_LEN], (size_t)num_tables * TABLE_PTR_LEN)) {
        return DJ_LINK_PDB_READ_ERROR;
    }

    for (i = 0; i < num_tables; i++) {
        const uint8_t *tp = &header[PDB_HEADER_LEN + i * TABLE_PTR_LEN];
        uint32_t page = rd_le32(&tp[8]);
        uint32_t visited = 0;
        if (rd_le32(tp) != TABLE_TYPE_TRACKS) {
            continue;
        }
        found_table = true;
        // A corrupt next-page chain can loop; each page is read at most once.
        while (page != 0xFFFFFFFFu && page < total_pages && visited++ < total_pages) {
            if (!read(ctx, page * page_size, page_buf, page_size)) {
                return DJ_LINK_PDB_READ_ERROR;
            }
            if (page_find(page_buf, page_size, rekordbox_id, out)) {
                return DJ_LINK_PDB_FOUND;
            }
            page = rd_le32(&page_buf[PAGE_NEXT_OFF]);
        }
        break;
    }
    return found_table ? DJ_LINK_PDB_NOT_FOUND : DJ_LINK_PDB_BAD_FILE;
}

void dj_link_pdb_extension(const char *path, char *out, size_t cap)
{
    const char *dot;
    const char *slash;
    size_t n;
    size_t i;
    if (out == NULL || cap == 0u) {
        return;
    }
    out[0] = '\0';
    if (path == NULL) {
        return;
    }
    dot = strrchr(path, '.');
    slash = strrchr(path, '/');
    if (dot == NULL || (slash != NULL && dot < slash)) {
        return;
    }
    n = strlen(dot + 1);
    if (n == 0u || n >= cap) {
        return;
    }
    for (i = 0; i < n; i++) {
        char ch = dot[1 + i];
        out[i] = (ch >= 'a' && ch <= 'z') ? (char)(ch - 'a' + 'A') : ch;
    }
    out[n] = '\0';
}

bool dj_link_pdb_stamp_matches(const dj_link_pdb_stamp_t *stamp, const dj_link_pdb_stamp_t *now)
{
    return stamp && now && stamp->valid && now->valid && stamp->ip == now->ip &&
           stamp->peer == now->peer && stamp->size == now->size &&
           (stamp->mtime_s != 0u || stamp->mtime_us != 0u) &&
           stamp->mtime_s == now->mtime_s && stamp->mtime_us == now->mtime_us;
}
