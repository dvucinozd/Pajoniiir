#pragma once
#include "dj_link_db.h"

/* Worker-owned nonblocking adapter. No default-route/interface fallback.
 * send copies into its bounded queue; callbacks carry the captured epoch. */
typedef struct {
    int fd;
    bool connecting;
    uint64_t epoch;
    uint32_t local_ip;
    char interface_name[16];
    size_t tx_len, tx_offset;
    uint8_t tx[DJ_LINK_DB_TX_MAX];
    dj_link_db_t *client;
} dj_link_tcp_t;
void dj_link_tcp_init(dj_link_tcp_t *t, dj_link_db_t *client);
bool dj_link_tcp_bind(dj_link_tcp_t *t, const char *name, uint32_t local_ip);
int dj_link_tcp_connect(void *ctx, uint32_t ip, uint16_t port);
int dj_link_tcp_send(void *ctx, const uint8_t *bytes, size_t len);
void dj_link_tcp_close(void *ctx);
void dj_link_tcp_poll(dj_link_tcp_t *t, uint32_t now_ms);
