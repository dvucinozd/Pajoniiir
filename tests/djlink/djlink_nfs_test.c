/* Host tests: NFSv2 codec and the fetch client against a mock player.
 *
 * The mock serves portmapper, MOUNT and NFS on three UDP sockets on
 * 127.0.0.1 from a small in-memory tree, checks the wire format a player
 * expects (AUTH_UNIX uid 0, UTF-16LE names, 32-byte handles) and can lose,
 * duplicate, reorder or shorten READ traffic on demand. */
#define _DEFAULT_SOURCE
#include "djlink.h"
#include "dj_link_cache.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static int failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);            \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        long _a = (long)(a), _b = (long)(b);                                 \
        if (_a != _b) {                                                      \
            printf("FAIL %s:%d: %s == %s (%ld != %ld)\n", __FILE__, __LINE__, \
                   #a, #b, _a, _b);                                          \
            failures++;                                                      \
        }                                                                    \
    } while (0)

#define LOOPBACK 0x7F000001u

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

static int udp_socket(uint16_t *port)
{
    struct sockaddr_in a;
    socklen_t alen = sizeof(a);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&a, 0, sizeof(a));
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(LOOPBACK);
    a.sin_port = 0;
    if (fd < 0 || bind(fd, (struct sockaddr *)&a, sizeof(a)) != 0 ||
        getsockname(fd, (struct sockaddr *)&a, &alen) != 0) {
        perror("udp_socket");
        exit(2);
    }
    *port = ntohs(a.sin_port);
    return fd;
}

/* --- Mock player ---------------------------------------------------------- */

typedef struct {
    uint32_t id;
    uint32_t parent;
    const char *name;
    int is_dir;
    uint32_t size;
} node_t;

#define BIG_SIZE (1536u * 1024u + 777u) /* > 1 MiB, not a multiple of any read size */

/* Export "/C/" = node 1. Names are compared after UTF-16LE decoding. */
static const node_t s_tree[] = {
    { 1, 0, "/C/", 1, 0 },
    { 2, 1, "PIONEER", 1, 0 },
    { 3, 2, "rekordbox", 1, 0 },
    { 4, 3, "export.pdb", 0, 12288 },
    { 5, 1, "Contents", 1, 0 },
    { 6, 5, "Café Artist", 1, 0 },
    { 7, 6, "Album", 1, 0 },
    { 8, 7, "Track.mp3", 0, BIG_SIZE },
    { 9, 1, "empty.bin", 0, 0 },
    { 10, 1, "small.bin", 0, 3000 },
};
#define TREE_LEN (sizeof(s_tree) / sizeof(s_tree[0]))

static uint8_t file_byte(uint32_t id, uint32_t off)
{
    return (uint8_t)((off * 31u) ^ (off >> 9) ^ (id * 77u));
}

typedef struct {
    /* Faults, set before a fetch. READ numbers count READ calls received
     * (retransmits included), starting at 1. */
    int drop_request;   /* ignore this READ call */
    int drop_reply;     /* answer this READ call but lose the reply */
    int dup_reply;      /* send this reply twice */
    int hold_reply;     /* send this reply after the next one */
    int short_reply;    /* answer this READ with half the data */
    int silent_portmap; /* never answer the portmapper */
    int no_mount_prog;  /* portmapper: MOUNT not registered */
    int changed_read;
    int media_variant;

    /* Observations. */
    int getport_calls;
    int mnt_calls;
    int lookup_calls;
    int read_calls;
    int bad_auth;
    int bad_name;
    uint32_t max_count;
} mock_cfg_t;

static pthread_mutex_t s_mock_lock = PTHREAD_MUTEX_INITIALIZER;
static mock_cfg_t s_mock;
static int s_mock_stop;
static int s_pmap_fd, s_mount_fd, s_nfs_fd;
static uint16_t s_pmap_port, s_mount_port, s_nfs_port;

static void mock_reset(void)
{
    pthread_mutex_lock(&s_mock_lock);
    memset(&s_mock, 0, sizeof(s_mock));
    pthread_mutex_unlock(&s_mock_lock);
}

static mock_cfg_t mock_get(void)
{
    mock_cfg_t m;
    pthread_mutex_lock(&s_mock_lock);
    m = s_mock;
    pthread_mutex_unlock(&s_mock_lock);
    return m;
}

/* UTF-16LE -> UTF-8 (BMP only, which is all the tree uses). */
static int utf16le_to_utf8(const uint8_t *in, size_t len, char *out, size_t cap)
{
    size_t i;
    size_t o = 0;
    if (len % 2 != 0) {
        return -1;
    }
    for (i = 0; i < len; i += 2) {
        uint32_t cp = (uint32_t)in[i] | ((uint32_t)in[i + 1] << 8);
        if (cp >= 0xD800u && cp <= 0xDFFFu) {
            return -1;
        }
        if (cp < 0x80u) {
            if (o + 1 >= cap) return -1;
            out[o++] = (char)cp;
        } else if (cp < 0x800u) {
            if (o + 2 >= cap) return -1;
            out[o++] = (char)(0xC0u | (cp >> 6));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            if (o + 3 >= cap) return -1;
            out[o++] = (char)(0xE0u | (cp >> 12));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            out[o++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }
    out[o] = '\0';
    return 0;
}

static void make_fh(uint32_t id, uint8_t fh[DJLINK_NFS_FHSIZE])
{
    memset(fh, 0xA5, DJLINK_NFS_FHSIZE);
    djlink_wr32(fh, id);
}

static const node_t *fh_node(const uint8_t *fh)
{
    size_t i;
    uint32_t id = djlink_rd32(fh);
    for (i = 4; i < DJLINK_NFS_FHSIZE; i++) {
        if (fh[i] != 0xA5) {
            return NULL;
        }
    }
    for (i = 0; i < TREE_LEN; i++) {
        if (s_tree[i].id == id) {
            return &s_tree[i];
        }
    }
    return NULL;
}

static void node_attr(const node_t *n, uint8_t out[DJLINK_NFS_FATTR_LEN])
{
    djlink_nfs_fattr_t a;
    memset(&a, 0, sizeof(a));
    a.type = n->is_dir ? DJLINK_NFS_TYPE_DIR : DJLINK_NFS_TYPE_REG;
    a.mode = n->is_dir ? 040755u : 0100644u;
    a.nlink = 1;
    a.size = n->size;
    a.blocksize = 4096;
    a.fileid = n->id;
    a.mtime_s = 1700000000u + n->id;     /* per-file stamp the client keeps */
    a.mtime_us = 1000u * n->id;
    djlink_nfs_fattr_encode(&a, out);
}

static void mock_send(int fd, const struct sockaddr_in *to, const uint8_t *buf, size_t len)
{
    (void)sendto(fd, buf, len, 0, (const struct sockaddr *)to, sizeof(*to));
}

/* A held READ reply waiting for the next one. */
static uint8_t s_held[DJLINK_NFS_MAXDATA + 256];
static size_t s_held_len;
static struct sockaddr_in s_held_to;

static void mock_handle(int fd, const uint8_t *buf, size_t len, const struct sockaddr_in *from)
{
    static uint8_t res[DJLINK_NFS_MAXDATA + 128];
    static uint8_t out[DJLINK_NFS_MAXDATA + 256];
    djlink_rpc_call_t call;
    size_t rl = 0;
    int n;
    int read_no = 0;
    mock_cfg_t m;

    if (djlink_rpc_call_parse(buf, len, &call) != DJLINK_OK) {
        return;
    }
    pthread_mutex_lock(&s_mock_lock);
    if (call.cred_flavor != DJLINK_RPC_AUTH_UNIX || call.uid != 0) {
        s_mock.bad_auth++;
    }
    m = s_mock;
    pthread_mutex_unlock(&s_mock_lock);

    if (fd == s_pmap_fd) {
        uint32_t prog;
        uint16_t port = 0;
        pthread_mutex_lock(&s_mock_lock);
        s_mock.getport_calls++;
        pthread_mutex_unlock(&s_mock_lock);
        if (m.silent_portmap || call.prog != DJLINK_PMAP_PROG ||
            call.proc != DJLINK_PMAP_PROC_GETPORT || call.args_len < 16) {
            return;
        }
        prog = djlink_rd32(call.args);
        if (prog == DJLINK_MOUNT_PROG && !m.no_mount_prog) {
            port = s_mount_port;
        } else if (prog == DJLINK_NFS_PROG) {
            port = s_nfs_port;
        }
        djlink_wr32(res, port);
        rl = 4;
    } else if (fd == s_mount_fd) {
        size_t plen;
        char path[128];
        pthread_mutex_lock(&s_mock_lock);
        s_mock.mnt_calls++;
        pthread_mutex_unlock(&s_mock_lock);
        if (call.prog != DJLINK_MOUNT_PROG || call.proc != DJLINK_MOUNT_PROC_MNT ||
            call.args_len < 4) {
            return;
        }
        plen = djlink_rd32(call.args);
        if (plen > call.args_len - 4 || utf16le_to_utf8(&call.args[4], plen, path, sizeof(path)) != 0) {
            pthread_mutex_lock(&s_mock_lock);
            s_mock.bad_name++;
            pthread_mutex_unlock(&s_mock_lock);
            djlink_wr32(res, DJLINK_NFSERR_IO);
            rl = 4;
        } else if (strcmp(path, s_tree[0].name) == 0) {
            djlink_wr32(res, 0);
            make_fh(s_tree[0].id, &res[4]);
            rl = 4 + DJLINK_NFS_FHSIZE;
        } else {
            djlink_wr32(res, DJLINK_NFSERR_NOENT);
            rl = 4;
        }
    } else {
        const node_t *dir;
        if (call.prog != DJLINK_NFS_PROG || call.vers != DJLINK_NFS_VERS ||
            call.args_len < DJLINK_NFS_FHSIZE + 4) {
            return;
        }
        dir = fh_node(call.args);
        if (call.proc == DJLINK_NFS_PROC_LOOKUP) {
            size_t nlen = djlink_rd32(&call.args[DJLINK_NFS_FHSIZE]);
            char name[128];
            size_t i;
            const node_t *hit = NULL;
            pthread_mutex_lock(&s_mock_lock);
            s_mock.lookup_calls++;
            pthread_mutex_unlock(&s_mock_lock);
            if (nlen > call.args_len - DJLINK_NFS_FHSIZE - 4 ||
                utf16le_to_utf8(&call.args[DJLINK_NFS_FHSIZE + 4], nlen, name, sizeof(name)) != 0) {
                pthread_mutex_lock(&s_mock_lock);
                s_mock.bad_name++;
                pthread_mutex_unlock(&s_mock_lock);
                name[0] = '\0';
            }
            for (i = 0; dir != NULL && i < TREE_LEN; i++) {
                if (s_tree[i].parent == dir->id && strcmp(s_tree[i].name, name) == 0) {
                    hit = &s_tree[i];
                }
            }
            if (dir == NULL) {
                djlink_wr32(res, DJLINK_NFSERR_STALE);
                rl = 4;
            } else if (!dir->is_dir) {
                djlink_wr32(res, DJLINK_NFSERR_NOTDIR);
                rl = 4;
            } else if (hit == NULL) {
                djlink_wr32(res, DJLINK_NFSERR_NOENT);
                rl = 4;
            } else {
                djlink_wr32(res, DJLINK_NFS_OK);
                make_fh(hit->id, &res[4]);
                node_attr(hit, &res[4 + DJLINK_NFS_FHSIZE]);
                rl = 4 + DJLINK_NFS_FHSIZE + DJLINK_NFS_FATTR_LEN;
            }
        } else if (call.proc == DJLINK_NFS_PROC_READ && call.args_len >= DJLINK_NFS_FHSIZE + 12) {
            uint32_t off = djlink_rd32(&call.args[DJLINK_NFS_FHSIZE]);
            uint32_t count = djlink_rd32(&call.args[DJLINK_NFS_FHSIZE + 4]);
            uint32_t got = 0;
            uint32_t i;
            pthread_mutex_lock(&s_mock_lock);
            read_no = ++s_mock.read_calls;
            if (count > s_mock.max_count) {
                s_mock.max_count = count;
            }
            m = s_mock;
            pthread_mutex_unlock(&s_mock_lock);
            if (read_no == m.drop_request) {
                return;
            }
            if (dir == NULL || dir->is_dir) {
                djlink_wr32(res, dir == NULL ? DJLINK_NFSERR_STALE : DJLINK_NFSERR_ISDIR);
                rl = 4;
            } else {
                if (count > DJLINK_NFS_MAXDATA) {
                    count = DJLINK_NFS_MAXDATA;
                }
                if (off < dir->size) {
                    got = dir->size - off < count ? dir->size - off : count;
                }
                if (read_no == m.short_reply && got > 1) {
                    got /= 2;
                }
                djlink_wr32(res, DJLINK_NFS_OK);
                node_attr(dir, &res[4]);
                if(read_no==m.changed_read)djlink_wr32(res+4+13*4,1800000000);
                djlink_wr32(&res[4 + DJLINK_NFS_FATTR_LEN], got);
                for (i = 0; i < got; i++) {
                    res[8 + DJLINK_NFS_FATTR_LEN + i] = file_byte(dir->id, off + i) ^ (m.media_variant?0x5a:0);
                }
                rl = 8 + DJLINK_NFS_FATTR_LEN + ((got + 3u) & ~3u);
                memset(&res[8 + DJLINK_NFS_FATTR_LEN + got], 0, rl - 8 - DJLINK_NFS_FATTR_LEN - got);
            }
        } else {
            n = djlink_rpc_reply_build(out, sizeof(out), call.xid, DJLINK_RPC_PROC_UNAVAIL, NULL, 0);
            mock_send(fd, from, out, (size_t)n);
            return;
        }
    }

    n = djlink_rpc_reply_build(out, sizeof(out), call.xid, DJLINK_RPC_SUCCESS, res, rl);
    if (n <= 0) {
        return;
    }
    if (read_no != 0 && read_no == m.drop_reply) {
        return;
    }
    if (read_no != 0 && read_no == m.hold_reply) {
        memcpy(s_held, out, (size_t)n);
        s_held_len = (size_t)n;
        s_held_to = *from;
        return;
    }
    mock_send(fd, from, out, (size_t)n);
    if (read_no != 0 && read_no == m.dup_reply) {
        mock_send(fd, from, out, (size_t)n);
    }
    if (read_no != 0 && s_held_len > 0) {
        mock_send(fd, &s_held_to, s_held, s_held_len);
        s_held_len = 0;
    }
}

static void *mock_thread(void *arg)
{
    static uint8_t buf[2048];
    (void)arg;
    for (;;) {
        struct pollfd p[3] = {
            { s_pmap_fd, POLLIN, 0 }, { s_mount_fd, POLLIN, 0 }, { s_nfs_fd, POLLIN, 0 },
        };
        int i;
        pthread_mutex_lock(&s_mock_lock);
        i = s_mock_stop;
        pthread_mutex_unlock(&s_mock_lock);
        if (i) {
            return NULL;
        }
        if (poll(p, 3, 20) <= 0) {
            continue;
        }
        for (i = 0; i < 3; i++) {
            struct sockaddr_in from;
            socklen_t flen = sizeof(from);
            ssize_t n;
            if (!(p[i].revents & POLLIN)) {
                continue;
            }
            n = recvfrom(p[i].fd, buf, sizeof(buf), 0, (struct sockaddr *)&from, &flen);
            if (n > 0) {
                mock_handle(p[i].fd, buf, (size_t)n, &from);
            }
        }
    }
}

/* --- Client harness ------------------------------------------------------- */

typedef struct {
    int fd;
    uint8_t *data;
    uint32_t cap;
    uint32_t size;       /* from open() */
    uint32_t open_mtime_s;  /* client attr at open() (cache validation) */
    uint32_t open_mtime_us;
    uint32_t written;
    int opens;
    int order_errors;
    int progress_calls;
    uint32_t last_done;
    uint32_t cancel_at;  /* cancel from the loop once written >= this */
    dj_link_cache_t *cache;
    dj_link_cache_txn_t txn;
} sink_t;

static int h_send(void *ctx, uint32_t ip, uint16_t port, const uint8_t *buf, size_t len)
{
    sink_t *s = (sink_t *)ctx;
    struct sockaddr_in to;
    memset(&to, 0, sizeof(to));
    to.sin_family = AF_INET;
    to.sin_addr.s_addr = htonl(ip);
    to.sin_port = htons(port);
    return sendto(s->fd, buf, len, 0, (struct sockaddr *)&to, sizeof(to)) == (ssize_t)len ? 0 : -1;
}

static djlink_nfs_t s_client;

static int h_open(void *ctx, uint32_t size)
{
    sink_t *s = (sink_t *)ctx;
    s->opens++;
    s->size = size;
    s->open_mtime_s = s_client.attr.mtime_s;
    s->open_mtime_us = s_client.attr.mtime_us;
    if(s->cache && !dj_link_cache_begin(s->cache,&s->txn,size,DJ_LINK_CACHE_BUDGET))return -1;
    return size > s->cap ? -1 : 0;
}

static int h_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len)
{
    sink_t *s = (sink_t *)ctx;
    if (offset != s->written || (size_t)offset + len > s->cap) {
        s->order_errors++;
        return -1;
    }
    memcpy(&s->data[offset], data, len);
    if(s->cache && !dj_link_cache_write(&s->txn,data,len))return -1;
    s->written += (uint32_t)len;
    return 0;
}

static void h_progress(void *ctx, uint32_t done, uint32_t total)
{
    sink_t *s = (sink_t *)ctx;
    s->progress_calls++;
    if (done < s->last_done || done > total) {
        s->order_errors++;
    }
    s->last_done = done;
}

static uint8_t s_window_buf[DJLINK_NFS_WINDOW_MAX * DJLINK_NFS_MAXDATA];

static void base_cfg(djlink_nfs_fetch_cfg_t *cfg, const char *path)
{
    static uint32_t xid = 0x1000;
    memset(cfg, 0, sizeof(*cfg));
    cfg->host_ip = LOOPBACK;
    cfg->export_path = DJLINK_NFS_EXPORT_USB;
    cfg->path = path;
    cfg->charset = DJLINK_NFS_NAMES_UTF16LE;
    cfg->portmap_port = s_pmap_port;
    cfg->retransmit_ms = 100; /* loopback answers in well under 1 ms */
    cfg->xid_seed = xid;
    xid += 0x10000;
}

/* Run one fetch to completion. */
static djlink_nfs_state_t run_fetch(const djlink_nfs_fetch_cfg_t *cfg, sink_t *sink)
{
    static uint8_t rx[DJLINK_NFS_MAXDATA + 512];
    djlink_nfs_io_t io = { h_send, h_open, h_write, h_progress, sink };
    uint16_t port;
    uint32_t start = now_ms();

    sink->fd = udp_socket(&port);
    sink->written = 0;
    sink->opens = 0;
    sink->order_errors = 0;
    sink->progress_calls = 0;
    sink->last_done = 0;
    djlink_nfs_fetch(&s_client, cfg, &io, now_ms());
    while (djlink_nfs_state(&s_client) == DJLINK_NFS_BUSY && now_ms() - start < 20000u) {
        struct pollfd p = { sink->fd, POLLIN, 0 };
        if (poll(&p, 1, 5) > 0) {
            ssize_t n = recv(sink->fd, rx, sizeof(rx), 0);
            if (n > 0) {
                djlink_nfs_on_datagram(&s_client, rx, (size_t)n, now_ms());
            }
        }
        djlink_nfs_poll(&s_client, now_ms());
        if (sink->cancel_at != 0 && sink->written >= sink->cancel_at) {
            djlink_nfs_cancel(&s_client);
        }
    }
    close(sink->fd);
    return djlink_nfs_state(&s_client);
}

static int content_ok(const sink_t *s, uint32_t id, uint32_t size)
{
    uint32_t i;
    if (s->written != size) {
        return 0;
    }
    for (i = 0; i < size; i++) {
        if (s->data[i] != file_byte(id, i)) {
            printf("  mismatch at %u\n", i);
            return 0;
        }
    }
    return 1;
}

/* --- Tests ---------------------------------------------------------------- */

static void test_codec(void)
{
    uint8_t buf[700];
    uint8_t fh[DJLINK_NFS_FHSIZE];
    uint8_t fh2[DJLINK_NFS_FHSIZE];
    uint8_t res[256];
    djlink_rpc_call_t call;
    djlink_rpc_reply_t rep;
    djlink_nfs_fattr_t a, b;
    const uint8_t *data;
    size_t dlen;
    uint32_t st;
    uint16_t port;
    int n;
    static const uint8_t cafe[] = { 'C', 0, 'a', 0, 'f', 0, 0xE9, 0, '/', 0 };
    static const uint8_t clef[] = { 0x34, 0xD8, 0x1E, 0xDD }; /* U+1D11E */

    /* Names. */
    n = djlink_nfs_name_encode("Caf\xC3\xA9/", 6, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf));
    CHECK_EQ(n, (int)sizeof(cafe));
    CHECK(memcmp(buf, cafe, sizeof(cafe)) == 0);
    n = djlink_nfs_name_encode("\xE2\x82\xAC", 3, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf));
    CHECK_EQ(n, 2); /* U+20AC */
    CHECK_EQ(buf[0], 0xAC);
    CHECK_EQ(buf[1], 0x20);
    n = djlink_nfs_name_encode("\xF0\x9D\x84\x9E", 4, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf));
    CHECK_EQ(n, 4);
    CHECK(memcmp(buf, clef, 4) == 0);
    CHECK_EQ(djlink_nfs_name_encode("\xC3", 1, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf)),
             DJLINK_ERR_TYPE);
    CHECK_EQ(djlink_nfs_name_encode("\xC0\x80", 2, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf)),
             DJLINK_ERR_TYPE); /* overlong NUL */
    CHECK_EQ(djlink_nfs_name_encode("\xED\xA0\x80", 3, DJLINK_NFS_NAMES_UTF16LE, buf, sizeof(buf)),
             DJLINK_ERR_TYPE); /* encoded surrogate */
    CHECK_EQ(djlink_nfs_name_encode("abc", 3, DJLINK_NFS_NAMES_UTF16LE, buf, 5), DJLINK_ERR_BOUNDS);
    CHECK_EQ(djlink_nfs_name_encode("abc", 3, DJLINK_NFS_NAMES_BYTES, buf, 3), 3);
    CHECK_EQ(djlink_nfs_name_encode("abc", 3, DJLINK_NFS_NAMES_BYTES, buf, 2), DJLINK_ERR_BOUNDS);

    /* GETPORT call: 60-byte header + 16 bytes of args, parsed back. */
    n = djlink_pmap_getport_build(buf, sizeof(buf), 0x11223344u, DJLINK_MOUNT_PROG, DJLINK_MOUNT_VERS);
    CHECK_EQ(n, 76);
    CHECK_EQ(djlink_rd32(&buf[4]), DJLINK_RPC_CALL);
    CHECK_EQ(djlink_rd32(&buf[8]), 2);
    CHECK_EQ(djlink_rd32(&buf[24]), DJLINK_RPC_AUTH_UNIX);
    CHECK_EQ(djlink_rd32(&buf[28]), 20); /* AUTH_UNIX body */
    CHECK_EQ(djlink_rd32(&buf[68]), DJLINK_IPPROTO_UDP);
    CHECK_EQ(djlink_rpc_call_parse(buf, (size_t)n, &call), DJLINK_OK);
    CHECK_EQ(call.xid, 0x11223344u);
    CHECK_EQ(call.prog, DJLINK_PMAP_PROG);
    CHECK_EQ(call.proc, DJLINK_PMAP_PROC_GETPORT);
    CHECK_EQ(call.uid, 0);
    CHECK_EQ(call.args_len, 16);
    CHECK_EQ(djlink_rd32(call.args), DJLINK_MOUNT_PROG);
    CHECK_EQ(djlink_pmap_getport_build(buf, 75, 1, 2, 3), DJLINK_ERR_BOUNDS);

    /* MNT pads the dirpath to 4 bytes. */
    n = djlink_mount_mnt_build(buf, sizeof(buf), 7, cafe, 10);
    CHECK_EQ(n, 60 + 4 + 12);
    CHECK_EQ(djlink_rd32(&buf[60]), 10);
    CHECK_EQ(buf[74], 0);
    CHECK_EQ(buf[75], 0);

    /* LOOKUP of the longest name still fits DJLINK_NFS_TX_MAX. */
    memset(fh, 0x5A, sizeof(fh));
    uint8_t short_name[DJLINK_NFS_NAME_MAX - 400] = {0};
    /* Donor fixture used buf+600 for 110 bytes with only 100 bytes remaining.
     * Supply the complete declared input; keep the capacity/boundary checks. */
    n = djlink_nfs_lookup_build(buf, DJLINK_NFS_TX_MAX, 8, fh, short_name, sizeof(short_name));
    CHECK(n > 0);
    {
        static uint8_t big[DJLINK_NFS_TX_MAX];
        static uint8_t name[DJLINK_NFS_NAME_MAX + 1];
        CHECK(djlink_nfs_lookup_build(big, sizeof(big), 8, fh, name, DJLINK_NFS_NAME_MAX) > 0);
        CHECK_EQ(djlink_nfs_lookup_build(big, sizeof(big), 8, fh, name, DJLINK_NFS_NAME_MAX + 1),
                 DJLINK_ERR_BOUNDS);
    }

    /* READ: fh, offset, count, totalcount. */
    n = djlink_nfs_read_build(buf, sizeof(buf), 9, fh, 4096, 1024);
    CHECK_EQ(n, 60 + 32 + 12);
    CHECK_EQ(djlink_rd32(&buf[20]), DJLINK_NFS_PROC_READ);
    CHECK_EQ(djlink_rd32(&buf[92]), 4096);
    CHECK_EQ(djlink_rd32(&buf[96]), 1024);

    /* Replies. */
    djlink_wr32(res, 2049);
    n = djlink_rpc_reply_build(buf, sizeof(buf), 42, DJLINK_RPC_SUCCESS, res, 4);
    CHECK_EQ(n, 28);
    CHECK_EQ(djlink_rpc_reply_parse(buf, (size_t)n, &rep), DJLINK_OK);
    CHECK_EQ(rep.xid, 42);
    CHECK_EQ(rep.reply_stat, DJLINK_RPC_MSG_ACCEPTED);
    CHECK_EQ(rep.accept_stat, DJLINK_RPC_SUCCESS);
    CHECK_EQ(djlink_pmap_getport_parse(rep.res, rep.res_len, &port), DJLINK_OK);
    CHECK_EQ(port, 2049);
    CHECK_EQ(djlink_rpc_reply_parse(buf, 20, &rep), DJLINK_ERR_TRUNCATED);
    CHECK_EQ(djlink_rpc_call_parse(buf, (size_t)n, &call), DJLINK_ERR_TYPE);

    /* Denied reply (AUTH_ERROR). */
    djlink_wr32(&buf[0], 43);
    djlink_wr32(&buf[4], DJLINK_RPC_REPLY);
    djlink_wr32(&buf[8], DJLINK_RPC_MSG_DENIED);
    djlink_wr32(&buf[12], 1);
    CHECK_EQ(djlink_rpc_reply_parse(buf, 16, &rep), DJLINK_OK);
    CHECK_EQ(rep.reply_stat, DJLINK_RPC_MSG_DENIED);
    CHECK_EQ(rep.accept_stat, 1);

    /* Accepted reply carrying a non-empty verifier. */
    djlink_wr32(&buf[8], DJLINK_RPC_MSG_ACCEPTED);
    djlink_wr32(&buf[12], 1);
    djlink_wr32(&buf[16], 3);
    memset(&buf[20], 0xEE, 4);
    djlink_wr32(&buf[24], DJLINK_RPC_SUCCESS);
    djlink_wr32(&buf[28], 99);
    CHECK_EQ(djlink_rpc_reply_parse(buf, 32, &rep), DJLINK_OK);
    CHECK_EQ(rep.res_len, 4);
    CHECK_EQ(djlink_rd32(rep.res), 99);

    /* MNT / LOOKUP / READ results. */
    djlink_wr32(res, 0);
    memset(&res[4], 0x77, DJLINK_NFS_FHSIZE);
    CHECK_EQ(djlink_mount_mnt_parse(res, 36, &st, fh2), DJLINK_OK);
    CHECK_EQ(st, 0);
    CHECK_EQ(fh2[31], 0x77);
    CHECK_EQ(djlink_mount_mnt_parse(res, 35, &st, fh2), DJLINK_ERR_TRUNCATED);
    djlink_wr32(res, 13);
    CHECK_EQ(djlink_mount_mnt_parse(res, 4, &st, fh2), DJLINK_OK);
    CHECK_EQ(st, 13);

    memset(&a, 0, sizeof(a));
    a.type = DJLINK_NFS_TYPE_REG;
    a.size = 123456;
    a.fileid = 77;
    a.ctime_us = 0xCAFEu;
    djlink_wr32(res, 0);
    memset(&res[4], 0x11, DJLINK_NFS_FHSIZE);
    djlink_nfs_fattr_encode(&a, &res[36]);
    CHECK_EQ(djlink_nfs_lookup_parse(res, 104, &st, fh2, &b), DJLINK_OK);
    CHECK_EQ(b.size, 123456);
    CHECK_EQ(b.fileid, 77);
    CHECK_EQ(b.ctime_us, 0xCAFEu);
    CHECK_EQ(fh2[0], 0x11);
    CHECK_EQ(djlink_nfs_lookup_parse(res, 103, &st, fh2, &b), DJLINK_ERR_TRUNCATED);

    djlink_wr32(res, 0);
    djlink_nfs_fattr_encode(&a, &res[4]);
    djlink_wr32(&res[72], 5);
    memcpy(&res[76], "hello\0\0\0", 8);
    CHECK_EQ(djlink_nfs_read_parse(res, 84, &st, &b, &data, &dlen), DJLINK_OK);
    CHECK_EQ(dlen, 5);
    CHECK(memcmp(data, "hello", 5) == 0);
    CHECK_EQ(djlink_nfs_read_parse(res, 81, &st, &b, &data, &dlen), DJLINK_OK); /* unpadded tail */
    CHECK_EQ(djlink_nfs_read_parse(res, 80, &st, &b, &data, &dlen), DJLINK_ERR_TRUNCATED);
    djlink_wr32(&res[72], DJLINK_NFS_MAXDATA + 1);
    CHECK_EQ(djlink_nfs_read_parse(res, 84, &st, &b, &data, &dlen), DJLINK_ERR_TRUNCATED);
}

static void test_bad_config(void)
{
    djlink_nfs_fetch_cfg_t cfg;
    sink_t sink;
    djlink_nfs_io_t io = { h_send, NULL, h_write, NULL, &sink };
    memset(&sink, 0, sizeof(sink));
    base_cfg(&cfg, "///");
    CHECK_EQ(djlink_nfs_fetch(&s_client, &cfg, &io, 0), DJLINK_NFS_E_ARG);
    CHECK_EQ(djlink_nfs_state(&s_client), DJLINK_NFS_FAILED);
    base_cfg(&cfg, "bad\xFFname");
    CHECK_EQ(djlink_nfs_fetch(&s_client, &cfg, &io, 0), DJLINK_NFS_E_ARG);
    base_cfg(&cfg, "a");
    cfg.read_size = 100;
    CHECK_EQ(djlink_nfs_fetch(&s_client, &cfg, &io, 0), DJLINK_NFS_E_ARG);
    base_cfg(&cfg, "a");
    io.write = NULL;
    CHECK_EQ(djlink_nfs_fetch(&s_client, &cfg, &io, 0), DJLINK_NFS_E_ARG);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "BAD FETCH CONFIG") == 0);
}

static void test_small_fetch(sink_t *sink)
{
    djlink_nfs_fetch_cfg_t cfg;
    mock_cfg_t m;

    /* Three-level LOOKUP, one READ window. */
    mock_reset();
    base_cfg(&cfg, "PIONEER/rekordbox/export.pdb");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    m = mock_get();
    CHECK_EQ(m.getport_calls, 2);
    CHECK_EQ(m.mnt_calls, 1);
    CHECK_EQ(m.lookup_calls, 3);
    CHECK_EQ(m.read_calls, 12); /* 12288 / 1024 */
    CHECK_EQ(m.max_count, DJLINK_NFS_READ_DEFAULT);
    CHECK_EQ(m.bad_auth, 0);
    CHECK_EQ(m.bad_name, 0);
    CHECK_EQ(s_client.mount_port, s_mount_port);
    CHECK_EQ(s_client.nfs_port, s_nfs_port);
    CHECK_EQ(s_client.window, 1);
    CHECK_EQ(sink->opens, 1);
    CHECK_EQ(sink->size, 12288);
    /* The file's LOOKUP attributes are there when the sink opens. */
    CHECK_EQ(sink->open_mtime_s, 1700000004u);
    CHECK_EQ(sink->open_mtime_us, 4000u);
    CHECK_EQ(s_client.attr.size, 12288);
    CHECK_EQ(sink->order_errors, 0);
    CHECK_EQ(sink->progress_calls, 12);
    CHECK(content_ok(sink, 4, 12288));

    /* Leading and doubled slashes are ignored; tail shorter than a read. */
    mock_reset();
    base_cfg(&cfg, "//small.bin");
    cfg.window = 4;
    cfg.window_buf = s_window_buf;
    cfg.window_buf_len = sizeof(s_window_buf);
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    CHECK_EQ(mock_get().read_calls, 3);
    CHECK(content_ok(sink, 10, 3000));

    /* Empty file: DONE right after LOOKUP. */
    mock_reset();
    base_cfg(&cfg, "/empty.bin");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    CHECK_EQ(mock_get().read_calls, 0);
    CHECK_EQ(sink->opens, 1);
    CHECK_EQ(sink->written, 0);
}

static void test_big_fetch_with_losses(sink_t *sink)
{
    djlink_nfs_fetch_cfg_t cfg;
    mock_cfg_t m;
    uint32_t reads = (BIG_SIZE + 1023u) / 1024u;
    uint32_t t0;

    /* > 1 MiB through a 4-deep window while the mock loses a request, loses
     * a reply, duplicates a reply and delivers one out of order. */
    mock_reset();
    pthread_mutex_lock(&s_mock_lock);
    s_mock.drop_request = 5;
    s_mock.drop_reply = 40;
    s_mock.dup_reply = 77;
    s_mock.hold_reply = 300;
    pthread_mutex_unlock(&s_mock_lock);
    base_cfg(&cfg, "/Contents/Caf\xC3\xA9 Artist/Album/Track.mp3");
    cfg.window = 4;
    cfg.window_buf = s_window_buf;
    cfg.window_buf_len = 4 * 1024;
    cfg.max_size = 4u * 1024u * 1024u;
    t0 = now_ms();
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    m = mock_get();
    printf("  1.5 MiB / 1024: %u ms, %u READs, %u retransmits, %u strays\n",
           now_ms() - t0, m.read_calls, s_client.retransmits, s_client.stray_replies);
    CHECK_EQ(s_client.window, 4);
    CHECK_EQ(m.lookup_calls, 4);
    CHECK_EQ(m.bad_name, 0);
    CHECK_EQ(s_client.retransmits, 2);
    CHECK_EQ((uint32_t)m.read_calls, reads + 2);
    CHECK(s_client.stray_replies >= 1); /* the duplicate */
    CHECK_EQ(sink->order_errors, 0);
    CHECK(content_ok(sink, 8, BIG_SIZE));

    /* Short READ before EOF: the window restarts from what was written. */
    mock_reset();
    pthread_mutex_lock(&s_mock_lock);
    s_mock.short_reply = 10;
    pthread_mutex_unlock(&s_mock_lock);
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    CHECK_EQ(sink->order_errors, 0);
    CHECK(content_ok(sink, 8, BIG_SIZE));

    /* 8 KiB reads with a full 8-deep window (loopback reassembles). */
    mock_reset();
    pthread_mutex_lock(&s_mock_lock);
    s_mock.drop_reply = 3;
    pthread_mutex_unlock(&s_mock_lock);
    cfg.read_size = DJLINK_NFS_MAXDATA;
    cfg.window = 8;
    cfg.window_buf_len = sizeof(s_window_buf);
    t0 = now_ms();
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    m = mock_get();
    printf("  1.5 MiB / 8192: %u ms, %u READs\n", now_ms() - t0, m.read_calls);
    CHECK_EQ(s_client.window, 8);
    CHECK_EQ(m.max_count, DJLINK_NFS_MAXDATA);
    CHECK_EQ((uint32_t)m.read_calls, (BIG_SIZE + DJLINK_NFS_MAXDATA - 1u) / DJLINK_NFS_MAXDATA + 1u);
    CHECK(content_ok(sink, 8, BIG_SIZE));

    /* The window is capped by the reorder buffer. */
    mock_reset();
    base_cfg(&cfg, "/small.bin");
    cfg.window = 8;
    cfg.window_buf = s_window_buf;
    cfg.window_buf_len = 3 * 1024 + 100;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_DONE);
    CHECK_EQ(s_client.window, 3);
}

static void test_errors(sink_t *sink)
{
    djlink_nfs_fetch_cfg_t cfg;
    uint32_t cap = sink->cap;

    /* Unknown export. */
    mock_reset();
    base_cfg(&cfg, "PIONEER/rekordbox/export.pdb");
    cfg.export_path = "/Z/";
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_MOUNT);
    CHECK_EQ(s_client.status, DJLINK_NFSERR_NOENT);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "EXPORT REFUSED (2)") == 0);
    CHECK_EQ(mock_get().lookup_calls, 0);

    /* Missing element in the middle of the path. */
    mock_reset();
    base_cfg(&cfg, "PIONEER/rekordbax/export.pdb");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_LOOKUP);
    CHECK_EQ(s_client.status, DJLINK_NFSERR_NOENT);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "NOT FOUND: rekordbax") == 0);
    CHECK_EQ(mock_get().lookup_calls, 2);

    /* A file used as a directory, and a directory as the target. */
    mock_reset();
    base_cfg(&cfg, "small.bin/x");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_LOOKUP);
    CHECK_EQ(mock_get().lookup_calls, 1); /* caught from the attributes */
    base_cfg(&cfg, "PIONEER/rekordbox");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_NOT_FILE);

    /* Size limit, and a sink that refuses the file. */
    base_cfg(&cfg, "small.bin");
    cfg.max_size = 2999;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_TOO_BIG);
    CHECK_EQ(sink->opens, 0);
    base_cfg(&cfg, "small.bin");
    sink->cap = 100;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_SINK);
    sink->cap = cap;

    /* Portmapper without MOUNT. */
    mock_reset();
    pthread_mutex_lock(&s_mock_lock);
    s_mock.no_mount_prog = 1;
    pthread_mutex_unlock(&s_mock_lock);
    base_cfg(&cfg, "small.bin");
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_NO_SERVICE);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "NO MOUNT SERVICE") == 0);

    /* Silent portmapper: 1 + 2 retransmits with backoff, then TIMEOUT. */
    mock_reset();
    pthread_mutex_lock(&s_mock_lock);
    s_mock.silent_portmap = 1;
    pthread_mutex_unlock(&s_mock_lock);
    base_cfg(&cfg, "small.bin");
    cfg.retransmit_ms = 10;
    cfg.retries = 2;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_TIMEOUT);
    CHECK_EQ(s_client.retransmits, 2);
    CHECK_EQ(mock_get().getport_calls, 3);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "TIMEOUT PORTMAP") == 0);

    /* Nothing listening on the port: sends succeed, replies never come. */
    mock_reset();
    base_cfg(&cfg, "small.bin");
    cfg.portmap_port = 9; /* discard */
    cfg.retransmit_ms = 5;
    cfg.retries = 1;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_FAILED);
    CHECK_EQ(s_client.error, DJLINK_NFS_E_TIMEOUT);
}

static void test_cancel(sink_t *sink)
{
    djlink_nfs_fetch_cfg_t cfg;
    int reads;

    mock_reset();
    base_cfg(&cfg, "Contents/Caf\xC3\xA9 Artist/Album/Track.mp3");
    sink->cancel_at = 64u * 1024u;
    CHECK_EQ(run_fetch(&cfg, sink), DJLINK_NFS_CANCELLED);
    sink->cancel_at = 0;
    CHECK(sink->written >= 64u * 1024u && sink->written < BIG_SIZE);
    CHECK(strcmp(djlink_nfs_error_text(&s_client), "CANCELLED") == 0);
    /* No more traffic once the READs already in flight have landed. */
    usleep(50000);
    reads = mock_get().read_calls;
    djlink_nfs_poll(&s_client, now_ms() + 100000u);
    usleep(50000);
    CHECK_EQ(mock_get().read_calls, reads);
    CHECK_EQ(djlink_nfs_state(&s_client), DJLINK_NFS_CANCELLED);
}
static void test_transactional_cache(sink_t *sink)
{
    char root[]="/tmp/pajoniiir-cache-XXXXXX";CHECK(mkdtemp(root)!=NULL);
    dj_link_cache_t cache;dj_link_cache_io_t io={0};
    CHECK(dj_link_cache_init(&cache,root,&io,NULL));sink->cache=&cache;
    djlink_nfs_fetch_cfg_t cfg;char a_path[272],b_path[272];
    uint8_t export_digest[32]={1};media_persistent_id_t a,b;
    mock_reset();base_cfg(&cfg,"small.bin");
    CHECK_EQ(run_fetch(&cfg,sink),DJLINK_NFS_DONE);
    CHECK(dj_link_cache_seal(&sink->txn));
    CHECK(dj_link_content_identity(export_digest,sink->txn.digest,"small.bin",sink->size,sink->open_mtime_s,&a));
    CHECK(dj_link_cache_commit(&sink->txn,&a,"MP3",a_path));
    cache.pins[0]=a;
    /* Same endpoint, track path, size and timestamps. New media bytes only. */
    mock_reset();pthread_mutex_lock(&s_mock_lock);s_mock.media_variant=1;pthread_mutex_unlock(&s_mock_lock);
    CHECK_EQ(run_fetch(&cfg,sink),DJLINK_NFS_DONE);CHECK(dj_link_cache_seal(&sink->txn));
    CHECK(dj_link_content_identity(export_digest,sink->txn.digest,"small.bin",sink->size,sink->open_mtime_s,&b));
    CHECK(!media_persistent_id_equal(&a,&b));
    CHECK(!dj_link_cache_hit(&cache,&b,"MP3",NULL));
    CHECK(dj_link_cache_commit(&sink->txn,&b,"MP3",b_path));
    CHECK(strcmp(a_path,b_path));
    CHECK(dj_link_cache_hit(&cache,&a,"MP3",NULL));CHECK(dj_link_cache_hit(&cache,&b,"MP3",NULL));
    /* Replaced file during READ cannot publish a completion. */
    mock_reset();pthread_mutex_lock(&s_mock_lock);s_mock.changed_read=2;pthread_mutex_unlock(&s_mock_lock);
    CHECK_EQ(run_fetch(&cfg,sink),DJLINK_NFS_FAILED);
    CHECK(!strcmp(djlink_nfs_error_text(&s_client),"SOURCE FILE CHANGED"));
    CHECK(!dj_link_cache_seal(&sink->txn));dj_link_cache_abort(&sink->txn);
    CHECK(dj_link_cache_hit(&cache,&a,"MP3",NULL));
    mock_reset();sink->cancel_at=1024;CHECK_EQ(run_fetch(&cfg,sink),DJLINK_NFS_CANCELLED);
    dj_link_cache_abort(&sink->txn);sink->cancel_at=0;sink->cache=NULL;
    CHECK(dj_link_cache_hit(&cache,&b,"MP3",NULL)); /* network gone, local intact */
    char meta[300];snprintf(meta,sizeof(meta),"%s.manifest",a_path);unlink(meta);unlink(a_path);
    snprintf(meta,sizeof(meta),"%s.manifest",b_path);unlink(meta);unlink(b_path);rmdir(root);
}

int main(void)
{
    pthread_t th;
    sink_t sink;

    test_codec();

    s_pmap_fd = udp_socket(&s_pmap_port);
    s_mount_fd = udp_socket(&s_mount_port);
    s_nfs_fd = udp_socket(&s_nfs_port);
    pthread_create(&th, NULL, mock_thread, NULL);

    memset(&sink, 0, sizeof(sink));
    sink.cap = 4u * 1024u * 1024u;
    sink.data = malloc(sink.cap);
    if (sink.data == NULL) {
        return 2;
    }

    test_bad_config();
    test_small_fetch(&sink);
    test_big_fetch_with_losses(&sink);
    test_errors(&sink);
    test_cancel(&sink);
    test_transactional_cache(&sink);

    pthread_mutex_lock(&s_mock_lock);
    s_mock_stop = 1;
    pthread_mutex_unlock(&s_mock_lock);
    pthread_join(th, NULL);
    close(s_pmap_fd);
    close(s_mount_fd);
    close(s_nfs_fd);
    free(sink.data);

    if (failures == 0) {
        printf("all djlink nfs tests passed\n");
        return 0;
    }
    printf("%d failure(s)\n", failures);
    return 1;
}
