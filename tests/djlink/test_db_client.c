#include "dj_link_db.h"
#include "djlink/dbserver.h"
#include "djlink/status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    unsigned connects, closes, sends, rows;
    uint16_t port;
    uint32_t total, last_index;
    uint8_t sent[DJ_LINK_DB_TX_MAX];
    size_t sent_len;
    dj_link_peer_track_t last_row;
    bool want_detail;
    unsigned detail_rows;
    unsigned blobs;
    size_t blob_len;
    bool blob_answered;
} mock_t;
static void blob(void *ctx,uint32_t id,uint16_t request,size_t len,bool answered)
{
    mock_t *m=ctx;assert(id==7 && request==DJLINK_DB_TYPE_BEATGRID_REQUEST);
    m->blobs++;m->blob_len=len;m->blob_answered=answered;
}
static int connect_mock(void *ctx, uint32_t ip, uint16_t port)
{
    mock_t *m = ctx; assert(ip == 0xc0a80102); ++m->connects; m->port = port; return 0;
}
static int send_mock(void *ctx, const uint8_t *buf, size_t len)
{
    mock_t *m = ctx; assert(len <= sizeof(m->sent));
    memcpy(m->sent, buf, len); m->sent_len = len; ++m->sends; return 0;
}
static void close_mock(void *ctx) { ++((mock_t *)ctx)->closes; }
static void list_begin(void *ctx, uint32_t total) { ((mock_t *)ctx)->total = total; }
static void row(void *ctx, uint32_t index, const dj_link_peer_track_t *t, bool detail)
{
    mock_t *m = ctx;
    if (detail) {
        assert(index == 0); m->last_row = *t; m->want_detail = false; ++m->detail_rows; return;
    }
    assert(index == m->rows);
    assert(t->audio == DJ_LINK_PEER_AUDIO_METADATA_ONLY);
    ++m->rows; m->last_index = index; m->last_row = *t;
}
static void feed(dj_link_db_t *c, const uint8_t *buf, size_t len, unsigned fragment)
{
    uint64_t epoch = c->connection_epoch;
    while (len) {
        size_t n = fragment && len > fragment ? fragment : len;
        dj_link_db_on_data(c, epoch, buf, n, 100);
        buf += n; len -= n;
    }
}
static void reply(dj_link_db_t *c, uint32_t tid, uint16_t type,
                   djlink_db_arg_t *args, uint8_t argc, unsigned fragment)
{
    uint8_t buf[1024];
    int n = djlink_db_msg_build(tid, type, args, argc, buf, sizeof(buf)); assert(n > 0);
    feed(c, buf, (size_t)n, fragment);
}
static void init(dj_link_db_t *c, mock_t *m, uint32_t limit)
{
    memset(m, 0, sizeof(*m));
    dj_link_db_io_t io = {.connect=connect_mock, .send=send_mock, .close=close_mock,
        .list_begin=list_begin, .track=row, .blob=blob, .ctx=m};
    dj_link_db_init(c, &io, limit);
}
static bool next_detail(void *ctx, uint32_t *index, uint32_t *id)
{
    mock_t *m = ctx;
    if (!m->want_detail) return false;
    *index = 0; *id = 1; return true;
}
static void join(dj_link_db_t *c, mock_t *m)
{
    dj_link_db_start(c, 0xc0a80102, 2, DJLINK_SLOT_USB, 4, 99, 0);
    assert(m->port == DJLINK_DBSERVER_DISCOVERY_PORT && c->source_epoch == 99);
    uint64_t old = c->connection_epoch;
    dj_link_db_on_connected(c, old, 1);
    assert(c->phase == DJ_LINK_DB_DISC_WAIT_PORT);
    uint8_t port[] = {0x30,0x39}; feed(c, port, sizeof(port), 1);
    assert(c->phase == DJ_LINK_DB_SRV_CONNECTING && m->port == 12345 && m->closes == 1);
    unsigned sends = m->sends;
    uint8_t bad[] = {0xff,0xff};
    dj_link_db_on_data(c, old, bad, sizeof(bad), 5);
    dj_link_db_on_closed(c, old, 5); dj_link_db_on_connected(c, old, 5);
    assert(c->phase == DJ_LINK_DB_SRV_CONNECTING && m->sends == sends);
    dj_link_db_on_connected(c, c->connection_epoch, 6);
    uint8_t greeting[5]; assert(djlink_db_setup_build(greeting, sizeof(greeting)) == 5);
    feed(c, greeting, sizeof(greeting), 1);
    assert(c->phase == DJ_LINK_DB_SRV_WAIT_SETUP);
    reply(c, DJLINK_DB_SETUP_TXID, DJLINK_DB_TYPE_SUCCESS, NULL, 0, 1);
    assert(c->phase == DJ_LINK_DB_LIST_WAIT_AVAIL);
}
static void availability(dj_link_db_t *c, uint32_t count)
{
    djlink_db_arg_t args[2] = {{.type=DJLINK_DB_FIELD_INT32,
        .num=c->menu == DJ_LINK_DB_MENU_ALL_TRACKS ? DJ_LINK_DB_TYPE_TRACK_MENU : DJ_LINK_DB_TYPE_PLAYLIST_MENU},
        {.type=DJLINK_DB_FIELD_INT32, .num=count}};
    reply(c, c->txid, DJLINK_DB_TYPE_SUCCESS, args, 2, 1);
}
static void item(dj_link_db_t *c, uint32_t id, uint32_t kind, unsigned fragment)
{
    static const uint8_t title[] = {0,'T',0,0xe9,0,'s',0,'t'};
    static const uint8_t artist[] = {0,'A'};
    djlink_db_arg_t args[7] = {0};
    for (unsigned i = 0; i < 7; ++i) args[i].type = DJLINK_DB_FIELD_INT32;
    args[1].num = id; args[6].num = kind;
    args[3].type = args[5].type = DJLINK_DB_FIELD_STRING;
    args[3].bin = title; args[3].bin_len = sizeof(title);
    args[5].bin = artist; args[5].bin_len = sizeof(artist);
    reply(c, c->txid, DJLINK_DB_TYPE_MENU_ITEM, args, 7, fragment);
}
static void bounded_list_and_playlist(void)
{
    dj_link_db_t c; mock_t m; init(&c, &m, UINT32_MAX); join(&c, &m);
    availability(&c, 2023);
    assert(m.total == 2023 && c.list_target == 2000 && c.max_tracks == 2000);
    while (!c.list_done) {
        assert(c.phase == DJ_LINK_DB_LIST_WAIT_RENDER && c.render_count <= 64);
        uint32_t count = c.render_count;
        for (uint32_t i = 0; i < count; ++i)
            item(&c, m.rows+1, DJ_LINK_DB_ITEM_TITLE_ARTIST, m.rows < 3 ? 1 : 0);
        reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    }
    assert(m.rows == 2000 && m.last_index == 1999 && c.phase == DJ_LINK_DB_READY);
    assert(!strcmp(m.last_row.title, "T\xc3\xa9st") && !strcmp(m.last_row.artist, "A"));
    dj_link_db_stop(&c); assert(c.source_epoch == 0);
    init(&c, &m, 20);
    dj_link_db_set_menu(&c, DJ_LINK_DB_MENU_PLAYLIST, 71);
    dj_link_db_set_sort(&c, DJ_LINK_DB_SORT_TITLE);
    join(&c, &m);
    djlink_db_msg_t sent;
    assert(djlink_db_msg_parse(m.sent, m.sent_len, &sent) == DJLINK_OK);
    assert(sent.type == DJ_LINK_DB_TYPE_PLAYLIST_MENU && sent.args[1].num == 0 && sent.args[2].num == 71);
    availability(&c, 1); item(&c, 900, DJ_LINK_DB_ITEM_TITLE, 1);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    assert(m.last_row.rekordbox_id == 900 && c.list_done);
    init(&c, &m, 20); dj_link_db_set_menu(&c, DJ_LINK_DB_MENU_FOLDER, 0); join(&c, &m);
    availability(&c, 2); item(&c, 10, DJ_LINK_DB_ITEM_FOLDER, 1);
    assert(m.last_row.kind == DJ_LINK_PEER_ROW_FOLDER && m.last_row.has_detail);
    item(&c, 11, DJ_LINK_DB_ITEM_PLAYLIST, 1);
    assert(m.last_row.kind == DJ_LINK_PEER_ROW_PLAYLIST);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1); assert(c.list_done);
}
static void failures_and_stale_events(void)
{
    dj_link_db_t c; mock_t m; init(&c, &m, 8); join(&c, &m);
    availability(&c, 2);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    assert(c.phase == DJ_LINK_DB_FAILED && !strcmp(c.error, "INCOMPLETE MENU"));
    init(&c, &m, 8); join(&c, &m);
    reply(&c, c.txid+1, DJLINK_DB_TYPE_SUCCESS, NULL, 0, 1);
    assert(c.phase == DJ_LINK_DB_FAILED && !strcmp(c.error, "BAD TXID"));
    init(&c, &m, 8); join(&c, &m);
    djlink_db_arg_t bad[] = {{.type=DJLINK_DB_FIELD_INT32, .num=0x1004}, {.type=DJLINK_DB_FIELD_BINARY}};
    reply(&c, c.txid, DJLINK_DB_TYPE_SUCCESS, bad, 2, 1);
    assert(c.phase == DJ_LINK_DB_FAILED);
    init(&c, &m, 8); join(&c, &m); availability(&c, 1);
    uint64_t old = c.connection_epoch;
    dj_link_db_stop(&c); unsigned callbacks = m.rows;
    uint8_t junk[512] = {0}; dj_link_db_on_data(&c, old, junk, sizeof(junk), 900);
    assert(c.phase == DJ_LINK_DB_IDLE && m.rows == callbacks);
    dj_link_db_start(&c, 0xc0a80102, 2, DJLINK_SLOT_USB, 4, 100, 900);
    assert(c.source_epoch == 100 && c.connection_epoch != old);
    dj_link_db_on_connected(&c, old, 900); dj_link_db_on_closed(&c, old, 900);
    assert(c.phase == DJ_LINK_DB_DISC_CONNECTING);
    dj_link_db_poll(&c, 3900); assert(c.phase == DJ_LINK_DB_FAILED && strstr(c.error, "TIMEOUT"));
    init(&c, &m, 8); join(&c, &m);
    uint8_t invalid[4096]; memset(invalid, 0xff, sizeof(invalid)); feed(&c, invalid, sizeof(invalid), 0);
    assert(c.phase == DJ_LINK_DB_FAILED);
}
static void framing_and_visible_details(void)
{
    uint8_t buf[128]; int n = djlink_db_msg_build(1, 0x4000, NULL, 0, buf, sizeof(buf)); assert(n > 0);
    for (int cut = 0; cut < n; ++cut) assert(dj_link_db_msg_size(buf, (size_t)cut) == 0);
    memcpy(buf+n, buf, (size_t)n); assert(dj_link_db_msg_size(buf, (size_t)n*2) == n);
    dj_link_peer_track_t rows[4] = {{.rekordbox_id=1}, {.rekordbox_id=2}, {.rekordbox_id=3}, {.rekordbox_id=4}};
    uint32_t index;
    assert(dj_link_db_pick_detail(rows, 4, 2, 2, 0, 0, &index) && index == 2);
    rows[2].has_detail = rows[3].has_detail = true;
    assert(!dj_link_db_pick_detail(rows, 4, 2, 2, 0, 0, &index));
    assert(!dj_link_db_pick_detail(rows, 4, UINT32_MAX, 10, 0, 0, &index));
    char out[5]; uint8_t utf16[] = {0,'A',0x01,0x07,0,'B'};
    dj_link_db_utf16be_to_utf8(utf16, sizeof(utf16), out, sizeof(out));
    assert(!strcmp(out, "A\xc4\x87" "B"));
    dj_link_db_utf16be_to_utf8(utf16, sizeof(utf16)-1, out, 2); assert(!strcmp(out, "A"));
    uint8_t bad_surrogate[] = {0xd8,0,0,'B'};
    dj_link_db_utf16be_to_utf8(bad_surrogate, sizeof(bad_surrogate), out, sizeof(out));
    assert(!strcmp(out, "?B"));
    assert(dj_link_db_decode_path(utf16,sizeof(utf16),out,sizeof(out)));
    assert(!dj_link_db_decode_path(utf16,sizeof(utf16),out,2) && !out[0]);
    assert(!dj_link_db_decode_path(bad_surrogate,sizeof(bad_surrogate),out,sizeof(out)) && !out[0]);
    uint8_t pair[]={0xd8,0x3d,0xde,0x00};
    assert(dj_link_db_decode_path(pair,sizeof(pair),out,sizeof(out)) && !strcmp(out,"\xf0\x9f\x98\x80"));
}
static void bounded_assets(void)
{
    dj_link_db_t c;mock_t m;init(&c,&m,8);join(&c,&m);availability(&c,0);
    uint8_t payload[]={1,2,3,4},out[4]={0};
    djlink_db_arg_t args[3]={{.type=DJLINK_DB_FIELD_INT32,.num=DJLINK_DB_TYPE_BEATGRID_REQUEST},
        {.type=DJLINK_DB_FIELD_INT32,.num=0},
        {.type=DJLINK_DB_FIELD_BINARY,.bin=payload,.bin_len=sizeof(payload)}};
    assert(dj_link_db_want_blob(&c,DJLINK_DB_TYPE_BEATGRID_REQUEST,7,out,2,200));
    reply(&c,c.txid,DJLINK_DB_TYPE_BEATGRID_REPLY,args,3,1);
    assert(m.blobs==1 && !m.blob_answered && !m.blob_len && !out[0]);
    assert(dj_link_db_want_blob(&c,DJLINK_DB_TYPE_BEATGRID_REQUEST,7,out,sizeof(out),300));
    reply(&c,c.txid,DJLINK_DB_TYPE_BEATGRID_REPLY,args,3,1);
    assert(m.blobs==2 && m.blob_answered && m.blob_len==4 && !memcmp(out,payload,4));
}
static void metadata_completion(void)
{
    dj_link_db_t c; mock_t m; init(&c, &m, 8); join(&c, &m); availability(&c, 1);
    item(&c, 1, DJ_LINK_DB_ITEM_TITLE, 1);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    c.io.next_detail = next_detail; m.want_detail = true;
    dj_link_db_poll(&c, 200);
    assert(c.phase == DJ_LINK_DB_DETAIL_WAIT_AVAIL);
    djlink_db_arg_t args[2] = {{.type=DJLINK_DB_FIELD_INT32, .num=DJLINK_DB_TYPE_METADATA_REQUEST},
        {.type=DJLINK_DB_FIELD_INT32, .num=4}};
    reply(&c, c.txid, DJLINK_DB_TYPE_SUCCESS, args, 2, 1);
    item(&c, 1, DJ_LINK_DB_ITEM_TITLE, 1); item(&c, 1, DJ_LINK_DB_ITEM_ARTIST, 1);
    item(&c, 202, DJ_LINK_DB_ITEM_DURATION, 1); item(&c, 12875, DJ_LINK_DB_ITEM_TEMPO, 1);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    assert(m.detail_rows == 1 && m.last_row.has_detail && c.phase == DJ_LINK_DB_READY);
    assert(m.last_row.duration_s == 202 && m.last_row.bpm100 == 12875);
    m.want_detail = true; dj_link_db_poll(&c, 300); args[1].num = 2;
    reply(&c, c.txid, DJLINK_DB_TYPE_SUCCESS, args, 2, 1);
    reply(&c, c.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0, 1);
    assert(c.phase == DJ_LINK_DB_FAILED && m.detail_rows == 1);
}
int main(void)
{
    bounded_list_and_playlist(); failures_and_stale_events(); framing_and_visible_details();
    metadata_completion();bounded_assets();
    puts("PASS DB client fragments, 2000-row cap, playlists, malformed replies, cancel/epochs, timeout");
    return 0;
}
