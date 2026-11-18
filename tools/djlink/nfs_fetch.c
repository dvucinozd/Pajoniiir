/* Host tool: fetch one file from a player's NFS export with the same client the
 * P4 uses to download from a CDJ (djlink_nfs), over real UDP sockets. v342: the
 * way to check the export of our own USB key from a PC.
 *
 *   cd firmware/common/djlink
 *   gcc -O2 -I include -o /tmp/nfs_fetch ../../../tools/djlink/nfs_fetch.c \
 *       $(find src -name '*.c') -lm
 *   /tmp/nfs_fetch --ip 192.168.1.50 --path /PIONEER/rekordbox/export.pdb --out /tmp/export.pdb
 *
 * --bytes sends UTF-8 names (a Linux client); the default is UTF-16LE, what a
 * CDJ sends. --pmap PORT replaces 111. Prints the size, the time, the rate and
 * the SHA-less checksum (sum of the bytes mod 2^32, enough to compare with
 * `cksum`-style tools) and exits 0 only for a complete file.
 */
#define _DEFAULT_SOURCE
#include <arpa/inet.h>
#include <getopt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "djlink/nfs.h"

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000u + ts.tv_nsec / 1000000u);
}

typedef struct {
    int fd;
    FILE *out;
    uint32_t size, sum;
} ctx_t;

static int io_send(void *c, uint32_t ip, uint16_t port, const uint8_t *buf, size_t len)
{
    ctx_t *x = c;
    struct sockaddr_in a = { .sin_family = AF_INET, .sin_port = htons(port) };
    a.sin_addr.s_addr = htonl(ip);
    return sendto(x->fd, buf, len, 0, (struct sockaddr *)&a, sizeof(a)) == (ssize_t)len ? 0 : -1;
}

static int io_open(void *c, uint32_t size)
{
    ((ctx_t *)c)->size = size;
    return 0;
}

static int io_write(void *c, uint32_t off, const uint8_t *d, size_t n)
{
    ctx_t *x = c;
    (void)off;
    for (size_t i = 0; i < n; i++) {
        x->sum += d[i];
    }
    return x->out && fwrite(d, 1, n, x->out) != n ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *ip = NULL, *path = NULL, *out = NULL, *export_path = "/C/";
    int bytes = 0, pmap = 0, window = 4, read_size = 2048;
    static const struct option opts[] = {
        { "ip", 1, 0, 'i' }, { "path", 1, 0, 'p' }, { "out", 1, 0, 'o' }, { "export", 1, 0, 'e' },
        { "bytes", 0, 0, 'b' }, { "pmap", 1, 0, 'm' }, { "window", 1, 0, 'w' },
        { "read-size", 1, 0, 'r' }, { 0, 0, 0, 0 } };
    int c;
    while ((c = getopt_long(argc, argv, "", opts, NULL)) != -1) {
        switch (c) {
        case 'i': ip = optarg; break;
        case 'p': path = optarg; break;
        case 'o': out = optarg; break;
        case 'e': export_path = optarg; break;
        case 'b': bytes = 1; break;
        case 'm': pmap = atoi(optarg); break;
        case 'w': window = atoi(optarg); break;
        case 'r': read_size = atoi(optarg); break;
        default: ip = NULL;
        }
    }
    if (!ip || !path) {
        fprintf(stderr, "usage: nfs_fetch --ip A.B.C.D --path /dir/file [--out FILE] [--export /C/] "
                        "[--bytes] [--pmap PORT] [--window N] [--read-size N]\n");
        return 2;
    }
    ctx_t x = { socket(AF_INET, SOCK_DGRAM, 0), out ? fopen(out, "wb") : NULL, 0, 0 };
    static uint8_t window_buf[DJLINK_NFS_WINDOW_MAX * DJLINK_NFS_MAXDATA];
    struct in_addr ina;
    inet_pton(AF_INET, ip, &ina);
    djlink_nfs_fetch_cfg_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.host_ip = ntohl(ina.s_addr);
    cfg.export_path = export_path;
    cfg.path = path;
    cfg.charset = bytes ? DJLINK_NFS_NAMES_BYTES : DJLINK_NFS_NAMES_UTF16LE;
    cfg.portmap_port = (uint16_t)pmap;
    cfg.read_size = (uint32_t)read_size;
    cfg.window = (uint8_t)window;
    cfg.window_buf = window_buf;
    cfg.window_buf_len = sizeof(window_buf);
    cfg.xid_seed = (uint32_t)time(NULL);
    djlink_nfs_io_t io = { io_send, io_open, io_write, NULL, &x };
    static djlink_nfs_t cl;
    const uint32_t t0 = now_ms();
    djlink_nfs_fetch(&cl, &cfg, &io, t0);
    while (djlink_nfs_state(&cl) == DJLINK_NFS_BUSY) {
        struct pollfd p = { x.fd, POLLIN, 0 };
        if (poll(&p, 1, 5) > 0) {
            uint8_t rx[16384];
            const ssize_t n = recv(x.fd, rx, sizeof(rx), 0);
            if (n > 0) {
                djlink_nfs_on_datagram(&cl, rx, (size_t)n, now_ms());
            }
        }
        djlink_nfs_poll(&cl, now_ms());
    }
    const uint32_t dt = now_ms() - t0;
    if (x.out) {
        fclose(x.out);
    }
    if (djlink_nfs_state(&cl) != DJLINK_NFS_DONE) {
        printf("FAILED: %s (retransmits %u)\n", djlink_nfs_error_text(&cl), (unsigned)cl.retransmits);
        return 1;
    }
    printf("OK %s: %u bytes in %u ms (%.0f KB/s), byte-sum %u, %u retransmits\n", path,
           (unsigned)x.size, (unsigned)dt, dt ? x.size / 1.024 / dt : 0.0, (unsigned)x.sum,
           (unsigned)cl.retransmits);
    return 0;
}
