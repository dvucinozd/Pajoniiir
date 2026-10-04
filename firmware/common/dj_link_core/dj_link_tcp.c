#ifndef ESP_PLATFORM
#define _DEFAULT_SOURCE 1
#endif
#include "dj_link_tcp.h"
#include <errno.h>
#include <string.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#include "lwip/inet.h"
#else
#include <arpa/inet.h>
#include <net/if.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>
#include <fcntl.h>
#endif

void dj_link_tcp_init(dj_link_tcp_t *t, dj_link_db_t *client)
{
    memset(t, 0, sizeof(*t)); t->fd = -1; t->client = client;
}
void dj_link_tcp_close(void *ctx)
{
    dj_link_tcp_t *t = ctx;
    if (t->fd >= 0) close(t->fd);
    t->fd = -1; t->connecting = false; t->tx_len = t->tx_offset = 0;
}
bool dj_link_tcp_bind(dj_link_tcp_t *t, const char *name, uint32_t local_ip)
{
    dj_link_tcp_close(t);
    t->interface_name[0] = 0; t->local_ip = 0;
    if (!name || !*name || strlen(name) >= sizeof(t->interface_name) || !local_ip) return false;
    memcpy(t->interface_name, name, strlen(name)+1); t->local_ip = local_ip;
    return true;
}
int dj_link_tcp_connect(void *ctx, uint32_t ip, uint16_t port)
{
    dj_link_tcp_t *t = ctx;
    dj_link_tcp_close(t);
    if (!t->interface_name[0] || !t->local_ip || !ip || !port) return -1;
    t->fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (t->fd < 0) return -1;
#ifdef ESP_PLATFORM
    struct ifreq iface = {0};
    memcpy(iface.ifr_name, t->interface_name, strlen(t->interface_name)+1);
    if (setsockopt(t->fd, SOL_SOCKET, SO_BINDTODEVICE, &iface, sizeof(iface)) < 0) goto fail;
#else
    if (setsockopt(t->fd, SOL_SOCKET, SO_BINDTODEVICE, t->interface_name,
                   strlen(t->interface_name)+1) < 0) goto fail;
#endif
    struct sockaddr_in local = {.sin_family=AF_INET, .sin_addr={.s_addr=htonl(t->local_ip)}};
    if (bind(t->fd, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        fcntl(t->fd, F_SETFL, O_NONBLOCK) < 0) goto fail;
    struct sockaddr_in remote = {.sin_family=AF_INET, .sin_port=htons(port),
        .sin_addr={.s_addr=htonl(ip)}};
    t->epoch = t->client->connection_epoch;
    if (connect(t->fd, (struct sockaddr *)&remote, sizeof(remote)) < 0 && errno != EINPROGRESS) goto fail;
    t->connecting = true; return 0;
fail:
    dj_link_tcp_close(t); return -1;
}
int dj_link_tcp_send(void *ctx, const uint8_t *bytes, size_t len)
{
    dj_link_tcp_t *t = ctx;
    if (t->fd < 0 || !bytes || !len || len > sizeof(t->tx) || t->tx_len) return -1;
    memcpy(t->tx, bytes, len); t->tx_len = len; t->tx_offset = 0; return 0;
}
static void failed(dj_link_tcp_t *t, uint32_t now)
{
    uint64_t epoch = t->epoch;
    dj_link_tcp_close(t); dj_link_db_on_closed(t->client, epoch, now);
}
void dj_link_tcp_poll(dj_link_tcp_t *t, uint32_t now)
{
    if (t->fd < 0) return;
    fd_set rd, wr; FD_ZERO(&rd); FD_ZERO(&wr); FD_SET(t->fd, &rd);
    if (t->connecting || t->tx_len) FD_SET(t->fd, &wr);
    struct timeval timeout = {0};
    int ready = select(t->fd+1, &rd, &wr, NULL, &timeout);
    if (ready < 0) { if (errno != EINTR) failed(t, now); return; }
    if (!ready) return;
    int fd = t->fd; uint64_t epoch = t->epoch;
    if (t->connecting && (FD_ISSET(fd, &rd) || FD_ISSET(fd, &wr))) {
        int error = 0; socklen_t size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &size) < 0 || error) { failed(t, now); return; }
        t->connecting = false; dj_link_db_on_connected(t->client, epoch, now);
        if (t->fd != fd || t->epoch != epoch) return;
    }
    if (t->tx_len && FD_ISSET(fd, &wr)) {
#ifdef MSG_NOSIGNAL
        int flags = MSG_NOSIGNAL;
#else
        int flags = 0;
#endif
        int n = send(fd, t->tx+t->tx_offset, t->tx_len-t->tx_offset, flags);
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) { failed(t, now); return; }
        if (n > 0) t->tx_offset += (size_t)n;
        if (t->tx_offset == t->tx_len) t->tx_offset = t->tx_len = 0;
    }
    /* One read per tick bounds worker monopolization. A callback can replace
     * this descriptor during port discovery; never reuse the old readiness. */
    if (FD_ISSET(fd, &rd)) {
        uint8_t rx[1024]; int n = recv(fd, rx, sizeof(rx), 0);
        if (!n || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) { failed(t, now); return; }
        if (n > 0) dj_link_db_on_data(t->client, epoch, rx, (size_t)n, now);
    }
}
