#define _DEFAULT_SOURCE 1
#include "dj_link_db.h"
#include "djlink/dbserver.h"
#include "djlink/status.h"
#include <arpa/inet.h>
#include <assert.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

typedef enum { NORMAL, BAD_MAGIC, DISCONNECT, SILENT } mock_db_mode_t;
typedef struct { int discovery, database; uint16_t db_port; mock_db_mode_t mode; } peer_t;
typedef struct {
    dj_link_db_t client;
    int fd;
    bool connected;
    uint64_t connection_epoch;
    unsigned rows, reported;
} owner_t;
static uint32_t clock_ms(void)
{
    struct timespec t; assert(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (uint32_t)((uint64_t)t.tv_sec*1000 + t.tv_nsec/1000000);
}
static void timeout(int fd)
{
    struct timeval t = {.tv_sec=1, .tv_usec=0};
    assert(setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &t, sizeof(t)) == 0);
    assert(setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &t, sizeof(t)) == 0);
}
static int listen_local(uint16_t *port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0); assert(fd >= 0); timeout(fd);
    struct sockaddr_in addr = {.sin_family=AF_INET, .sin_addr={.s_addr=htonl(INADDR_LOOPBACK)}};
    assert(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) == 0);
    assert(listen(fd, 1) == 0);
    socklen_t len = sizeof(addr); assert(getsockname(fd, (struct sockaddr *)&addr, &len) == 0);
    *port = ntohs(addr.sin_port); return fd;
}
static void send_all(int fd, const uint8_t *buf, size_t len, bool split)
{
    while (len) {
        size_t count = split ? 1 : len;
        int n = send(fd, buf, count, MSG_NOSIGNAL); assert(n > 0);
        buf += n; len -= (size_t)n;
    }
}
static void recv_exact(int fd, uint8_t *buf, size_t len)
{
    while (len) { int n = recv(fd, buf, len, 0); assert(n > 0); buf += n; len -= (size_t)n; }
}
static djlink_db_msg_t recv_message(int fd, uint8_t *buf, size_t cap)
{
    size_t len = 0;
    for (;;) {
        assert(len < cap); recv_exact(fd, buf+len++, 1);
        int n = dj_link_db_msg_size(buf, len); assert(n >= 0);
        if (n) {
            djlink_db_msg_t m; assert(djlink_db_msg_parse(buf, len, &m) == DJLINK_OK); return m;
        }
    }
}
static void message(int fd, uint32_t tid, uint16_t type, djlink_db_arg_t *args, uint8_t argc)
{
    uint8_t buf[512]; int n = djlink_db_msg_build(tid, type, args, argc, buf, sizeof(buf));
    assert(n > 0); send_all(fd, buf, (size_t)n, true);
}
static void *peer_worker(void *context)
{
    peer_t *peer = context;
    int fd = accept(peer->discovery, NULL, NULL); assert(fd >= 0); timeout(fd);
    uint8_t query[32], expected[32]; int query_len = djlink_db_port_query_build(expected, sizeof(expected));
    assert(query_len > 0); recv_exact(fd, query, (size_t)query_len);
    assert(!memcmp(query, expected, (size_t)query_len));
    if (peer->mode == SILENT) { usleep(250000); close(fd); return NULL; }
    uint8_t port[] = {(uint8_t)(peer->db_port >> 8), (uint8_t)peer->db_port};
    send_all(fd, port, sizeof(port), true); close(fd);
    fd = accept(peer->database, NULL, NULL); assert(fd >= 0); timeout(fd);
    if (peer->mode == DISCONNECT) { close(fd); return NULL; }
    uint8_t greeting[DJLINK_DB_SETUP_LEN]; recv_exact(fd, greeting, sizeof(greeting));
    send_all(fd, greeting, sizeof(greeting), true);
    uint8_t buf[512]; djlink_db_msg_t request = recv_message(fd, buf, sizeof(buf));
    assert(request.type == DJLINK_DB_TYPE_SETUP && request.args[0].num == 4);
    message(fd, DJLINK_DB_SETUP_TXID, DJLINK_DB_TYPE_SUCCESS, NULL, 0);
    request = recv_message(fd, buf, sizeof(buf)); assert(request.type == DJ_LINK_DB_TYPE_TRACK_MENU);
    if (peer->mode == BAD_MAGIC) {
        uint8_t bad[32] = {0}; send_all(fd, bad, sizeof(bad), false); close(fd); return NULL;
    }
    djlink_db_arg_t available[2] = {{.type=DJLINK_DB_FIELD_INT32, .num=DJ_LINK_DB_TYPE_TRACK_MENU},
        {.type=DJLINK_DB_FIELD_INT32, .num=2}};
    message(fd, request.txid, DJLINK_DB_TYPE_SUCCESS, available, 2);
    request = recv_message(fd, buf, sizeof(buf)); assert(request.type == DJLINK_DB_TYPE_RENDER);
    message(fd, request.txid, DJLINK_DB_TYPE_MENU_HEADER, NULL, 0);
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t title[] = {0,'T',0,(uint8_t)('1'+i)};
        djlink_db_arg_t args[7] = {0};
        for (unsigned a = 0; a < 7; ++a) args[a].type = DJLINK_DB_FIELD_INT32;
        args[1].num = 100+i; args[6].num = DJ_LINK_DB_ITEM_TITLE;
        args[3].type = args[5].type = DJLINK_DB_FIELD_STRING;
        args[3].bin = title; args[3].bin_len = sizeof(title);
        message(fd, request.txid, DJLINK_DB_TYPE_MENU_ITEM, args, 7);
    }
    message(fd, request.txid, DJLINK_DB_TYPE_MENU_FOOTER, NULL, 0);
    close(fd); return NULL;
}
static int connect_owner(void *context, uint32_t ip, uint16_t port)
{
    owner_t *o = context; assert(o->fd == -1);
    o->fd = socket(AF_INET, SOCK_STREAM, 0); assert(o->fd >= 0); timeout(o->fd);
    struct sockaddr_in address = {.sin_family=AF_INET, .sin_port=htons(port), .sin_addr={.s_addr=htonl(ip)}};
    assert(connect(o->fd, (struct sockaddr *)&address, sizeof(address)) == 0);
    o->connection_epoch = o->client.connection_epoch;
    o->connected = true; return 0;
}
static int send_owner(void *context, const uint8_t *buf, size_t len)
{ send_all(((owner_t *)context)->fd, buf, len, false); return 0; }
static void close_owner(void *context)
{
    owner_t *o = context; if (o->fd >= 0) close(o->fd); o->fd = -1; o->connected = false;
}
static void begin_owner(void *context, uint32_t count) { ((owner_t *)context)->reported = count; }
static void row_owner(void *context, uint32_t index, const dj_link_peer_track_t *t, bool detail)
{
    owner_t *o = context; assert(!detail && index == o->rows && t->rekordbox_id == 100+index);
    assert(!strcmp(t->title, index ? "T2" : "T1")); ++o->rows;
}
static void scenario(mock_db_mode_t mode)
{
    peer_t peer = {.mode=mode}; uint16_t discovery_port;
    peer.discovery = listen_local(&discovery_port); peer.database = listen_local(&peer.db_port);
    pthread_t thread; assert(pthread_create(&thread, NULL, peer_worker, &peer) == 0);
    owner_t owner = {.fd=-1};
    dj_link_db_io_t io = {.connect=connect_owner, .send=send_owner, .close=close_owner,
        .list_begin=begin_owner, .track=row_owner, .ctx=&owner};
    dj_link_db_init(&owner.client, &io, 2000); owner.client.discovery_port = discovery_port;
    owner.client.step_timeout_ms = mode == SILENT ? 100 : 1000;
    uint32_t started = clock_ms();
    dj_link_db_start(&owner.client, INADDR_LOOPBACK, 2, DJLINK_SLOT_USB, 4, 7, started);
    while (!owner.client.list_done && owner.client.phase != DJ_LINK_DB_FAILED) {
        uint32_t now = clock_ms(); assert((uint32_t)(now-started) < 3000);
        if (owner.connected) {
            owner.connected = false;
            dj_link_db_on_connected(&owner.client, owner.connection_epoch, now);
        }
        if (owner.fd >= 0) {
            struct pollfd p = {.fd=owner.fd, .events=POLLIN};
            if (poll(&p, 1, 10) > 0 && p.revents) {
                uint8_t buf[257]; int n = recv(owner.fd, buf, sizeof(buf), 0);
                uint64_t epoch = owner.connection_epoch;
                if (n > 0) dj_link_db_on_data(&owner.client, epoch, buf, (size_t)n, now);
                else dj_link_db_on_closed(&owner.client, epoch, now);
            }
        }
        dj_link_db_poll(&owner.client, clock_ms());
    }
    if (mode == NORMAL) assert(owner.rows == 2 && owner.reported == 2 && owner.client.list_done);
    else assert(owner.client.phase == DJ_LINK_DB_FAILED && !owner.client.list_done);
    dj_link_db_stop(&owner.client); close_owner(&owner);
    assert(pthread_join(thread, NULL) == 0); close(peer.discovery); close(peer.database);
}
int main(void)
{
    scenario(NORMAL); scenario(BAD_MAGIC); scenario(DISCONNECT); scenario(SILENT);
    puts("PASS real TCP DBServer discovery/setup/fragments/browse, malformed, disconnect, timeout");
    return 0;
}
