#include "djlink/nfs.h"

#include <stdio.h>
#include <string.h>

/* --- XDR ----------------------------------------------------------------- */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t pos;
    bool err;
} xdr_w_t;

typedef struct {
    const uint8_t *buf;
    size_t len;
    size_t pos;
    bool err;
} xdr_r_t;

static size_t xdr_pad(size_t n)
{
    return (n + 3u) & ~(size_t)3u;
}

static void w_u32(xdr_w_t *w, uint32_t v)
{
    if (w->err || w->cap - w->pos < 4) {
        w->err = true;
        return;
    }
    djlink_wr32(&w->buf[w->pos], v);
    w->pos += 4;
}

/* Fixed-length opaque: data plus zero padding to 4 bytes. */
static void w_fixed(xdr_w_t *w, const uint8_t *data, size_t len)
{
    size_t padded = xdr_pad(len);
    if (w->err || padded < len || w->cap - w->pos < padded) {
        w->err = true;
        return;
    }
    if (len > 0) {
        memcpy(&w->buf[w->pos], data, len);
    }
    memset(&w->buf[w->pos + len], 0, padded - len);
    w->pos += padded;
}

/* Variable-length opaque / string: length word then padded data. */
static void w_opaque(xdr_w_t *w, const uint8_t *data, size_t len)
{
    w_u32(w, (uint32_t)len);
    w_fixed(w, data, len);
}

static uint32_t r_u32(xdr_r_t *r)
{
    uint32_t v;
    if (r->err || r->len - r->pos < 4) {
        r->err = true;
        return 0;
    }
    v = djlink_rd32(&r->buf[r->pos]);
    r->pos += 4;
    return v;
}

/* Fixed-length opaque; returns a pointer into the buffer. */
static const uint8_t *r_fixed(xdr_r_t *r, size_t len)
{
    const uint8_t *p;
    size_t padded = xdr_pad(len);
    if (r->err || padded < len || r->len - r->pos < len) {
        r->err = true;
        return NULL;
    }
    p = &r->buf[r->pos];
    /* Tolerate a missing final pad at the very end of a datagram. */
    r->pos += (r->len - r->pos < padded) ? len : padded;
    return p;
}

/* Variable-length opaque with an upper bound. */
static const uint8_t *r_opaque(xdr_r_t *r, size_t max, size_t *len)
{
    uint32_t n = r_u32(r);
    if (r->err || n > max) {
        r->err = true;
        *len = 0;
        return NULL;
    }
    *len = n;
    return r_fixed(r, n);
}

/* --- Names --------------------------------------------------------------- */

/* Decode one UTF-8 scalar; returns bytes consumed or 0 when invalid. */
static size_t utf8_decode(const uint8_t *s, size_t len, uint32_t *cp)
{
    uint32_t c;
    size_t n;
    size_t i;
    if (len == 0) {
        return 0;
    }
    c = s[0];
    if (c < 0x80u) {
        *cp = c;
        return 1;
    } else if ((c & 0xE0u) == 0xC0u) {
        n = 2;
        c &= 0x1Fu;
    } else if ((c & 0xF0u) == 0xE0u) {
        n = 3;
        c &= 0x0Fu;
    } else if ((c & 0xF8u) == 0xF0u) {
        n = 4;
        c &= 0x07u;
    } else {
        return 0;
    }
    if (len < n) {
        return 0;
    }
    for (i = 1; i < n; i++) {
        if ((s[i] & 0xC0u) != 0x80u) {
            return 0;
        }
        c = (c << 6) | (s[i] & 0x3Fu);
    }
    /* Overlong forms, surrogates and values past U+10FFFF are invalid. */
    if ((n == 2 && c < 0x80u) || (n == 3 && c < 0x800u) || (n == 4 && c < 0x10000u) ||
        (c >= 0xD800u && c <= 0xDFFFu) || c > 0x10FFFFu) {
        return 0;
    }
    *cp = c;
    return n;
}

int djlink_nfs_name_encode(const char *utf8, size_t len, djlink_nfs_charset_t cs,
                           uint8_t *out, size_t cap)
{
    const uint8_t *s = (const uint8_t *)utf8;
    size_t pos = 0;
    size_t i = 0;
    if (utf8 == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cs == DJLINK_NFS_NAMES_BYTES) {
        if (len > cap || len > 0x7FFFFFFFu) {
            return DJLINK_ERR_BOUNDS;
        }
        memcpy(out, s, len);
        return (int)len;
    }
    if (cs != DJLINK_NFS_NAMES_UTF16LE) {
        return DJLINK_ERR_TYPE;
    }
    while (i < len) {
        uint32_t cp = 0;
        size_t n = utf8_decode(&s[i], len - i, &cp);
        if (n == 0) {
            return DJLINK_ERR_TYPE;
        }
        i += n;
        if (cp >= 0x10000u) {
            uint32_t v = cp - 0x10000u;
            uint16_t hi = (uint16_t)(0xD800u | (v >> 10));
            uint16_t lo = (uint16_t)(0xDC00u | (v & 0x3FFu));
            if (cap - pos < 4) {
                return DJLINK_ERR_BOUNDS;
            }
            out[pos++] = (uint8_t)(hi & 0xFFu);
            out[pos++] = (uint8_t)(hi >> 8);
            out[pos++] = (uint8_t)(lo & 0xFFu);
            out[pos++] = (uint8_t)(lo >> 8);
        } else {
            if (cap - pos < 2) {
                return DJLINK_ERR_BOUNDS;
            }
            out[pos++] = (uint8_t)(cp & 0xFFu);
            out[pos++] = (uint8_t)(cp >> 8);
        }
    }
    if (pos > 0x7FFFFFFFu) {
        return DJLINK_ERR_BOUNDS;
    }
    return (int)pos;
}

/* --- RPC call / reply codec ---------------------------------------------- */

static void w_call_header(xdr_w_t *w, uint32_t xid, uint32_t prog, uint32_t vers, uint32_t proc)
{
    w_u32(w, xid);
    w_u32(w, DJLINK_RPC_CALL);
    w_u32(w, DJLINK_RPC_VERSION);
    w_u32(w, prog);
    w_u32(w, vers);
    w_u32(w, proc);
    /* AUTH_UNIX: stamp, machinename "", uid 0, gid 0, gids<> = 20 bytes. */
    w_u32(w, DJLINK_RPC_AUTH_UNIX);
    w_u32(w, 20);
    w_u32(w, 0);
    w_u32(w, 0);
    w_u32(w, 0);
    w_u32(w, 0);
    w_u32(w, 0);
    /* Null verifier. */
    w_u32(w, DJLINK_RPC_AUTH_NONE);
    w_u32(w, 0);
}

static int w_finish(const xdr_w_t *w)
{
    if (w->err || w->pos > 0x7FFFFFFFu) {
        return DJLINK_ERR_BOUNDS;
    }
    return (int)w->pos;
}

int djlink_pmap_getport_build(uint8_t *out, size_t cap, uint32_t xid,
                              uint32_t prog, uint32_t vers)
{
    xdr_w_t w = { out, cap, 0, false };
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    w_call_header(&w, xid, DJLINK_PMAP_PROG, DJLINK_PMAP_VERS, DJLINK_PMAP_PROC_GETPORT);
    w_u32(&w, prog);
    w_u32(&w, vers);
    w_u32(&w, DJLINK_IPPROTO_UDP);
    w_u32(&w, 0);
    return w_finish(&w);
}

int djlink_mount_mnt_build(uint8_t *out, size_t cap, uint32_t xid,
                           const uint8_t *dirpath, size_t dirpath_len)
{
    xdr_w_t w = { out, cap, 0, false };
    if (out == NULL || (dirpath == NULL && dirpath_len > 0)) {
        return DJLINK_ERR_NULL;
    }
    if (dirpath_len > DJLINK_NFS_NAME_MAX) {
        return DJLINK_ERR_BOUNDS;
    }
    w_call_header(&w, xid, DJLINK_MOUNT_PROG, DJLINK_MOUNT_VERS, DJLINK_MOUNT_PROC_MNT);
    w_opaque(&w, dirpath, dirpath_len);
    return w_finish(&w);
}

int djlink_nfs_lookup_build(uint8_t *out, size_t cap, uint32_t xid,
                            const uint8_t fh[DJLINK_NFS_FHSIZE],
                            const uint8_t *name, size_t name_len)
{
    xdr_w_t w = { out, cap, 0, false };
    if (out == NULL || fh == NULL || (name == NULL && name_len > 0)) {
        return DJLINK_ERR_NULL;
    }
    if (name_len > DJLINK_NFS_NAME_MAX) {
        return DJLINK_ERR_BOUNDS;
    }
    w_call_header(&w, xid, DJLINK_NFS_PROG, DJLINK_NFS_VERS, DJLINK_NFS_PROC_LOOKUP);
    w_fixed(&w, fh, DJLINK_NFS_FHSIZE);
    w_opaque(&w, name, name_len);
    return w_finish(&w);
}

int djlink_nfs_read_build(uint8_t *out, size_t cap, uint32_t xid,
                          const uint8_t fh[DJLINK_NFS_FHSIZE],
                          uint32_t offset, uint32_t count)
{
    xdr_w_t w = { out, cap, 0, false };
    if (out == NULL || fh == NULL) {
        return DJLINK_ERR_NULL;
    }
    w_call_header(&w, xid, DJLINK_NFS_PROG, DJLINK_NFS_VERS, DJLINK_NFS_PROC_READ);
    w_fixed(&w, fh, DJLINK_NFS_FHSIZE);
    w_u32(&w, offset);
    w_u32(&w, count);
    w_u32(&w, count); /* totalcount: unused by servers, mirrors count */
    return w_finish(&w);
}

djlink_err_t djlink_rpc_reply_parse(const uint8_t *buf, size_t len, djlink_rpc_reply_t *out)
{
    xdr_r_t r = { buf, len, 0, false };
    size_t verf_len = 0;
    if (buf == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    out->xid = r_u32(&r);
    if (r_u32(&r) != DJLINK_RPC_REPLY && !r.err) {
        return DJLINK_ERR_TYPE;
    }
    out->reply_stat = r_u32(&r);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (out->reply_stat == DJLINK_RPC_MSG_ACCEPTED) {
        (void)r_u32(&r);                  /* verifier flavor */
        (void)r_opaque(&r, 400, &verf_len); /* RFC 1057: body <= 400 bytes */
        out->accept_stat = r_u32(&r);
    } else if (out->reply_stat == DJLINK_RPC_MSG_DENIED) {
        out->accept_stat = r_u32(&r);    /* reject_stat */
    } else {
        return DJLINK_ERR_TYPE;
    }
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    out->res = &buf[r.pos];
    out->res_len = len - r.pos;
    return DJLINK_OK;
}

djlink_err_t djlink_pmap_getport_parse(const uint8_t *res, size_t len, uint16_t *port)
{
    xdr_r_t r = { res, len, 0, false };
    uint32_t v;
    if (res == NULL || port == NULL) {
        return DJLINK_ERR_NULL;
    }
    v = r_u32(&r);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (v > 0xFFFFu) {
        return DJLINK_ERR_BOUNDS;
    }
    *port = (uint16_t)v;
    return DJLINK_OK;
}

djlink_err_t djlink_mount_mnt_parse(const uint8_t *res, size_t len, uint32_t *status,
                                    uint8_t fh[DJLINK_NFS_FHSIZE])
{
    xdr_r_t r = { res, len, 0, false };
    const uint8_t *p;
    uint32_t st;
    if (res == NULL || status == NULL || fh == NULL) {
        return DJLINK_ERR_NULL;
    }
    st = r_u32(&r);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (st == 0) {
        p = r_fixed(&r, DJLINK_NFS_FHSIZE);
        if (r.err) {
            return DJLINK_ERR_TRUNCATED;
        }
        memcpy(fh, p, DJLINK_NFS_FHSIZE);
    }
    *status = st;
    return DJLINK_OK;
}

void djlink_nfs_fattr_encode(const djlink_nfs_fattr_t *attr, uint8_t out[DJLINK_NFS_FATTR_LEN])
{
    const uint32_t v[17] = {
        attr->type,     attr->mode,     attr->nlink,   attr->uid,       attr->gid,
        attr->size,     attr->blocksize, attr->rdev,   attr->blocks,    attr->fsid,
        attr->fileid,   attr->atime_s,  attr->atime_us, attr->mtime_s,  attr->mtime_us,
        attr->ctime_s,  attr->ctime_us,
    };
    size_t i;
    for (i = 0; i < 17; i++) {
        djlink_wr32(&out[i * 4], v[i]);
    }
}

void djlink_nfs_fattr_decode(const uint8_t in[DJLINK_NFS_FATTR_LEN], djlink_nfs_fattr_t *attr)
{
    uint32_t *v[17] = {
        &attr->type,     &attr->mode,     &attr->nlink,    &attr->uid,      &attr->gid,
        &attr->size,     &attr->blocksize, &attr->rdev,    &attr->blocks,   &attr->fsid,
        &attr->fileid,   &attr->atime_s,  &attr->atime_us, &attr->mtime_s,  &attr->mtime_us,
        &attr->ctime_s,  &attr->ctime_us,
    };
    size_t i;
    for (i = 0; i < 17; i++) {
        *v[i] = djlink_rd32(&in[i * 4]);
    }
}

djlink_err_t djlink_nfs_lookup_parse(const uint8_t *res, size_t len, uint32_t *status,
                                     uint8_t fh[DJLINK_NFS_FHSIZE],
                                     djlink_nfs_fattr_t *attr)
{
    xdr_r_t r = { res, len, 0, false };
    const uint8_t *pfh;
    const uint8_t *pat;
    uint32_t st;
    if (res == NULL || status == NULL || fh == NULL || attr == NULL) {
        return DJLINK_ERR_NULL;
    }
    st = r_u32(&r);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (st == DJLINK_NFS_OK) {
        pfh = r_fixed(&r, DJLINK_NFS_FHSIZE);
        pat = r_fixed(&r, DJLINK_NFS_FATTR_LEN);
        if (r.err) {
            return DJLINK_ERR_TRUNCATED;
        }
        memcpy(fh, pfh, DJLINK_NFS_FHSIZE);
        djlink_nfs_fattr_decode(pat, attr);
    }
    *status = st;
    return DJLINK_OK;
}

djlink_err_t djlink_nfs_read_parse(const uint8_t *res, size_t len, uint32_t *status,
                                   djlink_nfs_fattr_t *attr,
                                   const uint8_t **data, size_t *data_len)
{
    xdr_r_t r = { res, len, 0, false };
    const uint8_t *pat;
    const uint8_t *pdata;
    size_t n = 0;
    uint32_t st;
    if (res == NULL || status == NULL || attr == NULL || data == NULL || data_len == NULL) {
        return DJLINK_ERR_NULL;
    }
    *data = NULL;
    *data_len = 0;
    st = r_u32(&r);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (st == DJLINK_NFS_OK) {
        pat = r_fixed(&r, DJLINK_NFS_FATTR_LEN);
        pdata = r_opaque(&r, DJLINK_NFS_MAXDATA, &n);
        if (r.err) {
            return DJLINK_ERR_TRUNCATED;
        }
        djlink_nfs_fattr_decode(pat, attr);
        *data = pdata;
        *data_len = n;
    }
    *status = st;
    return DJLINK_OK;
}

djlink_err_t djlink_rpc_call_parse(const uint8_t *buf, size_t len, djlink_rpc_call_t *out)
{
    xdr_r_t r = { buf, len, 0, false };
    const uint8_t *cred;
    size_t cred_len = 0;
    size_t verf_len = 0;
    if (buf == NULL || out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    out->xid = r_u32(&r);
    if (r_u32(&r) != DJLINK_RPC_CALL && !r.err) {
        return DJLINK_ERR_TYPE;
    }
    if (r_u32(&r) != DJLINK_RPC_VERSION && !r.err) {
        return DJLINK_ERR_TYPE;
    }
    out->prog = r_u32(&r);
    out->vers = r_u32(&r);
    out->proc = r_u32(&r);
    out->cred_flavor = r_u32(&r);
    cred = r_opaque(&r, 400, &cred_len);
    (void)r_u32(&r); /* verifier flavor */
    (void)r_opaque(&r, 400, &verf_len);
    if (r.err) {
        return DJLINK_ERR_TRUNCATED;
    }
    if (out->cred_flavor == DJLINK_RPC_AUTH_UNIX) {
        xdr_r_t c = { cred, cred_len, 0, false };
        size_t machine_len = 0;
        (void)r_u32(&c); /* stamp */
        (void)r_opaque(&c, 255, &machine_len);
        out->uid = r_u32(&c);
        if (c.err) {
            return DJLINK_ERR_TRUNCATED;
        }
    }
    out->args = &buf[r.pos];
    out->args_len = len - r.pos;
    return DJLINK_OK;
}

int djlink_rpc_reply_build(uint8_t *out, size_t cap, uint32_t xid, uint32_t accept_stat,
                           const uint8_t *res, size_t res_len)
{
    xdr_w_t w = { out, cap, 0, false };
    if (out == NULL || (res == NULL && res_len > 0)) {
        return DJLINK_ERR_NULL;
    }
    w_u32(&w, xid);
    w_u32(&w, DJLINK_RPC_REPLY);
    w_u32(&w, DJLINK_RPC_MSG_ACCEPTED);
    w_u32(&w, DJLINK_RPC_AUTH_NONE);
    w_u32(&w, 0);
    w_u32(&w, accept_stat);
    if (!w.err && res_len > 0) {
        if (cap - w.pos < res_len) {
            w.err = true;
        } else {
            memcpy(&out[w.pos], res, res_len);
            w.pos += res_len;
        }
    }
    return w_finish(&w);
}

/* --- Fetch client -------------------------------------------------------- */

#define SLOT_FREE     0u
#define SLOT_WAITING  1u
#define SLOT_RECEIVED 2u
#define BACKOFF_SHIFT_MAX 6u

static const char *phase_name(djlink_nfs_phase_t phase)
{
    switch (phase) {
    case DJLINK_NFS_PH_GETPORT_MOUNT:
    case DJLINK_NFS_PH_GETPORT_NFS:
        return "PORTMAP";
    case DJLINK_NFS_PH_MOUNT:
        return "MOUNT";
    case DJLINK_NFS_PH_LOOKUP:
        return "LOOKUP";
    case DJLINK_NFS_PH_READ:
    default:
        return "READ";
    }
}

static void fail(djlink_nfs_t *c, djlink_nfs_error_t err, uint32_t status, const char *text)
{
    size_t i;
    c->state = DJLINK_NFS_FAILED;
    c->error = err;
    c->status = status;
    snprintf(c->error_text, sizeof(c->error_text), "%s", text);
    for (i = 0; i < DJLINK_NFS_WINDOW_MAX; i++) {
        c->slots[i].state = SLOT_FREE;
    }
}

static uint32_t backoff_ms(const djlink_nfs_t *c, uint8_t tries)
{
    uint8_t shift = tries > BACKOFF_SHIFT_MAX ? BACKOFF_SHIFT_MAX : tries;
    return c->cfg.retransmit_ms << shift;
}

static bool expired(uint32_t now_ms, uint32_t sent_ms, uint32_t timeout_ms)
{
    return (uint32_t)(now_ms - sent_ms) >= timeout_ms;
}

/* Bounds of the path element at c->path_pos; false when none is left. */
static bool path_element(const djlink_nfs_t *c, size_t *start, size_t *len)
{
    size_t pos = c->path_pos;
    size_t end;
    while (c->path[pos] == '/') {
        pos++;
    }
    if (c->path[pos] == '\0') {
        return false;
    }
    end = pos;
    while (c->path[end] != '\0' && c->path[end] != '/') {
        end++;
    }
    *start = pos;
    *len = end - pos;
    return true;
}

static void send_request(djlink_nfs_t *c, uint16_t port, uint32_t now_ms)
{
    c->req_port = port;
    c->req_sent_ms = now_ms;
    /* A failed send is a lost datagram: poll() retransmits it. */
    (void)c->io.send(c->io.ctx, c->cfg.host_ip, port, c->tx, c->tx_len);
}

/* Build tx for the current phase with a fresh xid and send it. */
static void start_request(djlink_nfs_t *c, uint32_t now_ms)
{
    uint8_t name[DJLINK_NFS_NAME_MAX];
    size_t start = 0;
    size_t len = 0;
    int n = DJLINK_ERR_BOUNDS;
    int enc;
    uint16_t port = c->cfg.portmap_port;

    c->req_xid = c->next_xid++;
    c->req_tries = 0;
    switch (c->phase) {
    case DJLINK_NFS_PH_GETPORT_MOUNT:
        n = djlink_pmap_getport_build(c->tx, sizeof(c->tx), c->req_xid,
                                      DJLINK_MOUNT_PROG, DJLINK_MOUNT_VERS);
        break;
    case DJLINK_NFS_PH_GETPORT_NFS:
        n = djlink_pmap_getport_build(c->tx, sizeof(c->tx), c->req_xid,
                                      DJLINK_NFS_PROG, DJLINK_NFS_VERS);
        break;
    case DJLINK_NFS_PH_MOUNT:
        enc = djlink_nfs_name_encode(c->export_path, strlen(c->export_path), c->cfg.charset,
                                     name, sizeof(name));
        if (enc >= 0) {
            n = djlink_mount_mnt_build(c->tx, sizeof(c->tx), c->req_xid, name, (size_t)enc);
        }
        port = c->mount_port;
        break;
    case DJLINK_NFS_PH_LOOKUP:
        if (path_element(c, &start, &len)) {
            enc = djlink_nfs_name_encode(&c->path[start], len, c->cfg.charset, name, sizeof(name));
            if (enc >= 0) {
                n = djlink_nfs_lookup_build(c->tx, sizeof(c->tx), c->req_xid, c->fh, name,
                                            (size_t)enc);
            }
        }
        port = c->nfs_port;
        break;
    case DJLINK_NFS_PH_READ:
    default:
        break;
    }
    if (n < 0) {
        /* Names were validated in fetch(); this is a programming error. */
        fail(c, DJLINK_NFS_E_ARG, 0, "BAD NAME");
        return;
    }
    c->tx_len = (size_t)n;
    send_request(c, port, now_ms);
}

static uint8_t *slot_buf(djlink_nfs_t *c, size_t i)
{
    return &c->cfg.window_buf[i * c->read_size];
}

static void send_read(djlink_nfs_t *c, const djlink_nfs_slot_t *s)
{
    uint8_t tx[128];
    int n = djlink_nfs_read_build(tx, sizeof(tx), s->xid, c->fh, s->offset, s->count);
    if (n > 0) {
        (void)c->io.send(c->io.ctx, c->cfg.host_ip, c->nfs_port, tx, (size_t)n);
    }
}

/* Keep the window full up to the end of the file. */
static void fill_window(djlink_nfs_t *c, uint32_t now_ms)
{
    size_t i;
    for (i = 0; i < c->window && c->requested < c->size; i++) {
        djlink_nfs_slot_t *s = &c->slots[i];
        uint32_t left;
        if (s->state != SLOT_FREE) {
            continue;
        }
        left = c->size - c->requested;
        s->xid = c->next_xid++;
        s->offset = c->requested;
        s->count = left < c->read_size ? left : c->read_size;
        s->len = 0;
        s->tries = 0;
        s->sent_ms = now_ms;
        s->state = SLOT_WAITING;
        c->requested += s->count;
        send_read(c, s);
    }
}

static void finish_done(djlink_nfs_t *c)
{
    size_t i;
    c->state = DJLINK_NFS_DONE;
    c->error = DJLINK_NFS_E_NONE;
    c->error_text[0] = '\0';
    for (i = 0; i < DJLINK_NFS_WINDOW_MAX; i++) {
        c->slots[i].state = SLOT_FREE;
    }
}

/* Write the data of the slot at c->delivered. Returns false once the fetch
 * has left BUSY or the window was reset. */
static bool deliver(djlink_nfs_t *c, size_t idx, const uint8_t *data, uint32_t len)
{
    djlink_nfs_slot_t *s = &c->slots[idx];
    uint32_t count = s->count;
    size_t i;

    s->state = SLOT_FREE;
    if (len == 0) {
        fail(c, DJLINK_NFS_E_READ, 0, "READ EMPTY BEFORE EOF");
        return false;
    }
    if (c->io.write(c->io.ctx, c->delivered, data, len) != 0) {
        fail(c, DJLINK_NFS_E_SINK, 0, "CACHE WRITE FAILED");
        return false;
    }
    c->delivered += len;
    if (c->io.progress != NULL) {
        c->io.progress(c->io.ctx, c->delivered, c->size);
    }
    if (c->delivered >= c->size) {
        finish_done(c);
        return false;
    }
    if (len < count) {
        /* Short read before EOF: everything past it is misaligned. Drop
         * the rest of the window (late replies become strays) and resume
         * from what was written. */
        for (i = 0; i < DJLINK_NFS_WINDOW_MAX; i++) {
            c->slots[i].state = SLOT_FREE;
        }
        c->requested = c->delivered;
        return false;
    }
    return true;
}

/* Deliver buffered slots that became contiguous. */
static void flush_window(djlink_nfs_t *c)
{
    bool progressed = true;
    while (progressed && c->state == DJLINK_NFS_BUSY) {
        size_t i;
        progressed = false;
        for (i = 0; i < c->window; i++) {
            djlink_nfs_slot_t *s = &c->slots[i];
            if (s->state == SLOT_RECEIVED && s->offset == c->delivered) {
                if (!deliver(c, i, slot_buf(c, i), s->len)) {
                    return;
                }
                progressed = true;
                break;
            }
        }
    }
}

static void on_read_reply(djlink_nfs_t *c, const djlink_rpc_reply_t *rep, uint32_t now_ms)
{
    djlink_nfs_fattr_t attr;
    const uint8_t *data = NULL;
    size_t data_len = 0;
    uint32_t st = 0;
    size_t idx;
    djlink_nfs_slot_t *s = NULL;

    for (idx = 0; idx < c->window; idx++) {
        if (c->slots[idx].state == SLOT_WAITING && c->slots[idx].xid == rep->xid) {
            s = &c->slots[idx];
            break;
        }
    }
    if (s == NULL) {
        c->stray_replies++;
        return;
    }
    if (rep->reply_stat != DJLINK_RPC_MSG_ACCEPTED || rep->accept_stat != DJLINK_RPC_SUCCESS) {
        fail(c, DJLINK_NFS_E_RPC, rep->accept_stat, "RPC ERROR READ");
        return;
    }
    if (djlink_nfs_read_parse(rep->res, rep->res_len, &st, &attr, &data, &data_len) != DJLINK_OK ||
        (st == DJLINK_NFS_OK && data_len > s->count)) {
        fail(c, DJLINK_NFS_E_BAD_REPLY, 0, "BAD READ REPLY");
        return;
    }
    if (st != DJLINK_NFS_OK) {
        char text[sizeof(c->error_text)];
        snprintf(text, sizeof(text), "READ ERROR (%lu)", (unsigned long)st);
        fail(c, DJLINK_NFS_E_READ, st, text);
        return;
    }
    if (s->offset == c->delivered) {
        if (!deliver(c, idx, data, (uint32_t)data_len)) {
            if (c->state == DJLINK_NFS_BUSY) {
                fill_window(c, now_ms);
            }
            return;
        }
        flush_window(c);
    } else {
        if (data_len > 0) {
            memcpy(slot_buf(c, idx), data, data_len);
        }
        s->len = (uint32_t)data_len;
        s->state = SLOT_RECEIVED;
    }
    if (c->state == DJLINK_NFS_BUSY) {
        fill_window(c, now_ms);
    }
}

static void begin_read(djlink_nfs_t *c, uint32_t now_ms)
{
    c->phase = DJLINK_NFS_PH_READ;
    c->requested = 0;
    c->delivered = 0;
    if (c->io.open != NULL && c->io.open(c->io.ctx, c->size) != 0) {
        fail(c, DJLINK_NFS_E_SINK, 0, "CACHE OPEN FAILED");
        return;
    }
    if (c->size == 0) {
        if (c->io.progress != NULL) {
            c->io.progress(c->io.ctx, 0, 0);
        }
        finish_done(c);
        return;
    }
    fill_window(c, now_ms);
}

static void on_single_reply(djlink_nfs_t *c, const djlink_rpc_reply_t *rep, uint32_t now_ms)
{
    char text[sizeof(c->error_text)];
    djlink_nfs_fattr_t attr;
    uint16_t port = 0;
    uint32_t st = 0;
    size_t start = 0;
    size_t len = 0;

    if (rep->xid != c->req_xid) {
        c->stray_replies++;
        return;
    }
    if (rep->reply_stat != DJLINK_RPC_MSG_ACCEPTED) {
        snprintf(text, sizeof(text), "RPC DENIED %s", phase_name(c->phase));
        fail(c, DJLINK_NFS_E_RPC, rep->accept_stat, text);
        return;
    }
    if (rep->accept_stat != DJLINK_RPC_SUCCESS) {
        snprintf(text, sizeof(text), "RPC ERROR %s (%lu)", phase_name(c->phase),
                 (unsigned long)rep->accept_stat);
        fail(c, DJLINK_NFS_E_RPC, rep->accept_stat, text);
        return;
    }

    switch (c->phase) {
    case DJLINK_NFS_PH_GETPORT_MOUNT:
    case DJLINK_NFS_PH_GETPORT_NFS:
        if (djlink_pmap_getport_parse(rep->res, rep->res_len, &port) != DJLINK_OK) {
            fail(c, DJLINK_NFS_E_BAD_REPLY, 0, "BAD PORTMAP REPLY");
            return;
        }
        if (port == 0) {
            fail(c, DJLINK_NFS_E_NO_SERVICE, 0,
                 c->phase == DJLINK_NFS_PH_GETPORT_MOUNT ? "NO MOUNT SERVICE" : "NO NFS SERVICE");
            return;
        }
        if (c->phase == DJLINK_NFS_PH_GETPORT_MOUNT) {
            c->mount_port = port;
            c->phase = DJLINK_NFS_PH_GETPORT_NFS;
        } else {
            c->nfs_port = port;
            c->phase = DJLINK_NFS_PH_MOUNT;
        }
        start_request(c, now_ms);
        return;

    case DJLINK_NFS_PH_MOUNT:
        if (djlink_mount_mnt_parse(rep->res, rep->res_len, &st, c->fh) != DJLINK_OK) {
            fail(c, DJLINK_NFS_E_BAD_REPLY, 0, "BAD MOUNT REPLY");
            return;
        }
        if (st != 0) {
            snprintf(text, sizeof(text), "EXPORT REFUSED (%lu)", (unsigned long)st);
            fail(c, DJLINK_NFS_E_MOUNT, st, text);
            return;
        }
        c->phase = DJLINK_NFS_PH_LOOKUP;
        c->path_pos = 0;
        start_request(c, now_ms);
        return;

    case DJLINK_NFS_PH_LOOKUP:
        if (!path_element(c, &start, &len)) {
            fail(c, DJLINK_NFS_E_ARG, 0, "BAD PATH");
            return;
        }
        if (djlink_nfs_lookup_parse(rep->res, rep->res_len, &st, c->fh, &attr) != DJLINK_OK) {
            fail(c, DJLINK_NFS_E_BAD_REPLY, 0, "BAD LOOKUP REPLY");
            return;
        }
        if (st != DJLINK_NFS_OK) {
            if (st == DJLINK_NFSERR_NOENT) {
                snprintf(text, sizeof(text), "NOT FOUND: %.*s",
                         (int)(len > 30 ? 30 : len), &c->path[start]);
            } else {
                snprintf(text, sizeof(text), "LOOKUP ERROR (%lu)", (unsigned long)st);
            }
            fail(c, DJLINK_NFS_E_LOOKUP, st, text);
            return;
        }
        c->path_pos = start + len;
        if (path_element(c, &start, &len)) {
            if (attr.type != DJLINK_NFS_TYPE_DIR) {
                fail(c, DJLINK_NFS_E_LOOKUP, DJLINK_NFSERR_NOTDIR, "NOT A DIRECTORY");
                return;
            }
            start_request(c, now_ms);
            return;
        }
        if (attr.type != DJLINK_NFS_TYPE_REG) {
            fail(c, DJLINK_NFS_E_NOT_FILE, 0, "NOT A FILE");
            return;
        }
        if (c->cfg.max_size != 0 && attr.size > c->cfg.max_size) {
            fail(c, DJLINK_NFS_E_TOO_BIG, 0, "FILE TOO BIG");
            return;
        }
        c->size = attr.size;
        c->attr = attr;
        begin_read(c, now_ms);
        return;

    case DJLINK_NFS_PH_READ:
    default:
        return;
    }
}

djlink_nfs_error_t djlink_nfs_fetch(djlink_nfs_t *c, const djlink_nfs_fetch_cfg_t *cfg,
                                    const djlink_nfs_io_t *io, uint32_t now_ms)
{
    uint8_t name[DJLINK_NFS_NAME_MAX];
    size_t start = 0;
    size_t len = 0;
    size_t elements = 0;
    size_t window;

    if (c == NULL) {
        return DJLINK_NFS_E_ARG;
    }
    memset(c, 0, sizeof(*c));
    if (cfg == NULL || io == NULL || io->send == NULL || io->write == NULL ||
        cfg->export_path == NULL || cfg->path == NULL || cfg->host_ip == 0 ||
        strlen(cfg->export_path) >= sizeof(c->export_path) ||
        strlen(cfg->path) >= sizeof(c->path) ||
        (cfg->charset != DJLINK_NFS_NAMES_UTF16LE && cfg->charset != DJLINK_NFS_NAMES_BYTES) ||
        (cfg->read_size != 0 &&
         (cfg->read_size < DJLINK_NFS_READ_MIN || cfg->read_size > DJLINK_NFS_MAXDATA))) {
        fail(c, DJLINK_NFS_E_ARG, 0, "BAD FETCH CONFIG");
        return DJLINK_NFS_E_ARG;
    }
    c->io = *io;
    c->cfg = *cfg;
    memcpy(c->export_path, cfg->export_path, strlen(cfg->export_path) + 1);
    memcpy(c->path, cfg->path, strlen(cfg->path) + 1);
    c->cfg.export_path = c->export_path;
    c->cfg.path = c->path;
    if (c->cfg.portmap_port == 0) {
        c->cfg.portmap_port = DJLINK_PMAP_PORT;
    }
    if (c->cfg.retransmit_ms == 0) {
        c->cfg.retransmit_ms = DJLINK_NFS_RETRANSMIT_MS;
    }
    if (c->cfg.retries == 0) {
        c->cfg.retries = DJLINK_NFS_RETRIES;
    }
    c->read_size = c->cfg.read_size != 0 ? c->cfg.read_size : DJLINK_NFS_READ_DEFAULT;

    /* The reorder buffer bounds the window; one READ needs none. */
    window = c->cfg.window == 0 ? 1u : c->cfg.window;
    if (window > DJLINK_NFS_WINDOW_MAX) {
        window = DJLINK_NFS_WINDOW_MAX;
    }
    if (window > 1) {
        size_t fit = c->cfg.window_buf == NULL ? 0 : c->cfg.window_buf_len / c->read_size;
        if (window > fit) {
            window = fit;
        }
        if (window < 1) {
            window = 1;
        }
    }
    c->window = (uint8_t)window;

    /* Every name must encode before anything goes on the wire. */
    if (djlink_nfs_name_encode(c->export_path, strlen(c->export_path), c->cfg.charset, name,
                               sizeof(name)) < 0) {
        fail(c, DJLINK_NFS_E_ARG, 0, "BAD EXPORT NAME");
        return DJLINK_NFS_E_ARG;
    }
    while (path_element(c, &start, &len)) {
        if (djlink_nfs_name_encode(&c->path[start], len, c->cfg.charset, name, sizeof(name)) < 0) {
            fail(c, DJLINK_NFS_E_ARG, 0, "BAD PATH NAME");
            return DJLINK_NFS_E_ARG;
        }
        c->path_pos = start + len;
        elements++;
    }
    c->path_pos = 0;
    if (elements == 0) {
        fail(c, DJLINK_NFS_E_ARG, 0, "EMPTY PATH");
        return DJLINK_NFS_E_ARG;
    }

    c->next_xid = c->cfg.xid_seed;
    c->state = DJLINK_NFS_BUSY;
    c->phase = DJLINK_NFS_PH_GETPORT_MOUNT;
    start_request(c, now_ms);
    return c->state == DJLINK_NFS_BUSY ? DJLINK_NFS_E_NONE : c->error;
}

void djlink_nfs_on_datagram(djlink_nfs_t *c, const uint8_t *buf, size_t len, uint32_t now_ms)
{
    djlink_rpc_reply_t rep;
    if (c == NULL || buf == NULL || c->state != DJLINK_NFS_BUSY) {
        return;
    }
    if (djlink_rpc_reply_parse(buf, len, &rep) != DJLINK_OK) {
        c->stray_replies++;
        return;
    }
    if (c->phase == DJLINK_NFS_PH_READ) {
        on_read_reply(c, &rep, now_ms);
    } else {
        on_single_reply(c, &rep, now_ms);
    }
}

void djlink_nfs_poll(djlink_nfs_t *c, uint32_t now_ms)
{
    char text[sizeof(c->error_text)];
    size_t i;
    if (c == NULL || c->state != DJLINK_NFS_BUSY) {
        return;
    }
    if (c->phase != DJLINK_NFS_PH_READ) {
        if (!expired(now_ms, c->req_sent_ms, backoff_ms(c, c->req_tries))) {
            return;
        }
        if (c->req_tries >= c->cfg.retries) {
            snprintf(text, sizeof(text), "TIMEOUT %s", phase_name(c->phase));
            fail(c, DJLINK_NFS_E_TIMEOUT, 0, text);
            return;
        }
        c->req_tries++;
        c->retransmits++;
        send_request(c, c->req_port, now_ms);
        return;
    }
    for (i = 0; i < c->window; i++) {
        djlink_nfs_slot_t *s = &c->slots[i];
        if (s->state != SLOT_WAITING || !expired(now_ms, s->sent_ms, backoff_ms(c, s->tries))) {
            continue;
        }
        if (s->tries >= c->cfg.retries) {
            fail(c, DJLINK_NFS_E_TIMEOUT, 0, "TIMEOUT READ");
            return;
        }
        s->tries++;
        s->sent_ms = now_ms;
        c->retransmits++;
        send_read(c, s); /* same xid: the server may answer either copy */
    }
}

void djlink_nfs_cancel(djlink_nfs_t *c)
{
    size_t i;
    if (c == NULL || c->state != DJLINK_NFS_BUSY) {
        return;
    }
    c->state = DJLINK_NFS_CANCELLED;
    snprintf(c->error_text, sizeof(c->error_text), "CANCELLED");
    for (i = 0; i < DJLINK_NFS_WINDOW_MAX; i++) {
        c->slots[i].state = SLOT_FREE;
    }
}

djlink_nfs_state_t djlink_nfs_state(const djlink_nfs_t *c)
{
    return c == NULL ? DJLINK_NFS_IDLE : c->state;
}

const char *djlink_nfs_error_text(const djlink_nfs_t *c)
{
    return c == NULL ? "" : c->error_text;
}
