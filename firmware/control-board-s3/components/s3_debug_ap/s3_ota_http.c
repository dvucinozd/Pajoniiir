#include "s3_ota_http.h"

#include <stdbool.h>
#include <stdlib.h>

#include "esp_err.h"
#include "ota_manifest.h"
#include "s3_ota.h"
#include "s3_ota_policy.h"
#include "s3_ota_upload_guard.h"

#define S3_OTA_HTTP_BUFFER_SIZE 4096u
#define S3_OTA_HTTP_MAX_CONSECUTIVE_TIMEOUTS 5u

static s3_ota_http_result_t result(s3_ota_http_result_code_t code,
                                   const char *detail)
{
    return (s3_ota_http_result_t) {.code = code, .detail = detail};
}

static int guarded_recv(const s3_ota_http_request_t *request,
                        uint8_t *buffer, size_t wanted,
                        s3_ota_upload_guard_t *guard,
                        s3_ota_upload_guard_result_t *guard_result)
{
    *guard_result = S3_OTA_UPLOAD_GUARD_OK;
    for (unsigned timeout_count = 0u;
         timeout_count < S3_OTA_HTTP_MAX_CONSECUTIVE_TIMEOUTS;
         ++timeout_count) {
        s3_ota_upload_guard_result_t check =
            s3_ota_upload_guard_check(guard, request->now_ms(request->ctx));
        if (check != S3_OTA_UPLOAD_GUARD_OK) {
            *guard_result = check;
            return S3_OTA_HTTP_RECV_TIMEOUT;
        }
        int received = request->recv(request->ctx, buffer, wanted);
        if (received > 0) {
            if ((size_t)received > wanted) return S3_OTA_HTTP_RECV_ERROR;
            s3_ota_upload_guard_note_bytes(guard, (size_t)received);
            check = s3_ota_upload_guard_check(guard,
                                              request->now_ms(request->ctx));
            if (check != S3_OTA_UPLOAD_GUARD_OK) {
                *guard_result = check;
                return S3_OTA_HTTP_RECV_TIMEOUT;
            }
            return received;
        }
        if (received != S3_OTA_HTTP_RECV_TIMEOUT) return S3_OTA_HTTP_RECV_ERROR;
    }
    return S3_OTA_HTTP_RECV_TIMEOUT;
}

static s3_ota_http_result_t receive_failure(bool started,
                                            int received,
                                            s3_ota_upload_guard_result_t guard_result)
{
    if (started) s3_ota_abort("HTTP upload interrupted");
    if (received == S3_OTA_HTTP_RECV_TIMEOUT) {
        return result(S3_OTA_HTTP_TIMEOUT,
                      guard_result == S3_OTA_UPLOAD_GUARD_TOO_SLOW
                          ? "Firmware upload throughput too low"
                          : "Firmware upload timed out");
    }
    return result(S3_OTA_HTTP_BAD_REQUEST, "Firmware upload interrupted");
}

s3_ota_http_result_t s3_ota_http_process(const s3_ota_http_request_t *request)
{
    if (!request || !request->recv || !request->now_ms) {
        return result(S3_OTA_HTTP_BAD_REQUEST, "Invalid OTA request");
    }
    if (request->content_len < DDJ_OTA_HEADER_SIZE + S3_OTA_IMAGE_HEADER_SIZE) {
        return result(S3_OTA_HTTP_BAD_REQUEST, "Signed OTA bundle is too small");
    }
    if (request->content_len >
        DDJ_OTA_HEADER_SIZE + (size_t)S3_OTA_MAX_IMAGE_SIZE) {
        return result(S3_OTA_HTTP_PAYLOAD_TOO_LARGE,
                      "Signed OTA bundle is too large");
    }

    uint8_t *buffer = malloc(S3_OTA_HTTP_BUFFER_SIZE);
    if (!buffer) return result(S3_OTA_HTTP_INTERNAL_ERROR, "No memory");

    s3_ota_upload_guard_t guard;
    s3_ota_upload_guard_init(&guard, request->now_ms(request->ctx));
    s3_ota_upload_guard_result_t guard_result = S3_OTA_UPLOAD_GUARD_OK;
    uint8_t manifest_header[DDJ_OTA_HEADER_SIZE];
    size_t manifest_received = 0u;
    while (manifest_received < sizeof(manifest_header)) {
        int received = guarded_recv(request,
                                    manifest_header + manifest_received,
                                    sizeof(manifest_header) - manifest_received,
                                    &guard, &guard_result);
        if (received <= 0) {
            free(buffer);
            return receive_failure(false, received, guard_result);
        }
        manifest_received += (size_t)received;
    }

    ddj_ota_manifest_t manifest;
    ddj_ota_manifest_result_t manifest_result = ddj_ota_manifest_parse(
        manifest_header, sizeof(manifest_header), DDJ_OTA_TARGET_S3,
        S3_OTA_ESP32S3_CHIP_ID, "control-board-s3", S3_OTA_MAX_IMAGE_SIZE,
        &manifest);
    if (manifest_result != DDJ_OTA_MANIFEST_OK) {
        free(buffer);
        return result(S3_OTA_HTTP_BAD_REQUEST,
                      ddj_ota_manifest_result_name(manifest_result));
    }
    if (!ddj_ota_manifest_verify_signature(manifest_header,
                                           sizeof(manifest_header))) {
        free(buffer);
        return result(S3_OTA_HTTP_FORBIDDEN,
                      "Invalid OTA manifest signature");
    }
    if (request->content_len != DDJ_OTA_HEADER_SIZE + manifest.image_size) {
        free(buffer);
        return result(S3_OTA_HTTP_BAD_REQUEST,
                      "Bundle length does not match signed manifest");
    }

    size_t remaining = manifest.image_size;
    size_t buffered = 0u;
    while (buffered < S3_OTA_IMAGE_HEADER_SIZE) {
        size_t capacity = S3_OTA_HTTP_BUFFER_SIZE - buffered;
        size_t wanted = remaining < capacity ? remaining : capacity;
        if (wanted == 0u) {
            free(buffer);
            return result(S3_OTA_HTTP_BAD_REQUEST,
                          "Not an ESP32-S3 firmware image");
        }
        int received = guarded_recv(request, buffer + buffered, wanted,
                                    &guard, &guard_result);
        if (received <= 0) {
            free(buffer);
            return receive_failure(false, received, guard_result);
        }
        buffered += (size_t)received;
        remaining -= (size_t)received;
    }
    if (!s3_ota_policy_header_valid(buffer, buffered)) {
        free(buffer);
        return result(S3_OTA_HTTP_BAD_REQUEST,
                      "Not an ESP32-S3 firmware image");
    }

    esp_err_t rc = s3_ota_begin(&manifest);
    if (rc != ESP_OK) {
        free(buffer);
        return result(rc == ESP_ERR_INVALID_STATE ? S3_OTA_HTTP_CONFLICT
                                                  : S3_OTA_HTTP_BAD_REQUEST,
                      esp_err_to_name(rc));
    }
    rc = s3_ota_write(buffer, buffered);
    if (rc != ESP_OK) {
        free(buffer);
        return result(S3_OTA_HTTP_INTERNAL_ERROR, "Flash write failed");
    }

    while (remaining > 0u) {
        size_t wanted = remaining < S3_OTA_HTTP_BUFFER_SIZE
                            ? remaining : S3_OTA_HTTP_BUFFER_SIZE;
        int received = guarded_recv(request, buffer, wanted,
                                    &guard, &guard_result);
        if (received <= 0) {
            free(buffer);
            return receive_failure(true, received, guard_result);
        }
        rc = s3_ota_write(buffer, (size_t)received);
        if (rc != ESP_OK) {
            free(buffer);
            return result(S3_OTA_HTTP_INTERNAL_ERROR, "Flash write failed");
        }
        remaining -= (size_t)received;
    }
    free(buffer);

    rc = s3_ota_finish();
    if (rc != ESP_OK) {
        return result(S3_OTA_HTTP_BAD_REQUEST, "Firmware validation failed");
    }
    return result(S3_OTA_HTTP_OK, "ok");
}
