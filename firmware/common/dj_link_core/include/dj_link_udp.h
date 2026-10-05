#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "djlink/packet.h"

typedef struct { int fd[3]; uint32_t broadcast_ip; uint8_t next_rx; } dj_link_udp_t;
typedef struct {
    uint16_t port;
    uint32_t source_ip;
    size_t len;
    uint8_t bytes[DJLINK_MAX_PACKET + 1]; /* oversized datagrams are rejected */
} dj_link_datagram_t;
/* Requires a real interface name and host-order IPv4/netmask. No ANY-interface
 * fallback: SO_BINDTODEVICE failure closes the whole transport. Ports normally
 * 50000/50001/50002; test ports may be zero (ephemeral localhost UDP). */
bool dj_link_udp_open(dj_link_udp_t *t, const char *interface_name,
                      uint32_t ip, uint32_t netmask, const uint16_t ports[3]);
void dj_link_udp_close(dj_link_udp_t *t);
/* 1 datagram, 0 timeout/oversized, -1 socket error. wait_ms is bounded at 40. */
int dj_link_udp_receive(dj_link_udp_t *t, dj_link_datagram_t *out, unsigned wait_ms);
bool dj_link_udp_broadcast(dj_link_udp_t *t, const uint8_t *buf, size_t len);
bool dj_link_udp_send(dj_link_udp_t *t, uint16_t port, uint32_t ip,
    const uint8_t *buf, size_t len);
