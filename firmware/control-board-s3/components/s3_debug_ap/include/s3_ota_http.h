#pragma once

#include <stddef.h>
#include <stdint.h>

#define S3_OTA_HTTP_RECV_TIMEOUT (-1)
#define S3_OTA_HTTP_RECV_ERROR   (-2)

typedef int (*s3_ota_http_recv_fn_t)(void *ctx, uint8_t *buffer, size_t wanted);
typedef uint32_t (*s3_ota_http_now_ms_fn_t)(void *ctx);

typedef struct {
    size_t content_len;
    void *ctx;
    s3_ota_http_recv_fn_t recv;
    s3_ota_http_now_ms_fn_t now_ms;
} s3_ota_http_request_t;

typedef enum {
    S3_OTA_HTTP_OK = 0,
    S3_OTA_HTTP_BAD_REQUEST,
    S3_OTA_HTTP_FORBIDDEN,
    S3_OTA_HTTP_PAYLOAD_TOO_LARGE,
    S3_OTA_HTTP_CONFLICT,
    S3_OTA_HTTP_TIMEOUT,
    S3_OTA_HTTP_INTERNAL_ERROR,
} s3_ota_http_result_code_t;

typedef struct {
    s3_ota_http_result_code_t code;
    const char *detail;
} s3_ota_http_result_t;

s3_ota_http_result_t s3_ota_http_process(const s3_ota_http_request_t *request);
