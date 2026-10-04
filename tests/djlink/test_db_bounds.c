#include "djlink.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static void payload(uint8_t type, size_t length)
{
    uint8_t bytes[4] = {0, 0x41, 0, 0};
    uint8_t built[128];
    djlink_db_arg_t arg = {.type=type, .bin=length ? bytes : NULL, .bin_len=length};
    int n = djlink_db_msg_build(42, DJLINK_DB_TYPE_MENU_ITEM, &arg, 1, built, sizeof built);
    assert(n > 0);
    /* Exact-size allocation makes an accidental read after an empty or short
     * final payload visible under ASan rather than hidden in a large array. */
    uint8_t *exact = malloc((size_t)n);
    assert(exact);
    memcpy(exact, built, (size_t)n);
    djlink_db_msg_t msg;
    size_t used = 999;
    assert(djlink_db_msg_parse_prefix(exact, (size_t)n, &msg, &used) == DJLINK_OK);
    assert(used == (size_t)n && msg.txid == 42 && msg.arg_count == 1);
    assert(msg.args[0].num == 0 && msg.args[0].bin_len == length);
    if (length) assert(memcmp(msg.args[0].bin, bytes, length) == 0);
    for (size_t i=0; i<(size_t)n; ++i) {
        used = 999;
        assert(djlink_db_msg_parse_prefix(exact, i, &msg, &used) != DJLINK_OK);
        assert(used == 0);
    }
    built[32] = DJLINK_DB_FIELD_INT32; /* Actual field contradicts binary/string tag. */
    assert(djlink_db_msg_parse(built, (size_t)n, &msg) != DJLINK_OK);
    free(exact);
}

int main(void)
{
    for (size_t n=0; n<=3; ++n) payload(DJLINK_DB_FIELD_BINARY, n);
    payload(DJLINK_DB_FIELD_STRING, 0);
    payload(DJLINK_DB_FIELD_STRING, 2);
    payload(DJLINK_DB_FIELD_STRING, 4);
    uint8_t wire[128];
    djlink_db_arg_t arg = {.type=DJLINK_DB_FIELD_BINARY, .bin_len=SIZE_MAX};
    assert(djlink_db_msg_build(1, 1, &arg, 1, wire, sizeof wire) == DJLINK_ERR_BOUNDS);
    arg.bin_len = 1;
    assert(djlink_db_msg_build(1, 1, &arg, 1, wire, sizeof wire) == DJLINK_ERR_NULL);
    arg.type = DJLINK_DB_FIELD_STRING;
    assert(djlink_db_msg_build(1, 1, &arg, 1, wire, sizeof wire) == DJLINK_ERR_BOUNDS);
    arg.type = DJLINK_DB_FIELD_INT32; arg.num=0x12345678;
    int n=djlink_db_msg_build(1, 1, &arg, 1, wire, sizeof wire);
    assert(n > 0 && (size_t)n*2 <= sizeof wire);
    memcpy(wire+n, wire, (size_t)n); /* Coalesced TCP messages. */
    djlink_db_msg_t msg; size_t used=0;
    assert(djlink_db_msg_parse_prefix(wire, (size_t)n*2, &msg, &used) == DJLINK_OK);
    assert(used == (size_t)n && msg.args[0].num == arg.num);
    assert(djlink_db_msg_parse_prefix(wire+used, (size_t)n, &msg, &used) == DJLINK_OK);
    assert(used == (size_t)n);
    puts("PASS DB short/empty fields, tag mismatch, truncation, payload bounds and TCP prefix consumption");
    return 0;
}
