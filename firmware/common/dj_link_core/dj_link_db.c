/* MIT kayrozen/Pajoniiir 428b97dd; see README.md and LICENSE.
 * Local bounds, prefix framing and explicit source/connection generations. */
#include "dj_link_db.h"

#include <stdio.h>
#include <string.h>

#include "djlink/dbserver.h"

#define DJ_LINK_DB_NO_ITEMS     0xffffffffu

static const char *phase_name(dj_link_db_phase_t phase)
{
    switch (phase) {
    case DJ_LINK_DB_DISC_CONNECTING:    return "CONNECT";
    case DJ_LINK_DB_DISC_WAIT_PORT:     return "PORT";
    case DJ_LINK_DB_SRV_CONNECTING:     return "CONNECT DB";
    case DJ_LINK_DB_SRV_WAIT_GREETING:  return "GREETING";
    case DJ_LINK_DB_SRV_WAIT_SETUP:     return "SETUP";
    case DJ_LINK_DB_LIST_WAIT_AVAIL:    return "LIST";
    case DJ_LINK_DB_LIST_WAIT_RENDER:   return "RENDER";
    case DJ_LINK_DB_DETAIL_WAIT_AVAIL:  return "DETAIL";
    case DJ_LINK_DB_DETAIL_WAIT_RENDER: return "DETAIL RENDER";
    case DJ_LINK_DB_PATH_WAIT_AVAIL:    return "PATH";
    case DJ_LINK_DB_PATH_WAIT_RENDER:   return "PATH RENDER";
    case DJ_LINK_DB_BLOB_WAIT:          return "ANALYSIS";
    default:                            return "?";
    }
}

static bool has_connection(const dj_link_db_t *c)
{
    return c->phase != DJ_LINK_DB_IDLE && c->phase != DJ_LINK_DB_FAILED;
}

static void close_connection(dj_link_db_t *c)
{
    ++c->connection_epoch;
    if (has_connection(c) && c->io.close) {
        c->io.close(c->io.ctx);
    }
    c->rx_len = 0;
}

static void fail(dj_link_db_t *c, const char *reason)
{
    close_connection(c);
    snprintf(c->error, sizeof(c->error), "%s", reason);
    c->phase = DJ_LINK_DB_FAILED;
}

static void wait_for(dj_link_db_t *c, dj_link_db_phase_t phase, uint32_t now_ms)
{
    c->phase = phase;
    c->deadline_ms = now_ms + c->step_timeout_ms;
}

static void connect_to(dj_link_db_t *c, uint16_t port, dj_link_db_phase_t phase,
                       uint32_t now_ms)
{
    ++c->connection_epoch;
    if (!c->io.connect || c->io.connect(c->io.ctx, c->peer_ip, port) != 0) {
        c->phase = DJ_LINK_DB_IDLE; /* nothing to close */
        fail(c, "CONNECT FAILED");
        return;
    }
    wait_for(c, phase, now_ms);
}

/* Send what was just built into c->tx, then wait in `phase`. */
static void send_tx(dj_link_db_t *c, int n, dj_link_db_phase_t phase, uint32_t now_ms)
{
    if (n <= 0 || !c->io.send || c->io.send(c->io.ctx, c->tx, (size_t)n) != 0) {
        fail(c, "SEND FAILED");
        return;
    }
    wait_for(c, phase, now_ms);
}

static uint32_t dmst(const dj_link_db_t *c)
{
    return ((uint32_t)c->our_number << 24) | (0x01u << 16) |
           ((uint32_t)c->slot << 8) | 0x01u; /* main menu, rekordbox track */
}

static void request_render(dj_link_db_t *c, uint32_t offset, uint32_t count,
                           dj_link_db_phase_t phase, uint32_t now_ms)
{
    c->render_offset = offset;
    c->render_count = count;
    c->render_seen = 0;
    int n = djlink_db_render_request_build(++c->txid, c->our_number, c->slot,
                                           offset, count, c->tx, sizeof(c->tx));
    send_tx(c, n, phase, now_ms);
}

static void go_ready(dj_link_db_t *c, uint32_t now_ms)
{
    c->phase = DJ_LINK_DB_READY;
    c->idle_since_ms = now_ms;
}

static void blob_reset(dj_link_db_t *c)
{
    c->blob_request = 0u;
    c->blob_id = 0u;
    c->blob_dst = NULL;
    c->blob_cap = 0u;
    c->blob_body = false;
    c->blob_typed = false;
}

/* READY: list first, then a wanted file path, then a wanted analysis blob,
 * then any metadata the owner asks for. */
static void start_next(dj_link_db_t *c, uint32_t now_ms)
{
    if (c->phase != DJ_LINK_DB_READY) {
        return;
    }
    if (c->list_done && c->path_id != 0u) {
        djlink_db_arg_t args[2];
        memset(args, 0, sizeof(args));
        args[0].type = DJLINK_DB_FIELD_INT32;
        args[0].num = dmst(c);
        args[1].type = DJLINK_DB_FIELD_INT32;
        args[1].num = c->path_id;
        c->path[0] = '\0';
        int n = djlink_db_msg_build(++c->txid, DJ_LINK_DB_TYPE_TRACK_INFO, args, 2,
                                    c->tx, sizeof(c->tx));
        send_tx(c, n, DJ_LINK_DB_PATH_WAIT_AVAIL, now_ms);
        return;
    }
    if (c->list_done && c->blob_request != 0u && c->blob_id != 0u) {
        djlink_db_arg_t args[3];
        memset(args, 0, sizeof(args));
        args[0].type = DJLINK_DB_FIELD_INT32;
        args[0].num = dmst(c);
        args[1].type = DJLINK_DB_FIELD_INT32;
        args[1].num = c->blob_id;
        args[2].type = DJLINK_DB_FIELD_INT32; /* wave detail, cues: trailing 0 */
        c->blob_len = 0u;
        c->blob_body = false;
        const bool trailing = c->blob_request == DJLINK_DB_TYPE_WAVEFORM_REQUEST ||
                              c->blob_request == DJLINK_DB_TYPE_CUES_EXT_REQUEST;
        const bool tag = c->blob_request == DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST;
        djlink_db_arg_t all[4];
        memset(all, 0, sizeof(all));
        memcpy(all, args, sizeof(args));
        if (tag) {   /* v314: [DMST, id, 'PWV4', 'EXT'], reversed fourccs */
            all[2].type = DJLINK_DB_FIELD_INT32;
            all[2].num = DJ_LINK_DB_ANLZ_TAG_PWV4;
            all[3].type = DJLINK_DB_FIELD_INT32;
            all[3].num = DJ_LINK_DB_ANLZ_FILE_EXT;
        }
        int n = djlink_db_msg_build(++c->txid, c->blob_request, all,
                                    tag ? 4 : trailing ? 3 : 2, c->tx, sizeof(c->tx));
        send_tx(c, n, DJ_LINK_DB_BLOB_WAIT, now_ms);
        return;
    }
    if (!c->list_done) {
        djlink_db_arg_t args[4];
        memset(args, 0, sizeof(args));
        for (size_t i = 0; i < 4u; i++) {
            args[i].type = DJLINK_DB_FIELD_INT32;
        }
        args[0].num = dmst(c);
        const bool all = c->menu == DJ_LINK_DB_MENU_ALL_TRACKS;
        args[1].num = all ? c->sort : DJ_LINK_DB_SORT_DEFAULT;
        args[2].num = c->menu_id;
        args[3].num = c->menu == DJ_LINK_DB_MENU_FOLDER ? 1u : 0u;
        int n = djlink_db_msg_build(++c->txid,
                                    all ? DJ_LINK_DB_TYPE_TRACK_MENU : DJ_LINK_DB_TYPE_PLAYLIST_MENU,
                                    args, all ? 2 : 4, c->tx, sizeof(c->tx));
        send_tx(c, n, DJ_LINK_DB_LIST_WAIT_AVAIL, now_ms);
        return;
    }
    uint32_t index;
    uint32_t id;
    if (c->io.next_detail && c->io.next_detail(c->io.ctx, &index, &id)) {
        c->detail_index = index;
        memset(&c->detail, 0, sizeof(c->detail));
        c->detail.rekordbox_id = id;
        c->detail.audio = c->audio;
        int n = djlink_db_metadata_request_build(++c->txid, c->our_number, c->slot, id,
                                                 c->tx, sizeof(c->tx));
        send_tx(c, n, DJ_LINK_DB_DETAIL_WAIT_AVAIL, now_ms);
    }
}

bool dj_link_db_pick_detail(const dj_link_peer_track_t *rows, uint32_t count,
                            uint32_t first, uint32_t n, uint32_t prio_index,
                            uint32_t prio_id, uint32_t *index)
{
    if (!rows || !index) {
        return false;
    }
    if (prio_id != 0u && prio_index < count && rows[prio_index].rekordbox_id == prio_id &&
        !rows[prio_index].has_detail) {
        *index = prio_index;
        return true;
    }
    uint32_t end = first + n;
    if (end < first || end > count) {
        end = count;
    }
    for (uint32_t i = first; i < end; i++) {
        if (!rows[i].has_detail && rows[i].rekordbox_id != 0u) {
            *index = i;
            return true;
        }
    }
    return false;
}

void dj_link_db_utf16be_to_utf8(const uint8_t *in, size_t in_len, char *out, size_t cap)
{
    size_t o = 0;
    if (!out || cap == 0) {
        return;
    }
    for (size_t i = 0; in && i + 1 < in_len; i += 2) {
        uint32_t cp = ((uint32_t)in[i] << 8) | in[i + 1];
        if (cp == 0) {
            break;
        }
        if (cp >= 0xd800u && cp <= 0xdfffu) {
            if (cp < 0xdc00u && i + 3 < in_len) {
                uint16_t low = ((uint16_t)in[i+2] << 8) | in[i+3];
                if (low >= 0xdc00u && low <= 0xdfffu)
                    i += 2; /* one '?' for a valid surrogate pair */
            }
            cp = '?';
        }
        size_t need = cp < 0x80u ? 1u : (cp < 0x800u ? 2u : 3u);
        if (o + need >= cap) {
            break;
        }
        if (need == 1) {
            out[o++] = (char)cp;
        } else if (need == 2) {
            out[o++] = (char)(0xc0u | (cp >> 6));
            out[o++] = (char)(0x80u | (cp & 0x3fu));
        } else {
            out[o++] = (char)(0xe0u | (cp >> 12));
            out[o++] = (char)(0x80u | ((cp >> 6) & 0x3fu));
            out[o++] = (char)(0x80u | (cp & 0x3fu));
        }
    }
    out[o] = '\0';
}

int dj_link_db_msg_size(const uint8_t *buf, size_t len)
{
    djlink_db_msg_t m;
    if (len == 0u) {
        return 0; /* the codec reports an empty buffer as bad magic */
    }
    size_t consumed = 0;
    djlink_err_t err = djlink_db_msg_parse_prefix(buf, len, &m, &consumed);
    if (err == DJLINK_ERR_TRUNCATED) {
        return len >= DJ_LINK_DB_RX_MAX ? -1 : 0;
    }
    if (err != DJLINK_OK) {
        return -1;
    }
    return consumed <= DJ_LINK_DB_RX_MAX ? (int)consumed : -1;
}

/* Menu item: 0 parent, 1 id / number, 3 label 1, 5 label 2, 6 item type. */
static bool item_fields(const djlink_db_msg_t *m, uint32_t *num,
                        const djlink_db_arg_t **label1, const djlink_db_arg_t **label2,
                        uint32_t *item_type)
{
    if (m->arg_count < 7u ||
        m->args[1].type != DJLINK_DB_FIELD_INT32 ||
        m->args[3].type != DJLINK_DB_FIELD_STRING ||
        m->args[5].type != DJLINK_DB_FIELD_STRING ||
        m->args[6].type != DJLINK_DB_FIELD_INT32) {
        return false;
    }
    *num = m->args[1].num;
    *label1 = &m->args[3];
    *label2 = &m->args[5];
    *item_type = m->args[6].num & 0xffffu;
    return true;
}

static uint32_t menu_count(const djlink_db_msg_t *m)
{
    if (m->arg_count < 2u || m->args[1].num == DJ_LINK_DB_NO_ITEMS) {
        return 0;
    }
    return m->args[1].num;
}

/* Filenames must be lossless: a clipped or substituted display string can
 * accidentally identify a different file. Titles keep their display policy. */
bool dj_link_db_decode_path(const uint8_t *in,size_t len,char *out,size_t cap)
{
    if(!out || !cap)return false;
    out[0]=0;if(!in || (len&1))return false;
    size_t o=0;
    for(size_t i=0;i<len;i+=2) {
        uint32_t cp=((uint32_t)in[i]<<8)|in[i+1];
        if(!cp)break;
        if(cp>=0xd800 && cp<=0xdbff) {
            if(i+3>=len)goto bad;
            uint32_t lo=((uint32_t)in[i+2]<<8)|in[i+3];
            if(lo<0xdc00 || lo>0xdfff)goto bad;
            cp=0x10000+((cp-0xd800)<<10)+(lo-0xdc00);i+=2;
        } else if(cp>=0xdc00 && cp<=0xdfff)goto bad;
        size_t n=cp<0x80?1:cp<0x800?2:cp<0x10000?3:4;
        if(o+n>=cap)goto bad;
        if(n==1)out[o++]=(char)cp;
        else {
            out[o++]=(char)((n==2?0xc0:n==3?0xe0:0xf0)|(cp>>(6*(n-1))));
            for(size_t j=n-1;j;j--)out[o++]=(char)(0x80|((cp>>(6*(j-1)))&63));
        }
    }
    out[o]=0;return o>0;
bad:
    out[0]=0;return false;
}

static bool menu_success(const djlink_db_msg_t *m, uint32_t request)
{
    return m->type == DJLINK_DB_TYPE_SUCCESS && m->arg_count >= 2 &&
        m->args[0].type == DJLINK_DB_FIELD_INT32 && m->args[1].type == DJLINK_DB_FIELD_INT32 &&
        m->args[0].num == request;
}

/* v300: argument 8 of a title item is its artwork id (0 = none). */
static uint32_t item_artwork(const djlink_db_msg_t *m, uint32_t item_type)
{
    if ((item_type & 0xffu) != DJ_LINK_DB_ITEM_TITLE || m->arg_count < 9u ||
        m->args[8].type != DJLINK_DB_FIELD_INT32) {
        return 0u;
    }
    return m->args[8].num;
}

static void list_item(dj_link_db_t *c, const djlink_db_msg_t *m)
{
    uint32_t id;
    uint32_t item_type;
    const djlink_db_arg_t *l1;
    const djlink_db_arg_t *l2;
    if (c->render_seen >= c->render_count || !item_fields(m, &id, &l1, &l2, &item_type)) {
        fail(c, "BAD MENU ITEM");
        return;
    }
    dj_link_peer_track_t t;
    memset(&t, 0, sizeof(t));
    t.rekordbox_id = id;
    t.audio = c->audio;
    dj_link_db_utf16be_to_utf8(l1->bin, l1->bin_len, t.title, sizeof(t.title));
    /* v311: a folder or playlist row of a playlist menu: a name to open, not
     * a track (no metadata, never downloaded). */
    if (item_type == DJ_LINK_DB_ITEM_FOLDER || item_type == DJ_LINK_DB_ITEM_PLAYLIST) {
        t.kind = item_type == DJ_LINK_DB_ITEM_FOLDER ? DJ_LINK_PEER_ROW_FOLDER
                                                     : DJ_LINK_PEER_ROW_PLAYLIST;
        t.has_detail = true;
        t.audio = DJ_LINK_PEER_AUDIO_METADATA_ONLY;
        if (c->io.track) {
            c->io.track(c->io.ctx, c->render_offset + c->render_seen, &t, false);
        }
        c->render_seen++;
        return;
    }
    t.artwork_id = item_artwork(m, item_type);
    /* The second label follows the player's sort; it is the artist only for
     * "title + artist" items. */
    if (item_type == DJ_LINK_DB_ITEM_TITLE_ARTIST) {
        dj_link_db_utf16be_to_utf8(l2->bin, l2->bin_len, t.artist, sizeof(t.artist));
    }
    if (c->io.track) {
        c->io.track(c->io.ctx, c->render_offset + c->render_seen, &t, false);
    }
    c->render_seen++;
}

static void detail_item(dj_link_db_t *c, const djlink_db_msg_t *m)
{
    uint32_t num;
    uint32_t item_type;
    const djlink_db_arg_t *l1;
    const djlink_db_arg_t *l2;
    if (!item_fields(m, &num, &l1, &l2, &item_type)) {
        fail(c, "BAD METADATA ITEM");
        return;
    }
    switch (item_type) {
    case DJ_LINK_DB_ITEM_TITLE:
        dj_link_db_utf16be_to_utf8(l1->bin, l1->bin_len, c->detail.title,
                                   sizeof(c->detail.title));
        c->detail.artwork_id = item_artwork(m, item_type);
        break;
    case DJ_LINK_DB_ITEM_ARTIST:
        dj_link_db_utf16be_to_utf8(l1->bin, l1->bin_len, c->detail.artist,
                                   sizeof(c->detail.artist));
        break;
    case DJ_LINK_DB_ITEM_DURATION:
        c->detail.duration_s = num > 0xffffu ? 0xffffu : (uint16_t)num;
        break;
    case DJ_LINK_DB_ITEM_TEMPO:
        c->detail.bpm100 = num > 0xffffu ? 0u : (uint16_t)num;
        break;
    default:
        break;
    }
}

static void finish_detail(dj_link_db_t *c, uint32_t now_ms)
{
    c->detail.has_detail = true;
    if (c->io.track) {
        c->io.track(c->io.ctx, c->detail_index, &c->detail, true);
    }
    go_ready(c, now_ms);
    start_next(c, now_ms);
}

/* The path request is answered (path may be ""), the session goes on. */
static void finish_path(dj_link_db_t *c, uint32_t now_ms)
{
    uint32_t id = c->path_id;
    c->path_id = 0u;
    go_ready(c, now_ms);
    if (c->io.path) {
        c->io.path(c->io.ctx, id, c->path);
    }
    start_next(c, now_ms);
}

/* The blob request is answered (len may be 0), the session goes on. */
static void finish_blob(dj_link_db_t *c, uint32_t now_ms)
{
    uint32_t id = c->blob_id;
    uint16_t request = c->blob_request;
    size_t len = c->blob_keep ? c->blob_len : 0u;
    bool answered = c->blob_typed;
    blob_reset(c);
    go_ready(c, now_ms);
    if (id != 0u && c->io.blob) {
        c->io.blob(c->io.ctx, id, request, len, answered);
    }
    start_next(c, now_ms);
}

static void consume(dj_link_db_t *c, size_t n)
{
    c->rx_len -= n;
    memmove(c->rx, &c->rx[n], c->rx_len);
}

/* Size of the integer field at buf: 0 = more bytes needed, -1 = not an
 * integer field. */
static int int_field_size(const uint8_t *buf, size_t len)
{
    if (len == 0u) {
        return 0;
    }
    int size = buf[0] == DJLINK_DB_FIELD_INT8  ? 2 :
               buf[0] == DJLINK_DB_FIELD_INT16 ? 3 :
               buf[0] == DJLINK_DB_FIELD_INT32 ? 5 : -1;
    return size < 0 || len >= (size_t)size ? size : 0;
}

/* v300: the head of an analysis reply, up to and including the header of
 * its blob argument (or the whole message when it has none). 1 = parsed,
 * 0 = more bytes needed, -1 = malformed. */
static int blob_head(dj_link_db_t *c, size_t *head_len, uint32_t *txid, uint16_t *type,
                     bool *has_blob, uint32_t *blob_len, uint8_t *tail)
{
    const uint8_t *b = c->rx;
    size_t len = c->rx_len;
    static const uint8_t lead[4] = { DJLINK_DB_FIELD_INT32, DJLINK_DB_FIELD_INT32,
                                     DJLINK_DB_FIELD_INT16, DJLINK_DB_FIELD_INT8 };
    size_t pos = 0u;
    uint32_t status = 0u;
    for (size_t i = 0; i < sizeof(lead); i++) {
        int n = int_field_size(&b[pos], len - pos);
        if (n == 0) {
            return 0;
        }
        if (n < 0 || b[pos] != lead[i]) {
            return -1;
        }
        pos += (size_t)n;
    }
    if (djlink_rd32(&b[1]) != DJLINK_DB_MAGIC) {
        return -1;
    }
    *txid = djlink_rd32(&b[6]);
    *type = djlink_rd16(&b[11]);
    uint8_t argc = b[14];
    if (len - pos < 5u) {
        return 0;
    }
    uint32_t tags_len = djlink_rd32(&b[pos + 1u]);
    if (b[pos] != DJLINK_DB_FIELD_BINARY || argc > DJLINK_DB_MAX_ARGS ||
        tags_len < argc || tags_len > 64u) {
        return -1;
    }
    if (len - pos < 5u + tags_len) {
        return 0;
    }
    const uint8_t *tags = &b[pos + 5u];
    pos += 5u + tags_len;
    for (uint8_t i = 0; i < argc; i++) {
        if (tags[i] == 0x03u && status != 0u) {
            /* vynull's "not found": the blob is declared, never sent. Its
             * empty cue list (v303) still puts a zero int32 in its place. */
            if (*type == DJLINK_DB_TYPE_CUES_EXT_REPLY) {
                int n = int_field_size(&b[pos], len - pos);
                if (n == 0) {
                    return 0;
                }
                if (n < 0) {
                    return -1;
                }
                pos += (size_t)n;
            }
            break;
        }
        if (tags[i] == 0x03u) { /* binary */
            if (len - pos < 5u) {
                return 0;
            }
            if (b[pos] != DJLINK_DB_FIELD_BINARY) {
                return -1;
            }
            *blob_len = djlink_rd32(&b[pos + 1u]);
            *has_blob = true;
            *tail = (uint8_t)(argc - i - 1u);
            *head_len = pos + 5u;
            return 1;
        }
        int n = int_field_size(&b[pos], len - pos);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            return -1; /* a string is no analysis reply */
        }
        if (i == 1u) {
            status = n == 5 ? djlink_rd32(&b[pos + 1u])
                   : n == 3 ? djlink_rd16(&b[pos + 1u]) : b[pos + 1u];
        }
        pos += (size_t)n;
    }
    *has_blob = false;
    *blob_len = 0u;
    *tail = 0u;
    *head_len = pos;
    return 1;
}

/* One step through an analysis reply: 1 = progress, 0 = more bytes needed
 * (or the session ended). */
static int blob_step(dj_link_db_t *c, uint32_t now_ms)
{
    if (!c->blob_body) {
        size_t head_len = 0u;
        uint32_t txid = 0u;
        uint16_t type = 0u;
        bool has_blob = false;
        uint32_t blob_len = 0u;
        uint8_t tail = 0u;
        int r = blob_head(c, &head_len, &txid, &type, &has_blob, &blob_len, &tail);
        if (r == 0) {
            return 0;
        }
        if (r < 0) {
            fail(c, "BAD REPLY");
            return 0;
        }
        if (txid != c->txid) {
            fail(c, "BAD TXID");
            return 0;
        }
        consume(c, head_len);
        c->blob_keep = type == (c->blob_request == DJLINK_DB_TYPE_WAVEFORM_REQUEST
                                    ? DJLINK_DB_TYPE_WAVEFORM_REPLY
                                    : c->blob_request == DJLINK_DB_TYPE_ARTWORK_REQUEST
                                    ? DJLINK_DB_TYPE_ARTWORK
                                    : c->blob_request == DJLINK_DB_TYPE_CUES_EXT_REQUEST
                                    ? DJLINK_DB_TYPE_CUES_EXT_REPLY
                                    : c->blob_request == DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST
                                    ? DJ_LINK_DB_TYPE_ANLZ_TAG_REPLY
                                    : DJLINK_DB_TYPE_BEATGRID_REPLY);
        c->blob_typed = c->blob_keep;
        if (blob_len > c->blob_cap) {
            c->blob_keep = c->blob_typed = false; /* never publish a clipped asset */
        }
        if (!has_blob) {
            /* "No data": three integers and nothing else. */
            c->blob_keep = false;
            finish_blob(c, now_ms);
            return 1;
        }
        c->blob_body = true;
        c->blob_left = blob_len;
        c->blob_tail = tail;
        return 1;
    }
    if (c->blob_left > 0u) {
        size_t n = c->rx_len < c->blob_left ? c->rx_len : c->blob_left;
        if (n == 0u) {
            return 0;
        }
        size_t room = c->blob_dst && c->blob_cap > c->blob_len ? c->blob_cap - c->blob_len : 0u;
        size_t keep = n < room ? n : room;
        if (c->blob_keep && keep > 0u) {
            memcpy(&c->blob_dst[c->blob_len], c->rx, keep);
            c->blob_len += keep;
        }
        c->blob_left -= (uint32_t)n;
        consume(c, n);
        return 1;
    }
    while (c->blob_tail > 0u) {
        int n = int_field_size(c->rx, c->rx_len);
        if (n == 0) {
            return 0;
        }
        if (n < 0) {
            fail(c, "BAD REPLY");
            return 0;
        }
        consume(c, (size_t)n);
        c->blob_tail--;
    }
    finish_blob(c, now_ms);
    return 1;
}

static void handle_msg(dj_link_db_t *c, const djlink_db_msg_t *m, uint32_t now_ms)
{
    if (c->phase == DJ_LINK_DB_SRV_WAIT_SETUP) {
        if (m->type != DJLINK_DB_TYPE_SUCCESS || m->txid != DJLINK_DB_SETUP_TXID) {
            fail(c, "SETUP REFUSED");
            return;
        }
        go_ready(c, now_ms);
        start_next(c, now_ms);
        return;
    }
    if (m->txid != c->txid) {
        fail(c, "BAD TXID");
        return;
    }
    switch (c->phase) {
    case DJ_LINK_DB_LIST_WAIT_AVAIL:
        if (!menu_success(m, c->menu == DJ_LINK_DB_MENU_ALL_TRACKS ?
                          DJ_LINK_DB_TYPE_TRACK_MENU : DJ_LINK_DB_TYPE_PLAYLIST_MENU)) {
            fail(c, "NO TRACK MENU");
            return;
        }
        c->list_total = menu_count(m);
        c->list_target = c->list_total < c->max_tracks ? c->list_total : c->max_tracks;
        if (c->io.list_begin) {
            c->io.list_begin(c->io.ctx, c->list_total);
        }
        if (c->list_target == 0) {
            c->list_done = true;
            go_ready(c, now_ms);
            start_next(c, now_ms);
            return;
        }
        request_render(c, 0, c->list_target < DJ_LINK_DB_RENDER_BATCH
                                 ? c->list_target : DJ_LINK_DB_RENDER_BATCH,
                       DJ_LINK_DB_LIST_WAIT_RENDER, now_ms);
        return;
    case DJ_LINK_DB_LIST_WAIT_RENDER:
        if (m->type == DJLINK_DB_TYPE_MENU_ITEM) {
            list_item(c, m);
        } else if (m->type == DJLINK_DB_TYPE_MENU_FOOTER) {
            if (c->render_seen != c->render_count) {
                fail(c, "INCOMPLETE MENU"); return;
            }
            uint32_t next = c->render_offset + c->render_count;
            if (next < c->list_target) {
                uint32_t left = c->list_target - next;
                request_render(c, next, left < DJ_LINK_DB_RENDER_BATCH
                                            ? left : DJ_LINK_DB_RENDER_BATCH,
                               DJ_LINK_DB_LIST_WAIT_RENDER, now_ms);
            } else {
                c->list_done = true;
                go_ready(c, now_ms);
                start_next(c, now_ms);
            }
        } else if (m->type != DJLINK_DB_TYPE_MENU_HEADER) {
            fail(c, "BAD RENDER REPLY");
        }
        return;
    case DJ_LINK_DB_DETAIL_WAIT_AVAIL:
        if (!menu_success(m, DJLINK_DB_TYPE_METADATA_REQUEST)) {
            fail(c, "NO METADATA");
            return;
        }
        c->detail_count = menu_count(m);
        if (c->detail_count > DJ_LINK_DB_RENDER_BATCH) {
            fail(c, "METADATA TOO LARGE"); return;
        }
        if (c->detail_count == 0) {
            finish_detail(c, now_ms);
            return;
        }
        request_render(c, 0, c->detail_count < DJ_LINK_DB_RENDER_BATCH
                                 ? c->detail_count : DJ_LINK_DB_RENDER_BATCH,
                       DJ_LINK_DB_DETAIL_WAIT_RENDER, now_ms);
        return;
    case DJ_LINK_DB_DETAIL_WAIT_RENDER:
        if (m->type == DJLINK_DB_TYPE_MENU_ITEM) {
            if (c->render_seen >= c->render_count) {
                fail(c, "EXCESS METADATA ITEM"); return;
            }
            detail_item(c, m);
            c->render_seen++;
        } else if (m->type == DJLINK_DB_TYPE_MENU_FOOTER) {
            if (c->render_seen != c->render_count) {
                fail(c, "INCOMPLETE METADATA"); return;
            }
            finish_detail(c, now_ms);
        } else if (m->type != DJLINK_DB_TYPE_MENU_HEADER) {
            fail(c, "BAD RENDER REPLY");
        }
        return;
    case DJ_LINK_DB_PATH_WAIT_AVAIL:
        if (m->type == DJLINK_DB_TYPE_SUCCESS && !menu_success(m, DJ_LINK_DB_TYPE_TRACK_INFO)) {
            fail(c, "BAD PATH REPLY"); return;
        }
        /* An unknown id is an answer, not a broken session. */
        if (m->type != DJLINK_DB_TYPE_SUCCESS || menu_count(m) == 0u) {
            finish_path(c, now_ms);
            return;
        }
        request_render(c, 0, menu_count(m) < DJ_LINK_DB_RENDER_BATCH
                                 ? menu_count(m) : DJ_LINK_DB_RENDER_BATCH,
                       DJ_LINK_DB_PATH_WAIT_RENDER, now_ms);
        return;
    case DJ_LINK_DB_PATH_WAIT_RENDER:
        if (m->type == DJLINK_DB_TYPE_MENU_ITEM) {
            uint32_t num;
            uint32_t item_type;
            const djlink_db_arg_t *l1;
            const djlink_db_arg_t *l2;
            if (item_fields(m, &num, &l1, &l2, &item_type) &&
                item_type == DJ_LINK_DB_ITEM_FILE_PATH) {
                (void)dj_link_db_decode_path(l1->bin,l1->bin_len,c->path,sizeof(c->path));
            }
        } else if (m->type == DJLINK_DB_TYPE_MENU_FOOTER) {
            finish_path(c, now_ms);
        } else if (m->type != DJLINK_DB_TYPE_MENU_HEADER) {
            fail(c, "BAD RENDER REPLY");
        }
        return;
    default:
        return;
    }
}

static void process(dj_link_db_t *c, uint32_t now_ms)
{
    for (;;) {
        switch (c->phase) {
        case DJ_LINK_DB_DISC_WAIT_PORT: {
            uint16_t port = 0;
            if (c->rx_len < 2u) {
                return;
            }
            (void)djlink_db_port_query_reply_parse(c->rx, c->rx_len, &port);
            close_connection(c); /* the discovery connection is one-shot */
            if (port == 0u || port == 0xffffu) {
                c->phase = DJ_LINK_DB_IDLE;
                fail(c, "NO DB SERVER");
                return;
            }
            connect_to(c, port, DJ_LINK_DB_SRV_CONNECTING, now_ms);
            return;
        }
        case DJ_LINK_DB_SRV_WAIT_GREETING: {
            uint8_t greeting[DJLINK_DB_SETUP_LEN];
            if (c->rx_len < DJLINK_DB_SETUP_LEN) {
                return;
            }
            (void)djlink_db_setup_build(greeting, sizeof(greeting));
            if (memcmp(c->rx, greeting, sizeof(greeting)) != 0) {
                fail(c, "BAD GREETING");
                return;
            }
            c->rx_len -= DJLINK_DB_SETUP_LEN;
            memmove(c->rx, &c->rx[DJLINK_DB_SETUP_LEN], c->rx_len);
            send_tx(c, djlink_db_context_setup_build(c->our_number, c->tx, sizeof(c->tx)),
                    DJ_LINK_DB_SRV_WAIT_SETUP, now_ms);
            continue;
        }
        case DJ_LINK_DB_SRV_WAIT_SETUP:
        case DJ_LINK_DB_LIST_WAIT_AVAIL:
        case DJ_LINK_DB_LIST_WAIT_RENDER:
        case DJ_LINK_DB_DETAIL_WAIT_AVAIL:
        case DJ_LINK_DB_DETAIL_WAIT_RENDER:
        case DJ_LINK_DB_PATH_WAIT_AVAIL:
        case DJ_LINK_DB_PATH_WAIT_RENDER: {
            int n = dj_link_db_msg_size(c->rx, c->rx_len);
            if (n == 0) {
                return;
            }
            if (n < 0) {
                fail(c, "BAD REPLY");
                return;
            }
            djlink_db_msg_t m;
            (void)djlink_db_msg_parse(c->rx, (size_t)n, &m);
            handle_msg(c, &m, now_ms);
            if (!has_connection(c)) {
                return;
            }
            c->rx_len -= (size_t)n;
            memmove(c->rx, &c->rx[n], c->rx_len);
            /* Progress (a streamed item or a new request) restarts the clock. */
            c->deadline_ms = now_ms + c->step_timeout_ms;
            continue;
        }
        case DJ_LINK_DB_BLOB_WAIT:
            if (blob_step(c, now_ms) == 0) {
                return;
            }
            if (has_connection(c)) {
                c->deadline_ms = now_ms + c->step_timeout_ms;
            }
            continue;
        default:
            c->rx_len = 0; /* unsolicited bytes while idle */
            return;
        }
    }
}

void dj_link_db_init(dj_link_db_t *c, const dj_link_db_io_t *io, uint32_t max_tracks)
{
    memset(c, 0, sizeof(*c));
    if (io) {
        c->io = *io;
    }
    c->discovery_port = DJLINK_DBSERVER_DISCOVERY_PORT;
    c->step_timeout_ms = DJ_LINK_DB_STEP_TIMEOUT_MS;
    c->max_tracks = max_tracks < DJ_LINK_DB_TRACK_LIMIT ? max_tracks : DJ_LINK_DB_TRACK_LIMIT;
    c->connection_epoch = 1;
}

void dj_link_db_start(dj_link_db_t *c, uint32_t peer_ip, uint8_t peer_number,
                      uint8_t slot, uint8_t our_number, uint64_t source_epoch, uint32_t now_ms)
{
    close_connection(c);
    c->peer_ip = peer_ip;
    c->peer_number = peer_number;
    c->slot = slot;
    c->our_number = our_number;
    c->source_epoch = source_epoch;
    c->list_done = false;
    c->path_id = 0u;
    blob_reset(c);
    c->list_total = 0;
    c->list_target = 0;
    c->error[0] = '\0';
    c->phase = DJ_LINK_DB_IDLE;
    if (!source_epoch || peer_ip == 0u || our_number == 0u || our_number > 6u ||
        peer_number == 0u || peer_number == our_number) {
        fail(c, our_number == 0u ? "NOT JOINED" : "BAD PEER");
        return;
    }
    connect_to(c, c->discovery_port, DJ_LINK_DB_DISC_CONNECTING, now_ms);
}

void dj_link_db_set_sort(dj_link_db_t *c, uint8_t sort)
{
    if (c) {
        c->sort = sort;
    }
}

void dj_link_db_set_menu(dj_link_db_t *c, dj_link_db_menu_t menu, uint32_t id)
{
    if (c) {
        c->menu = (uint8_t)menu;
        c->menu_id = menu == DJ_LINK_DB_MENU_ALL_TRACKS ? 0u : id;
    }
}

uint32_t dj_link_db_row_index(uint32_t rows, bool descending, uint32_t index)
{
    return descending && index < rows ? rows - 1u - index : index;
}

uint8_t dj_link_db_next_sort(uint8_t sort, bool descending, uint8_t tapped,
                             bool *next_descending)
{
    bool desc = false;
    uint8_t next = tapped;
    if (tapped != DJ_LINK_DB_SORT_DEFAULT && sort == tapped) {
        if (descending) {
            next = DJ_LINK_DB_SORT_DEFAULT;
        } else {
            desc = true;
        }
    }
    if (next_descending) {
        *next_descending = desc;
    }
    return next;
}

void dj_link_db_stop(dj_link_db_t *c)
{
    close_connection(c);
    c->phase = DJ_LINK_DB_IDLE;
    c->peer_number = 0;
    c->source_epoch = 0;
    c->list_done = false;
    c->path_id = 0u;
    blob_reset(c);
}

bool dj_link_db_want_path(dj_link_db_t *c, uint32_t rekordbox_id, uint32_t now_ms)
{
    if (!c || rekordbox_id == 0u || c->peer_number == 0u || c->phase == DJ_LINK_DB_FAILED) {
        return false;
    }
    c->path_id = rekordbox_id;
    start_next(c, now_ms);
    return true;
}

bool dj_link_db_want_blob(dj_link_db_t *c, uint16_t request, uint32_t rekordbox_id,
                          uint8_t *dst, size_t cap, uint32_t now_ms)
{
    if (!c || rekordbox_id == 0u || !dst || cap == 0u || c->peer_number == 0u ||
        c->phase == DJ_LINK_DB_FAILED || c->blob_request != 0u ||
        (request != DJLINK_DB_TYPE_WAVEFORM_REQUEST &&
         request != DJLINK_DB_TYPE_BEATGRID_REQUEST &&
         request != DJLINK_DB_TYPE_CUES_EXT_REQUEST &&
         request != DJ_LINK_DB_TYPE_ANLZ_TAG_REQUEST &&
         request != DJLINK_DB_TYPE_ARTWORK_REQUEST)) {
        return false;
    }
    c->blob_request = request;
    c->blob_id = rekordbox_id;
    c->blob_dst = dst;
    c->blob_cap = cap;
    c->blob_len = 0u;
    c->blob_keep = false;
    c->blob_typed = false;
    start_next(c, now_ms);
    return true;
}

void dj_link_db_cancel_blob(dj_link_db_t *c)
{
    if (!c || c->blob_request == 0u) {
        return;
    }
    if (c->phase == DJ_LINK_DB_BLOB_WAIT) {
        /* In flight: drain the reply, answer nobody. */
        c->blob_id = 0u;
        c->blob_dst = NULL;
        c->blob_cap = 0u;
        c->blob_keep = false;
    } else {
        blob_reset(c);
    }
}

void dj_link_db_on_connected(dj_link_db_t *c, uint64_t connection_epoch, uint32_t now_ms)
{
    if (!c || connection_epoch != c->connection_epoch) return;
    if (c->phase == DJ_LINK_DB_DISC_CONNECTING) {
        send_tx(c, djlink_db_port_query_build(c->tx, sizeof(c->tx)),
                DJ_LINK_DB_DISC_WAIT_PORT, now_ms);
    } else if (c->phase == DJ_LINK_DB_SRV_CONNECTING) {
        send_tx(c, djlink_db_setup_build(c->tx, sizeof(c->tx)),
                DJ_LINK_DB_SRV_WAIT_GREETING, now_ms);
    }
}

void dj_link_db_on_data(dj_link_db_t *c, uint64_t connection_epoch,
                        const uint8_t *buf, size_t len, uint32_t now_ms)
{
    if (!c || connection_epoch != c->connection_epoch) return;
    if (!has_connection(c) || !buf || len == 0) {
        return;
    }
    /* v300: in pieces, so a streamed analysis blob larger than rx passes. */
    while (len > 0u) {
        size_t room = sizeof(c->rx) - c->rx_len;
        if (room == 0u) {
            fail(c, "REPLY TOO LARGE");
            return;
        }
        size_t n = len < room ? len : room;
        memcpy(&c->rx[c->rx_len], buf, n);
        c->rx_len += n;
        buf += n;
        len -= n;
        process(c, now_ms);
        if (!has_connection(c)) {
            return;
        }
    }
}

void dj_link_db_on_closed(dj_link_db_t *c, uint64_t connection_epoch, uint32_t now_ms)
{
    if (!c || connection_epoch != c->connection_epoch) return;
    (void)now_ms;
    if (c->phase == DJ_LINK_DB_READY) {
        c->phase = DJ_LINK_DB_IDLE; /* idle session dropped: reconnect on demand */
        c->rx_len = 0;
    } else if (has_connection(c)) {
        fail(c, "CONNECTION LOST");
    }
}

void dj_link_db_poll(dj_link_db_t *c, uint32_t now_ms)
{
    uint32_t index;
    uint32_t id;
    switch (c->phase) {
    case DJ_LINK_DB_IDLE:
        /* Reconnect only when the owner wants a path or metadata for a
         * listed row. */
        if (c->peer_number != 0u && c->list_done &&
            (c->path_id != 0u || (c->blob_request != 0u && c->blob_id != 0u) ||
             (c->io.next_detail && c->io.next_detail(c->io.ctx, &index, &id)))) {
            connect_to(c, c->discovery_port, DJ_LINK_DB_DISC_CONNECTING, now_ms);
        }
        return;
    case DJ_LINK_DB_FAILED:
        return;
    case DJ_LINK_DB_READY:
        start_next(c, now_ms);
        if (c->phase == DJ_LINK_DB_READY &&
            (uint32_t)(now_ms - c->idle_since_ms) >= DJ_LINK_DB_IDLE_CLOSE_MS) {
            close_connection(c);
            c->phase = DJ_LINK_DB_IDLE;
        }
        return;
    default:
        if ((int32_t)(now_ms - c->deadline_ms) >= 0) {
            char reason[sizeof(c->error)];
            snprintf(reason, sizeof(reason), "TIMEOUT %s", phase_name(c->phase));
            fail(c, reason);
        }
        return;
    }
}

dj_link_db_phase_t dj_link_db_phase(const dj_link_db_t *c)
{
    return c->phase;
}

bool dj_link_db_list_done(const dj_link_db_t *c)
{
    return c->list_done;
}

const char *dj_link_db_error(const dj_link_db_t *c)
{
    return c->error;
}

dj_link_peer_load_t dj_link_peer_load_check(const dj_link_peer_track_t *t,
                                            char *reason, size_t cap)
{
    dj_link_peer_load_t verdict = DJ_LINK_PEER_LOAD_METADATA_ONLY;
    const char *text = "metadata only: peer audio not reachable";
    if (!t || t->rekordbox_id == 0u) {
        verdict = DJ_LINK_PEER_LOAD_NO_TRACK;
        text = "no track";
    } else if (t->audio == DJ_LINK_PEER_AUDIO_NFS) {
        verdict = DJ_LINK_PEER_LOAD_OK;
        text = "NFS download to the SD cache";
    }
    if (reason && cap) {
        snprintf(reason, cap, "%s", text);
    }
    return verdict;
}
