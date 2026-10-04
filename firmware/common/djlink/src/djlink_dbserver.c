#include "djlink/dbserver.h"

#include <string.h>
#include <limits.h>

int djlink_db_port_query_build(uint8_t *out, size_t cap)
{
    static const char name[] = "RemoteDBServer";
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_DB_PORT_QUERY_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    out[0] = 0x00;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = 0x0f;
    memcpy(&out[4], name, sizeof(name)); /* includes NUL; total 4+14+1 = 19 */
    return (int)DJLINK_DB_PORT_QUERY_LEN;
}

djlink_err_t djlink_db_port_query_reply_parse(const uint8_t *buf, size_t len, uint16_t *port)
{
    if (buf == NULL || port == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (len < 2) {
        return DJLINK_ERR_TRUNCATED;
    }
    *port = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    return DJLINK_OK;
}

int djlink_db_setup_build(uint8_t *out, size_t cap)
{
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    if (cap < DJLINK_DB_SETUP_LEN) {
        return DJLINK_ERR_BOUNDS;
    }
    out[0] = DJLINK_DB_FIELD_INT32;
    out[1] = 0x00;
    out[2] = 0x00;
    out[3] = 0x00;
    out[4] = 0x01;
    return (int)DJLINK_DB_SETUP_LEN;
}

/* Append one typed field. Returns bytes written or negative error. */
static int put_field(uint8_t *out, size_t cap, uint8_t type, uint32_t num, const uint8_t *data,
                     size_t data_len)
{
    size_t need = 1 + 4; /* worst case: type + 4-byte length */
    size_t pos = 0;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    switch (type) {
    case DJLINK_DB_FIELD_INT8:
        need = 2;
        break;
    case DJLINK_DB_FIELD_INT16:
        need = 3;
        break;
    case DJLINK_DB_FIELD_INT32:
        need = 5;
        break;
    case DJLINK_DB_FIELD_BINARY:
        if (data_len > (size_t)INT_MAX - 5u) return DJLINK_ERR_BOUNDS;
        need = 5 + data_len;
        break;
    case DJLINK_DB_FIELD_STRING:
        if (data_len > (size_t)INT_MAX - 5u) return DJLINK_ERR_BOUNDS;
        if (data_len & 1u) {
            return DJLINK_ERR_BOUNDS; /* UTF-16BE: whole code units only */
        }
        need = 5 + data_len;
        break;
    default:
        return DJLINK_ERR_BOUNDS;
    }
    if (cap < need) {
        return DJLINK_ERR_BOUNDS;
    }
    if ((type == DJLINK_DB_FIELD_BINARY || type == DJLINK_DB_FIELD_STRING) &&
        data_len && !data) return DJLINK_ERR_NULL;
    out[pos++] = type;
    switch (type) {
    case DJLINK_DB_FIELD_INT8:
        out[pos++] = (uint8_t)num;
        break;
    case DJLINK_DB_FIELD_INT16:
        out[pos++] = (uint8_t)(num >> 8);
        out[pos++] = (uint8_t)num;
        break;
    case DJLINK_DB_FIELD_INT32:
        out[pos++] = (uint8_t)(num >> 24);
        out[pos++] = (uint8_t)(num >> 16);
        out[pos++] = (uint8_t)(num >> 8);
        out[pos++] = (uint8_t)num;
        break;
    default: {
        /* A string's length counts UTF-16 code units, a blob's bytes. */
        size_t wire_len = type == DJLINK_DB_FIELD_STRING ? data_len / 2u : data_len;
        out[pos++] = (uint8_t)(wire_len >> 24);
        out[pos++] = (uint8_t)(wire_len >> 16);
        out[pos++] = (uint8_t)(wire_len >> 8);
        out[pos++] = (uint8_t)wire_len;
        if (data_len) memcpy(&out[pos], data, data_len);
        pos += data_len;
        break;
    }
    }
    return (int)pos;
}

static const uint8_t *skip_field(const uint8_t *buf, const uint8_t *end, uint8_t type,
                                 const uint8_t **data, size_t *data_len)
{
    uint32_t vlen;
    const uint8_t *p = buf;
    if (p >= end || *p != type) return NULL;
    switch (type) {
    case DJLINK_DB_FIELD_INT8:
        if (end - p < 2) {
            return NULL;
        }
        *data = p + 1;
        *data_len = 1;
        return p + 2;
    case DJLINK_DB_FIELD_INT16:
        if (end - p < 3) {
            return NULL;
        }
        *data = p + 1;
        *data_len = 2;
        return p + 3;
    case DJLINK_DB_FIELD_INT32:
        if (end - p < 5) {
            return NULL;
        }
        *data = p + 1;
        *data_len = 4;
        return p + 5;
    case DJLINK_DB_FIELD_BINARY:
    case DJLINK_DB_FIELD_STRING:
        if (end - p < 5) {
            return NULL;
        }
        vlen = djlink_rd32(&p[1]);
        if (type == DJLINK_DB_FIELD_STRING) {
            /* The length counts UTF-16 code units (NUL included), not bytes;
             * *data_len is in bytes. */
            if (vlen > 0x7fffffffu) {
                return NULL;
            }
            vlen *= 2u;
        }
        if ((size_t)(end - p - 5) < vlen) {
            return NULL;
        }
        *data = p + 5;
        *data_len = vlen;
        return p + 5 + vlen;
    default:
        return NULL;
    }
}

static uint32_t field_num(const uint8_t *data, size_t len)
{
    if (len == 1) {
        return data[0];
    }
    if (len == 2) {
        return djlink_rd16(data);
    }
    return djlink_rd32(data);
}

int djlink_db_msg_build(uint32_t txid, uint16_t type, const djlink_db_arg_t *args,
                        uint8_t arg_count, uint8_t *out, size_t cap)
{
    uint8_t arg_types[DJLINK_DB_MAX_ARGS];
    size_t pos = 0;
    int n;
    int i;

    if (args == NULL && arg_count > 0) {
        return DJLINK_ERR_NULL;
    }
    if (arg_count > DJLINK_DB_MAX_ARGS) {
        return DJLINK_ERR_BOUNDS;
    }
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    n = put_field(out, cap, DJLINK_DB_FIELD_INT32, DJLINK_DB_MAGIC, NULL, 0);
    if (n < 0) {
        return n;
    }
    pos = (size_t)n;
    n = put_field(&out[pos], cap - pos, DJLINK_DB_FIELD_INT32, txid, NULL, 0);
    if (n < 0) {
        return n;
    }
    pos += (size_t)n;
    n = put_field(&out[pos], cap - pos, DJLINK_DB_FIELD_INT16, type, NULL, 0);
    if (n < 0) {
        return n;
    }
    pos += (size_t)n;
    n = put_field(&out[pos], cap - pos, DJLINK_DB_FIELD_INT8, arg_count, NULL, 0);
    if (n < 0) {
        return n;
    }
    pos += (size_t)n;
    memset(arg_types, 0, sizeof(arg_types));
    for (i = 0; i < (int)arg_count; ++i) {
        /* Argument tag values differ from the field type tags. */
        switch (args[i].type) {
        case DJLINK_DB_FIELD_INT8:
            arg_types[i] = 0x04;
            break;
        case DJLINK_DB_FIELD_INT16:
            arg_types[i] = 0x05;
            break;
        case DJLINK_DB_FIELD_INT32:
            arg_types[i] = 0x06;
            break;
        case DJLINK_DB_FIELD_BINARY:
            arg_types[i] = 0x03;
            break;
        case DJLINK_DB_FIELD_STRING:
            arg_types[i] = 0x02;
            break;
        default:
            return DJLINK_ERR_BOUNDS;
        }
    }
    n = put_field(&out[pos], cap - pos, DJLINK_DB_FIELD_BINARY, 0, arg_types,
                  sizeof(arg_types));
    if (n < 0) {
        return n;
    }
    pos += (size_t)n;
    for (i = 0; i < (int)arg_count; ++i) {
        n = put_field(&out[pos], cap - pos, args[i].type, args[i].num, args[i].bin,
                      args[i].bin_len);
        if (n < 0) {
            return n;
        }
        pos += (size_t)n;
    }
    return (int)pos;
}

djlink_err_t djlink_db_msg_parse_prefix(const uint8_t *buf, size_t len,
                                       djlink_db_msg_t *out, size_t *consumed)
{
    const uint8_t *p = buf;
    const uint8_t *end;
    const uint8_t *data;
    size_t data_len;
    uint32_t magic;
    uint8_t i;

    if (consumed) *consumed = 0;
    if (out == NULL) {
        return DJLINK_ERR_NULL;
    }
    memset(out, 0, sizeof(*out));
    if (buf == NULL) {
        return DJLINK_ERR_NULL;
    }
    end = buf + len;
    /* magic int32 field */
    if (p == end || *p != DJLINK_DB_FIELD_INT32) {
        return DJLINK_ERR_MAGIC;
    }
    p = skip_field(p, end, DJLINK_DB_FIELD_INT32, &data, &data_len);
    if (p == NULL) {
        return DJLINK_ERR_TRUNCATED;
    }
    magic = field_num(data, data_len);
    if (magic != DJLINK_DB_MAGIC) {
        return DJLINK_ERR_MAGIC;
    }
    /* txid */
    if (p == end || *p != DJLINK_DB_FIELD_INT32) {
        return DJLINK_ERR_TRUNCATED;
    }
    p = skip_field(p, end, DJLINK_DB_FIELD_INT32, &data, &data_len);
    if (p == NULL) {
        return DJLINK_ERR_TRUNCATED;
    }
    out->txid = field_num(data, data_len);
    /* type */
    if (p == end || *p != DJLINK_DB_FIELD_INT16) {
        return DJLINK_ERR_TRUNCATED;
    }
    p = skip_field(p, end, DJLINK_DB_FIELD_INT16, &data, &data_len);
    if (p == NULL) {
        return DJLINK_ERR_TRUNCATED;
    }
    out->type = (uint16_t)field_num(data, data_len);
    /* arg count */
    if (p == end || *p != DJLINK_DB_FIELD_INT8) {
        return DJLINK_ERR_TRUNCATED;
    }
    p = skip_field(p, end, DJLINK_DB_FIELD_INT8, &data, &data_len);
    if (p == NULL) {
        return DJLINK_ERR_TRUNCATED;
    }
    out->arg_count = data[0];
    if (out->arg_count > DJLINK_DB_MAX_ARGS) {
        return DJLINK_ERR_BOUNDS;
    }
    /* argument type tags blob */
    if (p == end || *p != DJLINK_DB_FIELD_BINARY) {
        return DJLINK_ERR_TRUNCATED;
    }
    /* Players always send 12 tag bytes; vynull sends one per argument. */
    p = skip_field(p, end, DJLINK_DB_FIELD_BINARY, &data, &data_len);
    if (p == NULL || data_len < out->arg_count) {
        return DJLINK_ERR_TRUNCATED;
    }
    for (i = 0; i < out->arg_count; ++i) {
        switch (data[i]) {
        case 0x02:
            out->args[i].type = DJLINK_DB_FIELD_STRING;
            break;
        case 0x03:
            out->args[i].type = DJLINK_DB_FIELD_BINARY;
            break;
        case 0x04:
            out->args[i].type = DJLINK_DB_FIELD_INT8;
            break;
        case 0x05:
            out->args[i].type = DJLINK_DB_FIELD_INT16;
            break;
        case 0x06:
            out->args[i].type = DJLINK_DB_FIELD_INT32;
            break;
        default:
            return DJLINK_ERR_BOUNDS;
        }
    }
    /* arguments */
    for (i = 0; i < out->arg_count; ++i) {
        p = skip_field(p, end, out->args[i].type, &data, &data_len);
        if (p == NULL) {
            return DJLINK_ERR_TRUNCATED;
        }
        out->args[i].bin = data;
        out->args[i].bin_len = data_len;
        /* Blobs/strings may contain zero or fewer than four bytes. Only the
         * numeric field variants have a scalar value. */
        if (out->args[i].type == DJLINK_DB_FIELD_INT8 ||
            out->args[i].type == DJLINK_DB_FIELD_INT16 ||
            out->args[i].type == DJLINK_DB_FIELD_INT32)
            out->args[i].num = field_num(data, data_len);
    }
    if (consumed) *consumed = (size_t)(p - buf);
    return DJLINK_OK;
}

djlink_err_t djlink_db_msg_parse(const uint8_t *buf, size_t len, djlink_db_msg_t *out)
{
    return djlink_db_msg_parse_prefix(buf, len, out, NULL);
}

int djlink_db_context_setup_build(uint8_t device_number, uint8_t *out, size_t cap)
{
    djlink_db_arg_t arg;

    memset(&arg, 0, sizeof(arg));
    arg.type = DJLINK_DB_FIELD_INT32;
    arg.num = device_number;
    return djlink_db_msg_build(DJLINK_DB_SETUP_TXID, DJLINK_DB_TYPE_SETUP, &arg, 1, out, cap);
}

int djlink_db_metadata_request_build(uint32_t txid, uint8_t device_number, uint8_t slot,
                                     uint32_t rekordbox_id, uint8_t *out, size_t cap)
{
    djlink_db_arg_t args[2];

    memset(args, 0, sizeof(args));
    args[0].type = DJLINK_DB_FIELD_INT32;
    args[0].num = ((uint32_t)device_number << 24) | (0x01u << 16) |
                  ((uint32_t)slot << 8) | 0x01u; /* DMST */
    args[1].type = DJLINK_DB_FIELD_INT32;
    args[1].num = rekordbox_id;
    return djlink_db_msg_build(txid, DJLINK_DB_TYPE_METADATA_REQUEST, args, 2, out, cap);
}

int djlink_db_render_request_build(uint32_t txid, uint8_t device_number, uint8_t slot,
                                   uint32_t offset, uint32_t limit, uint8_t *out, size_t cap)
{
    djlink_db_arg_t args[6];

    memset(args, 0, sizeof(args));
    args[0].type = DJLINK_DB_FIELD_INT32;
    args[0].num = ((uint32_t)device_number << 24) | (0x01u << 16) |
                  ((uint32_t)slot << 8) | 0x01u;
    args[1].type = DJLINK_DB_FIELD_INT32;
    args[1].num = offset;
    args[2].type = DJLINK_DB_FIELD_INT32;
    args[2].num = limit;
    args[3].type = DJLINK_DB_FIELD_INT32;
    args[3].num = 0;
    args[4].type = DJLINK_DB_FIELD_INT32;
    args[4].num = limit;
    args[5].type = DJLINK_DB_FIELD_INT32;
    args[5].num = 0;
    return djlink_db_msg_build(txid, DJLINK_DB_TYPE_RENDER, args, 6, out, cap);
}
