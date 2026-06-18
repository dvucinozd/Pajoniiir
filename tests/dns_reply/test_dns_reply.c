#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "dns_reply.h"

static const uint8_t k_query_example_com[] = {
    0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x07, 'e',  'x',  'a',
    'm',  'p',  'l',  'e',  0x03, 'c',  'o',  'm',
    0x00, 0x00, 0x01, 0x00, 0x01,
};

static void test_builds_captive_a_reply(void)
{
    uint8_t out[128] = {0};
    const uint8_t ip[4] = {192, 168, 4, 1};

    size_t len = dns_build_captive_reply(k_query_example_com,
                                         sizeof(k_query_example_com),
                                         out,
                                         sizeof(out),
                                         ip);

    assert(len == sizeof(k_query_example_com) + 16);
    assert(out[0] == 0x12 && out[1] == 0x34);
    assert(out[2] == 0x85 && out[3] == 0x00);
    assert(out[4] == 0x00 && out[5] == 0x01);
    assert(out[6] == 0x00 && out[7] == 0x01);
    assert(memcmp(&out[12], &k_query_example_com[12], sizeof(k_query_example_com) - 12) == 0);
    assert(out[sizeof(k_query_example_com)] == 0xc0);
    assert(out[sizeof(k_query_example_com) + 1] == 0x0c);
    assert(memcmp(&out[len - 4], ip, sizeof(ip)) == 0);
}

static void test_rejects_malformed_or_too_small_buffers(void)
{
    uint8_t out[32] = {0};
    const uint8_t ip[4] = {192, 168, 4, 1};
    uint8_t malformed[] = {
        0x12, 0x34, 0x01, 0x00, 0x00, 0x01, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x20, 'b',  'a',  'd',
    };

    assert(dns_build_captive_reply(malformed, sizeof(malformed), out, sizeof(out), ip) == 0);
    assert(dns_build_captive_reply(k_query_example_com,
                                   sizeof(k_query_example_com),
                                   out,
                                   sizeof(out),
                                   ip) == 0);
    assert(dns_build_captive_reply(NULL, sizeof(k_query_example_com), out, sizeof(out), ip) == 0);
}

int main(void)
{
    test_builds_captive_a_reply();
    test_rejects_malformed_or_too_small_buffers();

    puts("dns_reply tests passed");
    return 0;
}
