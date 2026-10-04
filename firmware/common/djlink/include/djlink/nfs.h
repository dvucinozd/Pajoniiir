#pragma once

#include "djlink/packet.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Read-only NFSv2 client for the media a player exports (0.3.0).
 *
 * Players serve their mounted media over NFSv2 on UDP: the portmapper
 * (111) resolves the MOUNT and NFS programs, MOUNT MNT of the slot export
 * returns the root file handle, then LOOKUP walks the path one element at a
 * time and READ fetches the file. This is how crate-digger / beat-link
 * download export.pdb and analysis files from real CDJs.
 *
 * Protocol references: RFC 1057 (ONC RPC v2, AUTH_UNIX), RFC 1094 (NFSv2,
 * MOUNT v1), RFC 1833 (portmapper v2). Player behaviour (exports, UTF-16LE
 * names, one path element per LOOKUP, 250 ms retransmit, no unmount needed)
 * learned from Deep Symmetry's crate-digger FileFetcher - no code from that
 * project is used here. All XDR integers are big-endian.
 *
 * Like the rest of the library this is pure C99 with no I/O: the owner
 * sends the datagrams the client builds, feeds back what it receives and
 * calls poll() for retransmits. The file is streamed to a write callback
 * in order, so it can go straight to a cache file.
 */

/* --- ONC RPC / program numbers ------------------------------------------ */
#define DJLINK_RPC_VERSION        2u
#define DJLINK_RPC_CALL           0u
#define DJLINK_RPC_REPLY          1u
#define DJLINK_RPC_MSG_ACCEPTED   0u
#define DJLINK_RPC_MSG_DENIED     1u
#define DJLINK_RPC_SUCCESS        0u /* accept_stat */
#define DJLINK_RPC_PROG_UNAVAIL   1u
#define DJLINK_RPC_PROC_UNAVAIL   3u
#define DJLINK_RPC_GARBAGE_ARGS   4u
#define DJLINK_RPC_AUTH_NONE      0u
#define DJLINK_RPC_AUTH_UNIX      1u

#define DJLINK_PMAP_PORT          111u
#define DJLINK_PMAP_PROG          100000u
#define DJLINK_PMAP_VERS          2u
#define DJLINK_PMAP_PROC_GETPORT  3u
#define DJLINK_IPPROTO_UDP        17u

#define DJLINK_MOUNT_PROG         100005u
#define DJLINK_MOUNT_VERS         1u
#define DJLINK_MOUNT_PROC_MNT     1u

#define DJLINK_NFS_PROG           100003u
#define DJLINK_NFS_VERS           2u
#define DJLINK_NFS_PROC_LOOKUP    4u
#define DJLINK_NFS_PROC_READ      6u

/* NFSv2 limits and status codes (nfsstat; MOUNT uses the same errno set). */
#define DJLINK_NFS_FHSIZE         32u
#define DJLINK_NFS_MAXDATA        8192u
#define DJLINK_NFS_FATTR_LEN      68u
#define DJLINK_NFS_OK             0u
#define DJLINK_NFSERR_NOENT       2u
#define DJLINK_NFSERR_IO          5u
#define DJLINK_NFSERR_ACCES       13u
#define DJLINK_NFSERR_NOTDIR      20u
#define DJLINK_NFSERR_ISDIR       21u
#define DJLINK_NFSERR_STALE       70u
#define DJLINK_NFS_TYPE_REG       1u /* ftype */
#define DJLINK_NFS_TYPE_DIR       2u

/* Player exports (crate-digger): SD slot "/B/", USB slot "/C/". */
#define DJLINK_NFS_EXPORT_SD      "/B/"
#define DJLINK_NFS_EXPORT_USB     "/C/"

/* A 1024-byte READ reply (~1.1 KB) fits one 1500-byte Ethernet frame, so it
 * needs no IP reassembly (off by default in ESP-IDF lwIP). crate-digger
 * reads 2048 and accepts 1024..8192. */
#define DJLINK_NFS_READ_DEFAULT   1024u
#define DJLINK_NFS_READ_MIN       512u
#define DJLINK_NFS_RETRANSMIT_MS  250u  /* crate-digger default, doubles per retry */
#define DJLINK_NFS_RETRIES        5u
#define DJLINK_NFS_WINDOW_MAX     8u    /* READs in flight */
#define DJLINK_NFS_PATH_MAX       256u  /* UTF-8 bytes incl. NUL */
#define DJLINK_NFS_NAME_MAX       510u  /* encoded bytes of one path element */
#define DJLINK_NFS_TX_MAX         640u  /* largest call built (LOOKUP) */

typedef struct {
    uint32_t type;       /* DJLINK_NFS_TYPE_* */
    uint32_t mode;
    uint32_t nlink;
    uint32_t uid;
    uint32_t gid;
    uint32_t size;
    uint32_t blocksize;
    uint32_t rdev;
    uint32_t blocks;
    uint32_t fsid;
    uint32_t fileid;
    uint32_t atime_s, atime_us;
    uint32_t mtime_s, mtime_us;
    uint32_t ctime_s, ctime_us;
} djlink_nfs_fattr_t;

/* --- Names --------------------------------------------------------------- */

typedef enum {
    DJLINK_NFS_NAMES_UTF16LE = 0, /* players: mount path and every element */
    DJLINK_NFS_NAMES_BYTES,       /* plain servers: UTF-8 bytes as given */
} djlink_nfs_charset_t;

/* Encode a UTF-8 name (len bytes, no NUL needed). Returns the encoded byte
 * count, or DJLINK_ERR_BOUNDS / DJLINK_ERR_TYPE (invalid UTF-8). */
int djlink_nfs_name_encode(const char *utf8, size_t len, djlink_nfs_charset_t cs,
                           uint8_t *out, size_t cap);

/* --- RPC call / reply codec ---------------------------------------------- */

/* Calls carry AUTH_UNIX (stamp 0, empty machine name, uid 0, gid 0, no
 * groups) and a null verifier. Every builder returns the datagram length or
 * a negative djlink_err_t. */
int djlink_pmap_getport_build(uint8_t *out, size_t cap, uint32_t xid,
                              uint32_t prog, uint32_t vers);
int djlink_mount_mnt_build(uint8_t *out, size_t cap, uint32_t xid,
                           const uint8_t *dirpath, size_t dirpath_len);
int djlink_nfs_lookup_build(uint8_t *out, size_t cap, uint32_t xid,
                            const uint8_t fh[DJLINK_NFS_FHSIZE],
                            const uint8_t *name, size_t name_len);
int djlink_nfs_read_build(uint8_t *out, size_t cap, uint32_t xid,
                          const uint8_t fh[DJLINK_NFS_FHSIZE],
                          uint32_t offset, uint32_t count);

typedef struct {
    uint32_t xid;
    uint32_t reply_stat;  /* DJLINK_RPC_MSG_* */
    uint32_t accept_stat; /* when accepted; the reject_stat when denied */
    const uint8_t *res;   /* results, valid while the input buffer is */
    size_t res_len;
} djlink_rpc_reply_t;

/* Any well-formed reply parses (denied / unsuccessful ones too); the
 * caller checks reply_stat and accept_stat. */
djlink_err_t djlink_rpc_reply_parse(const uint8_t *buf, size_t len, djlink_rpc_reply_t *out);

djlink_err_t djlink_pmap_getport_parse(const uint8_t *res, size_t len, uint16_t *port);
/* status != 0 leaves fh untouched. */
djlink_err_t djlink_mount_mnt_parse(const uint8_t *res, size_t len, uint32_t *status,
                                    uint8_t fh[DJLINK_NFS_FHSIZE]);
djlink_err_t djlink_nfs_lookup_parse(const uint8_t *res, size_t len, uint32_t *status,
                                     uint8_t fh[DJLINK_NFS_FHSIZE],
                                     djlink_nfs_fattr_t *attr);
/* data points into res. */
djlink_err_t djlink_nfs_read_parse(const uint8_t *res, size_t len, uint32_t *status,
                                   djlink_nfs_fattr_t *attr,
                                   const uint8_t **data, size_t *data_len);

/* Server side (mocks now, a player export later). */
typedef struct {
    uint32_t xid;
    uint32_t prog, vers, proc;
    uint32_t cred_flavor;
    uint32_t uid;         /* AUTH_UNIX only */
    const uint8_t *args;  /* valid while the input buffer is */
    size_t args_len;
} djlink_rpc_call_t;

djlink_err_t djlink_rpc_call_parse(const uint8_t *buf, size_t len, djlink_rpc_call_t *out);
/* Accepted reply with a null verifier; res may be NULL when res_len is 0. */
int djlink_rpc_reply_build(uint8_t *out, size_t cap, uint32_t xid, uint32_t accept_stat,
                           const uint8_t *res, size_t res_len);

void djlink_nfs_fattr_encode(const djlink_nfs_fattr_t *attr, uint8_t out[DJLINK_NFS_FATTR_LEN]);
void djlink_nfs_fattr_decode(const uint8_t in[DJLINK_NFS_FATTR_LEN], djlink_nfs_fattr_t *attr);

/* --- Fetch client -------------------------------------------------------- */

typedef enum {
    DJLINK_NFS_IDLE = 0,
    DJLINK_NFS_BUSY,
    DJLINK_NFS_DONE,
    DJLINK_NFS_FAILED,
    DJLINK_NFS_CANCELLED,
} djlink_nfs_state_t;

typedef enum {
    DJLINK_NFS_PH_GETPORT_MOUNT = 0,
    DJLINK_NFS_PH_GETPORT_NFS,
    DJLINK_NFS_PH_MOUNT,
    DJLINK_NFS_PH_LOOKUP,
    DJLINK_NFS_PH_READ,
} djlink_nfs_phase_t;

typedef enum {
    DJLINK_NFS_E_NONE = 0,
    DJLINK_NFS_E_ARG,        /* bad config / path / name encoding */
    DJLINK_NFS_E_TIMEOUT,    /* no reply after every retransmit */
    DJLINK_NFS_E_RPC,        /* call denied or not successful */
    DJLINK_NFS_E_NO_SERVICE, /* portmapper: program not registered */
    DJLINK_NFS_E_MOUNT,      /* export refused; status holds the errno */
    DJLINK_NFS_E_LOOKUP,     /* path element; status holds the nfsstat */
    DJLINK_NFS_E_NOT_FILE,   /* the path ends on a directory */
    DJLINK_NFS_E_TOO_BIG,    /* larger than cfg.max_size */
    DJLINK_NFS_E_READ,       /* READ nfsstat, or no data before EOF */
    DJLINK_NFS_E_SINK,       /* open / write hook refused */
    DJLINK_NFS_E_BAD_REPLY,  /* reply to our xid that does not parse */
} djlink_nfs_error_t;

typedef struct {
    /* One datagram to ip:port (host order). A failure counts as a lost
     * datagram: the retransmit timer covers it. */
    int  (*send)(void *ctx, uint32_t ip, uint16_t port, const uint8_t *buf, size_t len);
    /* Optional: the file was found; size in bytes. Nonzero refuses it. */
    int  (*open)(void *ctx, uint32_t size);
    /* File bytes, strictly in order (offset = bytes delivered so far). */
    int  (*write)(void *ctx, uint32_t offset, const uint8_t *data, size_t len);
    /* Optional: after each write. */
    void (*progress)(void *ctx, uint32_t done, uint32_t total);
    void *ctx;
} djlink_nfs_io_t;

typedef struct {
    uint32_t host_ip;          /* host order */
    const char *export_path;   /* e.g. DJLINK_NFS_EXPORT_USB */
    const char *path;          /* UTF-8, '/'-separated, leading '/' optional */
    djlink_nfs_charset_t charset;
    uint16_t portmap_port;     /* 0 = 111 (host tests use an ephemeral port) */
    uint32_t read_size;        /* 0 = DJLINK_NFS_READ_DEFAULT; 512..8192 */
    uint8_t  window;           /* READs in flight, 0 = 1; capped as below */
    uint8_t *window_buf;       /* reorder storage: window * read_size bytes; */
    size_t   window_buf_len;   /* without it the window is 1 (no reordering) */
    uint32_t retransmit_ms;    /* 0 = 250; doubles on every retry */
    uint8_t  retries;          /* 0 = 5 retransmits per request */
    uint32_t max_size;         /* 0 = no limit */
    uint32_t xid_seed;         /* first xid; vary it per fetch */
} djlink_nfs_fetch_cfg_t;

typedef struct {
    uint32_t xid;
    uint32_t offset;
    uint32_t count;
    uint32_t len;       /* bytes received */
    uint32_t sent_ms;
    uint8_t  tries;
    uint8_t  state;     /* 0 free, 1 waiting, 2 received */
} djlink_nfs_slot_t;

typedef struct {
    djlink_nfs_io_t io;
    djlink_nfs_fetch_cfg_t cfg;          /* pointers are replaced by the copies below */
    char export_path[DJLINK_NFS_PATH_MAX];
    char path[DJLINK_NFS_PATH_MAX];

    djlink_nfs_state_t state;
    djlink_nfs_phase_t phase;
    djlink_nfs_error_t error;
    uint32_t status;                     /* nfsstat / mount errno / accept_stat */
    char error_text[48];

    uint16_t mount_port;
    uint16_t nfs_port;
    uint8_t  fh[DJLINK_NFS_FHSIZE];
    size_t   path_pos;                   /* next element in path[] */
    uint32_t size;                       /* file size from LOOKUP */
    djlink_nfs_fattr_t attr;             /* the file's LOOKUP attributes, valid
                                          * from io.open on (size, mtime) */
    uint32_t next_xid;

    /* Single request in flight (every phase but READ). */
    uint32_t req_xid;
    uint16_t req_port;
    uint32_t req_sent_ms;
    uint8_t  req_tries;
    size_t   tx_len;
    uint8_t  tx[DJLINK_NFS_TX_MAX];

    /* READ window. */
    uint8_t  window;
    uint32_t read_size;
    uint32_t requested;                  /* next offset to request */
    uint32_t delivered;                  /* bytes written to the sink */
    djlink_nfs_slot_t slots[DJLINK_NFS_WINDOW_MAX];

    /* Counters for logs and tests. */
    uint32_t retransmits;
    uint32_t stray_replies;              /* unknown / duplicate xid */
} djlink_nfs_t;

/* Start fetching cfg->path from cfg->export_path. Strings are copied. On a
 * bad config the client is FAILED (E_ARG) and the error is returned. */
djlink_nfs_error_t djlink_nfs_fetch(djlink_nfs_t *c, const djlink_nfs_fetch_cfg_t *cfg,
                                    const djlink_nfs_io_t *io, uint32_t now_ms);
/* A datagram received on the client's socket (any source; unknown xids are
 * ignored). */
void djlink_nfs_on_datagram(djlink_nfs_t *c, const uint8_t *buf, size_t len, uint32_t now_ms);
/* Retransmits and timeouts. */
void djlink_nfs_poll(djlink_nfs_t *c, uint32_t now_ms);
/* Stop; nothing more is sent. Players keep no mount list: no unmount. */
void djlink_nfs_cancel(djlink_nfs_t *c);

djlink_nfs_state_t djlink_nfs_state(const djlink_nfs_t *c);
/* Short upper-case reason, e.g. "EXPORT REFUSED (2)", "TIMEOUT READ". */
const char *djlink_nfs_error_text(const djlink_nfs_t *c);

#ifdef __cplusplus
}
#endif
