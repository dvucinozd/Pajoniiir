#ifndef ESP_PLATFORM
#define _DEFAULT_SOURCE 1
#endif
#include "dj_link_udp.h"
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
#endif

void dj_link_udp_close(dj_link_udp_t *t)
{
    if (!t) return;
    for (unsigned i = 0; i < 3; ++i) {
        if (t->fd[i] >= 0) close(t->fd[i]);
        t->fd[i] = -1;
    }
    t->broadcast_ip = 0;
    t->next_rx = 0;
}
bool dj_link_udp_open(dj_link_udp_t *t, const char *name,
                      uint32_t ip, uint32_t netmask, const uint16_t ports[3])
{
    if (!t) { errno = EINVAL; return false; }
    for (unsigned i = 0; i < 3; ++i) t->fd[i] = -1;
    t->broadcast_ip = 0;
    if (!name || !*name || strlen(name) >= IFNAMSIZ || !ip || !netmask || !ports) {
        errno = EINVAL; return false;
    }
    for (unsigned i = 0; i < 3; ++i) {
        int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
        t->fd[i] = fd;
        if (fd < 0) goto fail;
#ifdef ESP_PLATFORM
        /* lwIP expects struct ifreq; Linux expects the NUL-terminated name. */
        struct ifreq iface = {0}; memcpy(iface.ifr_name, name, strlen(name)+1);
        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, &iface, sizeof(iface)) < 0) goto fail;
#else
        if (setsockopt(fd, SOL_SOCKET, SO_BINDTODEVICE, name, strlen(name)+1) < 0) goto fail;
#endif
        int one = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &one, sizeof(one)) < 0) goto fail;
        struct timeval timeout = {.tv_sec=0, .tv_usec=40000};
        if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) goto fail;
        struct sockaddr_in address = {.sin_family=AF_INET, .sin_port=htons(ports[i]),
            .sin_addr={.s_addr=htonl(INADDR_ANY)}};
        if (bind(fd, (struct sockaddr *)&address, sizeof(address)) < 0) goto fail;
    }
    t->broadcast_ip = ip | ~netmask;
    return true;
fail: {
    int error = errno; dj_link_udp_close(t); errno = error; return false;
}
}
int dj_link_udp_receive(dj_link_udp_t *t, dj_link_datagram_t *out, unsigned wait_ms)
{
    if (!t || !out) { errno = EINVAL; return -1; }
    fd_set read_set; FD_ZERO(&read_set); int maximum = -1;
    for (unsigned i = 0; i < 3; ++i) {
        if (t->fd[i] < 0) { errno = EBADF; return -1; }
        FD_SET(t->fd[i], &read_set);
        if (t->fd[i] > maximum) maximum = t->fd[i];
    }
    if (wait_ms > 40) wait_ms = 40;
    struct timeval wait = {.tv_sec=0, .tv_usec=(long)wait_ms*1000};
    int ready = select(maximum+1, &read_set, NULL, NULL, &wait);
    if (ready <= 0) return ready < 0 && errno != EINTR ? -1 : 0;
    for (unsigned at = 0; at < 3; ++at) {
        unsigned i = (t->next_rx + at) % 3;
        if (!FD_ISSET(t->fd[i], &read_set)) continue;
        t->next_rx = (uint8_t)((i+1)%3);
        struct sockaddr_in source = {0}; socklen_t source_len = sizeof(source);
        int n = recvfrom(t->fd[i], out->bytes, sizeof(out->bytes), MSG_DONTWAIT,
            (struct sockaddr *)&source, &source_len);
        if (n < 0) return errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR ? 0 : -1;
        if ((size_t)n > DJLINK_MAX_PACKET || source.sin_family != AF_INET) return 0;
        out->source_ip = ntohl(source.sin_addr.s_addr);
        out->port = (uint16_t)(DJLINK_PORT_DISCOVERY+i); out->len = (size_t)n;
        return 1;
    }
    return 0;
}
bool dj_link_udp_broadcast(dj_link_udp_t *t, const uint8_t *buf, size_t len)
{
    if (!t || !buf || !len || len > DJLINK_MAX_PACKET || t->fd[0] < 0 || !t->broadcast_ip) return false;
    struct sockaddr_in destination = {.sin_family=AF_INET, .sin_port=htons(DJLINK_PORT_DISCOVERY),
        .sin_addr={.s_addr=htonl(t->broadcast_ip)}};
    return sendto(t->fd[0], buf, len, 0, (struct sockaddr *)&destination, sizeof(destination)) == (int)len;
}
