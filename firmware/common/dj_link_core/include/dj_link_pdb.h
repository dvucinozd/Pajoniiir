#pragma once
//
// dj_link_pdb — pure (host-testable) lookup of one track in a peer's
// export.pdb, v249: the dbserver lists and describes a peer's tracks but
// never says where the audio file lives, so the fetch job downloads the
// peer's PIONEER/rekordbox/export.pdb over NFS and resolves the rekordbox id
// to its file path here.
//
// The layout is the one library/rekordbox_pdb.c parses (little-endian pages,
// row-slot groups at the page end, DeviceSQL strings); that parser is tied to
// the local USB gate and caps the index at 1024 tracks, so this module walks
// the Tracks table one page at a time through a read callback and stops at
// the first matching id. No allocation: the caller lends a page buffer.
//

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DJ_LINK_PDB_PATH_MAX   256u  /* UTF-8 bytes incl. NUL */
#define DJ_LINK_PDB_TITLE_MAX  96u
#define DJ_LINK_PDB_PAGE_MAX   65536u

/* Read len bytes at offset; false on any I/O error. */
typedef bool (*dj_link_pdb_read_fn)(void *ctx, uint32_t offset, uint8_t *dst, size_t len);

typedef struct {
    uint32_t rekordbox_id;
    char     file_path[DJ_LINK_PDB_PATH_MAX]; /* "/Contents/..." on the peer media */
    char     anlz_path[DJ_LINK_PDB_PATH_MAX];
    char     title[DJ_LINK_PDB_TITLE_MAX];    /* tagged title, else file name */
    uint16_t duration_s;
    uint32_t bpm100;
} dj_link_pdb_track_t;

typedef enum {
    DJ_LINK_PDB_FOUND = 0,
    DJ_LINK_PDB_NOT_FOUND,
    DJ_LINK_PDB_BAD_FILE,     /* header / page size / no Tracks table */
    DJ_LINK_PDB_READ_ERROR,
    DJ_LINK_PDB_BUFFER_SMALL, /* page_buf_len < the file's page size */
} dj_link_pdb_result_t;

dj_link_pdb_result_t dj_link_pdb_find_track(dj_link_pdb_read_fn read, void *ctx,
                                            uint32_t file_size, uint32_t rekordbox_id,
                                            uint8_t *page_buf, size_t page_buf_len,
                                            dj_link_pdb_track_t *out);

/* File extension of path (".MP3" -> "MP3"), upper-cased into out; "" if none
 * or longer than cap - 1. */
void dj_link_pdb_extension(const char *path, char *out, size_t cap);

/* v303: which export.pdb the cached copy is: the peer and the NFS
 * attributes it was read with. v302 trusted the copy for as long as the
 * browse selection stayed (generation), so a source edit (re-analysis,
 * cues, a new track) under the same selection never reached the deck. */
typedef struct {
    bool     valid;
    uint32_t ip;
    uint8_t  peer;
    uint32_t size;
    uint32_t mtime_s;
    uint32_t mtime_us;
} dj_link_pdb_stamp_t;

/* Donor optimization hint only; NEVER a media/content identity or a cache-hit
 * authorization. The J runtime deliberately does not use it. A zero mtime is
 * unknown, never a match. */
bool dj_link_pdb_stamp_matches(const dj_link_pdb_stamp_t *stamp, const dj_link_pdb_stamp_t *now);

#ifdef __cplusplus
}
#endif
