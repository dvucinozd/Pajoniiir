#define _DEFAULT_SOURCE 1
#include "dj_link_udp.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

int main(void)
{
    dj_link_udp_t t = {.fd={-1,-1,-1}};
    uint16_t ports[3] = {0,0,0};
    assert(!dj_link_udp_open(&t, "missing0", 0x7f000001, 0xff000000, ports));
    for (unsigned i = 0; i < 3; ++i) assert(t.fd[i] == -1);
    assert(!dj_link_udp_open(&t, "", 0x7f000001, 0xff000000, ports));
    assert(!dj_link_udp_open(&t, "lo", 0, 0xff000000, ports));
    assert(dj_link_udp_open(&t, "lo", 0x7f000001, 0xff000000, ports));
    int sender = socket(AF_INET, SOCK_DGRAM, 0); assert(sender >= 0);
    dj_link_datagram_t packet;
    for (unsigned i = 0; i < 3; ++i) {
        struct sockaddr_in addr; socklen_t len = sizeof(addr);
        assert(getsockname(t.fd[i], (struct sockaddr *)&addr, &len) == 0);
        assert(addr.sin_addr.s_addr == htonl(i==2 ? 0x7f000001 : INADDR_ANY));
        addr.sin_addr.s_addr = htonl(0x7f000001);
        uint8_t payload[] = {1,2,3,4};
        assert(sendto(sender, payload, sizeof(payload), 0, (struct sockaddr *)&addr, len) == sizeof(payload));
        assert(dj_link_udp_receive(&t, &packet, 40) == 1);
        assert(packet.port == 50000+i && packet.source_ip == 0x7f000001 && packet.len == sizeof(payload));
        assert(!memcmp(packet.bytes, payload, sizeof(payload)));
        uint8_t oversized[DJLINK_MAX_PACKET+30] = {0};
        assert(sendto(sender, oversized, sizeof(oversized), 0, (struct sockaddr *)&addr, len) == sizeof(oversized));
        assert(dj_link_udp_receive(&t, &packet, 40) == 0);
    }
    assert(dj_link_udp_receive(&t, &packet, 1) == 0);
    struct sockaddr_in status; socklen_t status_len=sizeof(status);
    assert(getsockname(t.fd[2],(struct sockaddr *)&status,&status_len)==0);
    int one=1; assert(setsockopt(sender,SOL_SOCKET,SO_BROADCAST,&one,sizeof(one))==0);
    status.sin_addr.s_addr=htonl(0x7fffffff);
    uint8_t broadcast=1;
    assert(sendto(sender,&broadcast,1,0,(struct sockaddr *)&status,status_len)==1);
    assert(dj_link_udp_receive(&t,&packet,40)==0); /* No broadcast LOAD commands. */
    /* Discovery flood cannot starve beat/status receive. */
    for (unsigned i = 0; i < 3; ++i) {
        struct sockaddr_in addr; socklen_t len = sizeof(addr);
        assert(getsockname(t.fd[i], (struct sockaddr *)&addr, &len) == 0);
        addr.sin_addr.s_addr = htonl(0x7f000001);
        uint8_t payload = 1;
        for (unsigned n = 0; n < (i ? 1u : 8u); ++n)
            assert(sendto(sender, &payload, 1, 0, (struct sockaddr *)&addr, len) == 1);
    }
    for (unsigned i = 0; i < 3; ++i) {
        assert(dj_link_udp_receive(&t, &packet, 40) == 1);
        assert(packet.port == 50000+i);
    }
    assert(!dj_link_udp_send(&t,49999,0x7f000001,&broadcast,1));
    assert(!dj_link_udp_send(&t,50001,0,&broadcast,1));
    /* Real loopback sockets verify each outbound family uses its own bound
     * source port and exact destination, including handoff/beat port 50001. */
    for (unsigned i=0;i<3;++i) {
        struct sockaddr_in dst={.sin_family=AF_INET,.sin_port=htons((uint16_t)(50000+i)),
            .sin_addr={.s_addr=htonl(0x7f000001)}};
        int receiver=socket(AF_INET,SOCK_DGRAM,0);assert(receiver>=0);
        assert(bind(receiver,(struct sockaddr *)&dst,sizeof(dst))==0);
        struct timeval timeout={.tv_sec=1};
        assert(setsockopt(receiver,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout))==0);
        assert(dj_link_udp_send(&t,(uint16_t)(50000+i),0x7f000001,&broadcast,1));
        struct sockaddr_in src; socklen_t src_len=sizeof(src);uint8_t value=0;
        assert(recvfrom(receiver,&value,1,0,(struct sockaddr *)&src,&src_len)==1 && value==1);
        struct sockaddr_in bound;socklen_t bound_len=sizeof(bound);
        assert(getsockname(t.fd[i],(struct sockaddr *)&bound,&bound_len)==0);
        assert(src.sin_port==bound.sin_port);close(receiver);
    }
    close(sender); dj_link_udp_close(&t); dj_link_udp_close(&t);
    assert(dj_link_udp_receive(&t, &packet, 1) == -1);
    puts("PASS interface-required UDP, three ports, oversized datagrams, timeout, teardown");
    return 0;
}
