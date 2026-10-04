#pragma once
//
// dj_link_db — pure (host-testable) client for a peer's rekordbox database
// server (TCP), v248: list the tracks of a peer's USB and fetch per-track
// metadata on demand, so the Library can browse another player's media.
//
// Sans-I/O: the client never touches a socket. The owner supplies connect /
// send / close hooks and feeds back connection events and received bytes;
// the client builds every request with the esp-djlink dbserver codec and
// reports tracks through callbacks. dj_link.c drives it from the dj_link task
// over a raw lwIP TCP PCB; test/host drives it over real localhost sockets
// against a mock peer.
//
// Flow (djl-analysis track_metadata.html, "Database Server"):
//   connect peer:12523 -> "RemoteDBServer" port query -> u16 port -> close
//   connect peer:port  -> greeting 11 00 00 00 01 (echoed)
//   context setup (type 0, our player number) -> 0x4000
//   all-tracks menu 0x1004 (DMST, sort) -> 0x4000 [.., item count]
//   render 0x3000 in batches -> 0x4001 header, 0x4101 items, 0x4201 footer
//   per track on demand: metadata 0x2002 (DMST, id) -> 0x4000 -> render
//   v297, file path on demand: track info 0x2102 (DMST, id) -> 0x4000 ->
//   render; the item of type 0x0000 carries the path from the NFS export
//   root (rekordbox sources, which have no export.pdb to resolve it from)
//   v300, analysis on demand: wave detail 0x2904 (DMST, id, 0) -> 0x4a02 /
//   beat grid 0x2204 (DMST, id) -> 0x4602; the blob argument streams into
//   the owner's buffer, so these replies may exceed DJ_LINK_DB_RX_MAX;
//   artwork 0x2003 (DMST, artwork id) -> 0x4002, blob = the JPEG. The
//   artwork id is argument 8 of a track's title menu item. A non-zero
//   status (vynull 0x32 "not found") declares a blob it never sends.
//   v303, cues: nxs2 cue list 0x2b04 (DMST, id, 0) -> 0x4e02
//   [request, status, len, blob, count]; the blob is the cue entries
//   (dj_link_anlz_cue). vynull's empty answer sends a zero int32 where
//   the declared blob would be.
// One connection, one request in flight. The dbserver only serves metadata;
// v249 fetches the audio over NFS (esp-djlink nfs.h, dj_link_fetch_*), so
// the owner marks listed tracks DJ_LINK_PEER_AUDIO_NFS when that path exists.
//
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DJ_LINK_DB_TITLE_MAX        80u   /* UTF-8 bytes incl. NUL */
#define DJ_LINK_DB_ARTIST_MAX       64u
#define DJ_LINK_DB_STEP_TIMEOUT_MS  3000u /* per connect / reply */
#define DJ_LINK_DB_IDLE_CLOSE_MS    20000u
#define DJ_LINK_DB_RENDER_BATCH     64u
#define DJ_LINK_DB_RX_MAX           4096u /* largest single reply buffered */
#define DJ_LINK_DB_TX_MAX           128u
#define DJ_LINK_DB_TRACK_LIMIT      2000u

/* Menu item types (arg 6) used here. */
#define DJ_LINK_DB_ITEM_TITLE       0x0004u
#define DJ_LINK_DB_ITEM_ARTIST      0x0007u
#define DJ_LINK_DB_ITEM_DURATION    0x000bu
#define DJ_LINK_DB_ITEM_TEMPO       0x000du
#define DJ_LINK_DB_ITEM_TITLE_ARTIST 0x0704u
#define DJ_LINK_DB_ITEM_TEMPO_TITLE 0x0d04u /* v307: vynull's default list row */

#define DJ_LINK_DB_ITEM_FILE_PATH   0x0000u /* track info: label 1 = path */
#define DJ_LINK_DB_TYPE_TRACK_MENU  0x1004u /* all tracks, arg: sort order */
/* v310: sort orders of the track menus (arg 1), as CDJs send them and
 * vynull applies them (dbserver/menuitem.go). Ascending only. */
#define DJ_LINK_DB_SORT_DEFAULT     0x00u /* the player's own order */
#define DJ_LINK_DB_SORT_TITLE       0x01u
#define DJ_LINK_DB_SORT_ARTIST      0x02u
#define DJ_LINK_DB_SORT_BPM         0x04u
#define DJ_LINK_DB_SORT_KEY         0x0cu

/* v311: playlist menu 0x1105 [DMST, sort, id, folder]: folder 1 lists the
 * folders and playlists in folder `id` (0 = root), 0 the tracks of playlist
 * `id` (beat-link MenuLoader, vynull dbserver/playlist.go). Row item types:
 * 0x0001 folder, 0x0008 playlist. */
#define DJ_LINK_DB_TYPE_PLAYLIST_MENU 0x1105u

/* v314: one ANLZ section of a track: 0x2c04 [DMST, id, tag, file] ->
 * 0x4f02 [request, status, len, blob, 1]. tag and file are the fourccs
 * byte-reversed ("PWV4" -> 0x34565750, "EXT" -> 0x00545845; beat-link
 * AnlzTagFinder). The blob is a little-endian u32 section length, then the
 * whole section (fourcc, header, entries) as in the file (vynull
 * dbserver/track.go, analysis.ReadANLZSection). */
#define DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST 0x2c04u
#define DJ_LINK_DB_TYPE_ANLZ_TAG_REPLY   0x4f02u
#define DJ_LINK_DB_ANLZ_TAG_PWV4         0x34565750u
#define DJ_LINK_DB_ANLZ_FILE_EXT         0x00545845u
#define DJ_LINK_DB_ITEM_FOLDER      0x0001u
#define DJ_LINK_DB_ITEM_PLAYLIST    0x0008u

typedef enum {
    DJ_LINK_DB_MENU_ALL_TRACKS = 0,  /* 0x1004, sorted */
    DJ_LINK_DB_MENU_FOLDER,          /* 0x1105 folder `id` */
    DJ_LINK_DB_MENU_PLAYLIST,        /* 0x1105 playlist `id`, its order */
} dj_link_db_menu_t;

typedef enum {
    DJ_LINK_PEER_ROW_TRACK = 0,
    DJ_LINK_PEER_ROW_FOLDER,         /* rekordbox_id holds the folder id */
    DJ_LINK_PEER_ROW_PLAYLIST,       /* rekordbox_id holds the playlist id */
} dj_link_peer_row_t;
#define DJ_LINK_DB_TYPE_TRACK_INFO  0x2102u /* v297: path, duration, tempo, key */
#define DJ_LINK_DB_PATH_MAX         256u  /* UTF-8 bytes incl. NUL */

typedef enum {
    /* dbserver metadata only: no way to reach the audio. */
    DJ_LINK_PEER_AUDIO_METADATA_ONLY = 0,
    /* v249: downloadable from the peer's NFS export to the SD cache. */
    DJ_LINK_PEER_AUDIO_NFS,
} dj_link_peer_audio_t;

typedef struct {
    uint32_t rekordbox_id;
    char     title[DJ_LINK_DB_TITLE_MAX];
    char     artist[DJ_LINK_DB_ARTIST_MAX];
    uint16_t duration_s;    /* 0 = unknown (detail not fetched) */
    uint16_t bpm100;        /* 0 = unknown */
    bool     has_detail;    /* metadata request done (even if it failed) */
    uint32_t artwork_id;    /* v300: from the title item, 0 = none */
    dj_link_peer_audio_t audio;
    uint8_t  kind;          /* v311: dj_link_peer_row_t; not a track = no
                             * metadata, no download, opens a menu */
} dj_link_peer_track_t;

typedef enum {
    DJ_LINK_DB_IDLE = 0,        /* no connection */
    DJ_LINK_DB_DISC_CONNECTING,
    DJ_LINK_DB_DISC_WAIT_PORT,
    DJ_LINK_DB_SRV_CONNECTING,
    DJ_LINK_DB_SRV_WAIT_GREETING,
    DJ_LINK_DB_SRV_WAIT_SETUP,
    DJ_LINK_DB_READY,           /* session open, nothing in flight */
    DJ_LINK_DB_LIST_WAIT_AVAIL,
    DJ_LINK_DB_LIST_WAIT_RENDER,
    DJ_LINK_DB_DETAIL_WAIT_AVAIL,
    DJ_LINK_DB_DETAIL_WAIT_RENDER,
    DJ_LINK_DB_PATH_WAIT_AVAIL,     /* v297 */
    DJ_LINK_DB_PATH_WAIT_RENDER,
    DJ_LINK_DB_BLOB_WAIT,           /* v300 */
    DJ_LINK_DB_FAILED,          /* closed; error() says why; restart to retry */
} dj_link_db_phase_t;

typedef struct {
    /* Transport. connect starts an asynchronous connect (0 = started) and
     * the owner later calls dj_link_db_on_connected() or _on_closed(). */
    int  (*connect)(void *ctx, uint32_t ip, uint16_t port);
    int  (*send)(void *ctx, const uint8_t *buf, size_t len);   /* 0 = queued */
    void (*close)(void *ctx);
    /* Results. list_begin: item count the peer reported (before capping);
     * track: row `index` listed (detail false) or its metadata (true). */
    void (*list_begin)(void *ctx, uint32_t total);
    void (*track)(void *ctx, uint32_t index, const dj_link_peer_track_t *t, bool detail);
    /* Next row wanting metadata: its index and rekordbox id, false if none. */
    bool (*next_detail)(void *ctx, uint32_t *index, uint32_t *rekordbox_id);
    /* v297: answer to dj_link_db_want_path; path is "" if the peer has none. */
    void (*path)(void *ctx, uint32_t rekordbox_id, const char *path);
    /* v300: answer to dj_link_db_want_blob; len = bytes stored in its
     * buffer (0 = the peer has none). v303: answered = the reply had the
     * expected type, so len 0 is the peer saying "none" rather than an
     * error or unsupported request. */
    void (*blob)(void *ctx, uint32_t rekordbox_id, uint16_t request, size_t len,
                 bool answered);
    void *ctx;
} dj_link_db_io_t;

typedef struct {
    dj_link_db_io_t io;
    uint16_t discovery_port;    /* 12523; host tests use an ephemeral port */
    uint32_t step_timeout_ms;
    uint32_t max_tracks;        /* list cap (the owner's buffer size) */
    dj_link_peer_audio_t audio; /* stamped on every listed track */

    uint32_t peer_ip;           /* host order */
    uint8_t  peer_number;
    uint8_t  slot;              /* DJLINK_SLOT_USB */
    uint8_t  our_number;
    uint64_t source_epoch;      /* runtime peer/media generation, never cache identity */
    uint64_t connection_epoch;  /* callbacks from old TCP sessions are ignored */

    dj_link_db_phase_t phase;
    uint32_t deadline_ms;
    uint32_t idle_since_ms;
    uint32_t txid;
    bool     list_wanted;
    bool     list_done;
    uint32_t list_total;        /* reported by the peer */
    uint32_t list_target;       /* min(total, max_tracks) */
    uint8_t  sort;              /* v310: DJ_LINK_DB_SORT_*, kept by start */
    uint8_t  menu;              /* v311: dj_link_db_menu_t, kept by start */
    uint32_t menu_id;           /* v311: folder / playlist id */
    uint32_t render_offset;
    uint32_t render_count;
    uint32_t render_seen;
    uint32_t detail_index;
    uint32_t detail_count;
    dj_link_peer_track_t detail;
    uint32_t path_id;           /* v297: wanted file path, 0 = none */
    char     path[DJ_LINK_DB_PATH_MAX];
    uint16_t blob_request;      /* v300: wanted blob request type, 0 = none */
    uint32_t blob_id;           /* 0 = cancelled: drain without answering */
    uint8_t *blob_dst;
    size_t   blob_cap;
    size_t   blob_len;          /* bytes stored */
    bool     blob_keep;         /* the reply is the expected type, kept */
    bool     blob_typed;        /* v303: the reply is the expected type */
    bool     blob_body;         /* head consumed: streaming blob then tail */
    uint32_t blob_left;         /* blob bytes still to come */
    uint8_t  blob_tail;         /* integer arguments after the blob */
    char     error[32];

    size_t   rx_len;
    uint8_t  rx[DJ_LINK_DB_RX_MAX];
    uint8_t  tx[DJ_LINK_DB_TX_MAX];
} dj_link_db_t;

/* Reset to IDLE with the given hooks (no I/O). */
void dj_link_db_init(dj_link_db_t *c, const dj_link_db_io_t *io, uint32_t max_tracks);

/* Browse `slot` of player `peer_number` at peer_ip (host order), querying as
 * player our_number. Closes any previous session, then connects and lists. */
void dj_link_db_start(dj_link_db_t *c, uint32_t peer_ip, uint8_t peer_number,
                      uint8_t slot, uint8_t our_number, uint64_t source_epoch, uint32_t now_ms);
/* Close the connection (if any) and go IDLE. */
void dj_link_db_stop(dj_link_db_t *c);
/* v310: the sort order of the next listing (DJ_LINK_DB_SORT_*). Set it
 * before dj_link_db_start; init sets the default. */
void dj_link_db_set_sort(dj_link_db_t *c, uint8_t sort);
/* v311: the menu the next listing reads. A playlist or folder is listed in
 * the player's order (sort 0: playlists keep their order). Set it before
 * dj_link_db_start; init sets all tracks. */
void dj_link_db_set_menu(dj_link_db_t *c, dj_link_db_menu_t menu, uint32_t id);
/* v310: where listed row `index` of `rows` goes: reversed when descending
 * (the protocol only sorts ascending). The mapping is its own inverse. */
uint32_t dj_link_db_row_index(uint32_t rows, bool descending, uint32_t index);
/* v310: the order after a tap on the column of sort `tapped`, the list
 * being in (sort, descending): a new column ascending, the same column
 * reversed, then the player's own order again. */
uint8_t dj_link_db_next_sort(uint8_t sort, bool descending, uint8_t tapped,
                             bool *next_descending);

/* v297: ask for the file path of a listed track, ahead of any metadata; the
 * answer comes through io.path. False if no session can serve it (stopped
 * or FAILED); a later start / stop drops the request without an answer. */
bool dj_link_db_want_path(dj_link_db_t *c, uint32_t rekordbox_id, uint32_t now_ms);

/* v300: ask for one analysis blob of a track (DJLINK_DB_TYPE_WAVEFORM_REQUEST,
 * DJLINK_DB_TYPE_BEATGRID_REQUEST or, v303, DJLINK_DB_TYPE_CUES_EXT_REQUEST;
 * v314, DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST: the PWV4 colour preview section)
 * or its artwork
 * (DJLINK_DB_TYPE_ARTWORK_REQUEST, id = the artwork id); the reply's blob
 * streams into dst (bytes past cap are dropped; an artwork larger than cap
 * is dropped whole, len 0) and the answer comes through io.blob. One
 * blob at a time, after the list and any wanted path. dst must stay valid
 * until the answer or dj_link_db_cancel_blob / _start / _stop. False if no
 * session can serve it or a blob is already wanted. */
bool dj_link_db_want_blob(dj_link_db_t *c, uint16_t request, uint32_t rekordbox_id,
                          uint8_t *dst, size_t cap, uint32_t now_ms);
/* Forget the wanted blob without an answer; dst is never touched again (a
 * reply already streaming is drained). */
void dj_link_db_cancel_blob(dj_link_db_t *c);

void dj_link_db_on_connected(dj_link_db_t *c, uint64_t connection_epoch, uint32_t now_ms);
void dj_link_db_on_data(dj_link_db_t *c, uint64_t connection_epoch,
                        const uint8_t *buf, size_t len, uint32_t now_ms);
/* Remote close or connection error. */
void dj_link_db_on_closed(dj_link_db_t *c, uint64_t connection_epoch, uint32_t now_ms);
/* Timeouts, idle close, and starting the next detail request. */
void dj_link_db_poll(dj_link_db_t *c, uint32_t now_ms);

dj_link_db_phase_t dj_link_db_phase(const dj_link_db_t *c);
bool dj_link_db_list_done(const dj_link_db_t *c);
const char *dj_link_db_error(const dj_link_db_t *c);

/* Byte length of the complete dbserver message at buf, 0 if more bytes are
 * needed, -1 if it can never parse (bad magic / field / too large). */
int dj_link_db_msg_size(const uint8_t *buf, size_t len);

/* UTF-16BE (NUL-terminated or not) to UTF-8, always NUL-terminated;
 * characters outside the BMP become '?'. */
void dj_link_db_utf16be_to_utf8(const uint8_t *in, size_t in_len, char *out, size_t cap);

/* v307: the next row of rows[0, count) wanting metadata for io.next_detail:
 * the priority row first (a track being loaded, wherever it is listed;
 * prio_id must still match), then the first row of the window
 * [first, first + n). False if none. */
bool dj_link_db_pick_detail(const dj_link_peer_track_t *rows, uint32_t count,
                            uint32_t first, uint32_t n, uint32_t prio_index,
                            uint32_t prio_id, uint32_t *index);

typedef enum {
    DJ_LINK_PEER_LOAD_OK = 0,           /* audio reachable (NFS fetch) */
    DJ_LINK_PEER_LOAD_METADATA_ONLY,    /* no audio path for peer media */
    DJ_LINK_PEER_LOAD_NO_TRACK,
} dj_link_peer_load_t;

/* Can this peer track be loaded into a deck? Fills a one-line reason. */
dj_link_peer_load_t dj_link_peer_load_check(const dj_link_peer_track_t *t,
                                            char *reason, size_t cap);

#ifdef __cplusplus
}
#endif
