#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "s3_ota_http.h"
#include "s3_ota_upload_guard.h"
#include "s3_ota.h"
#include "s3_ota_policy.h"

typedef struct {
    uint8_t bytes[DDJ_OTA_HEADER_SIZE + 8192u];
    size_t size;
    size_t offset;
    size_t max_chunk;
    size_t fail_at;
    unsigned timeout_budget;
    uint32_t now_ms;
    uint32_t advance_per_recv_ms;
} fake_request_t;

static ddj_ota_manifest_t s_manifest;
static ddj_ota_manifest_result_t s_parse_result;
static bool s_signature_valid;
static esp_err_t s_begin_result;
static esp_err_t s_write_result;
static unsigned s_write_fail_call;
static esp_err_t s_finish_result;
static unsigned s_begin_calls;
static unsigned s_write_calls;
static unsigned s_finish_calls;
static unsigned s_abort_calls;

static void fake_reset(fake_request_t *fake, size_t image_size)
{
    memset(fake, 0, sizeof(*fake));
    fake->size = DDJ_OTA_HEADER_SIZE + image_size;
    fake->max_chunk = SIZE_MAX;
    fake->fail_at = SIZE_MAX;
    fake->bytes[DDJ_OTA_HEADER_SIZE] = S3_OTA_ESP_IMAGE_MAGIC;
    fake->bytes[DDJ_OTA_HEADER_SIZE + S3_OTA_CHIP_ID_OFFSET] =
        (uint8_t)(S3_OTA_ESP32S3_CHIP_ID & 0xffu);
    fake->bytes[DDJ_OTA_HEADER_SIZE + S3_OTA_CHIP_ID_OFFSET + 1u] =
        (uint8_t)(S3_OTA_ESP32S3_CHIP_ID >> 8);
    memset(&s_manifest, 0, sizeof(s_manifest));
    s_manifest.target = DDJ_OTA_TARGET_S3;
    s_manifest.image_size = (uint32_t)image_size;
    strcpy(s_manifest.version, "RC-http-test");
    s_parse_result = DDJ_OTA_MANIFEST_OK;
    s_signature_valid = true;
    s_begin_result = ESP_OK;
    s_write_result = ESP_OK;
    s_write_fail_call = 0u;
    s_finish_result = ESP_OK;
    s_begin_calls = s_write_calls = s_finish_calls = s_abort_calls = 0u;
}

static int fake_recv(void *ctx, uint8_t *buffer, size_t wanted)
{
    fake_request_t *fake = ctx;
    fake->now_ms += fake->advance_per_recv_ms;
    if (fake->timeout_budget > 0u) {
        fake->timeout_budget--;
        return S3_OTA_HTTP_RECV_TIMEOUT;
    }
    if (fake->offset >= fake->fail_at) return S3_OTA_HTTP_RECV_ERROR;
    if (fake->offset >= fake->size) return S3_OTA_HTTP_RECV_ERROR;
    size_t available = fake->size - fake->offset;
    size_t count = available < wanted ? available : wanted;
    if (count > fake->max_chunk) count = fake->max_chunk;
    memcpy(buffer, fake->bytes + fake->offset, count);
    fake->offset += count;
    return (int)count;
}

static uint32_t fake_now_ms(void *ctx)
{
    return ((fake_request_t *)ctx)->now_ms;
}

static s3_ota_http_result_t run(fake_request_t *fake, size_t content_len)
{
    const s3_ota_http_request_t request = {
        .content_len = content_len,
        .ctx = fake,
        .recv = fake_recv,
        .now_ms = fake_now_ms,
    };
    return s3_ota_http_process(&request);
}

ddj_ota_manifest_result_t ddj_ota_manifest_parse(
    const uint8_t *header, size_t header_size,
    ddj_ota_target_t expected_target, uint16_t expected_chip_id,
    const char *expected_project, size_t max_image_size,
    ddj_ota_manifest_t *out_manifest)
{
    assert(header != NULL && header_size == DDJ_OTA_HEADER_SIZE);
    assert(expected_target == DDJ_OTA_TARGET_S3);
    assert(expected_chip_id == S3_OTA_ESP32S3_CHIP_ID);
    assert(strcmp(expected_project, "control-board-s3") == 0);
    assert(max_image_size == S3_OTA_MAX_IMAGE_SIZE);
    if (s_parse_result == DDJ_OTA_MANIFEST_OK) *out_manifest = s_manifest;
    return s_parse_result;
}

bool ddj_ota_manifest_verify_signature(const uint8_t *header, size_t header_size)
{
    assert(header != NULL && header_size == DDJ_OTA_HEADER_SIZE);
    return s_signature_valid;
}

const char *ddj_ota_manifest_result_name(ddj_ota_manifest_result_t result)
{
    (void)result;
    return "manifest rejected";
}

esp_err_t s3_ota_begin(const ddj_ota_manifest_t *manifest)
{
    assert(manifest != NULL);
    s_begin_calls++;
    return s_begin_result;
}

esp_err_t s3_ota_write(const void *data, size_t size)
{
    assert(data != NULL && size > 0u);
    s_write_calls++;
    if (s_write_fail_call != 0u && s_write_calls == s_write_fail_call) {
        return ESP_FAIL;
    }
    return s_write_result;
}

esp_err_t s3_ota_finish(void)
{
    s_finish_calls++;
    return s_finish_result;
}

void s3_ota_abort(const char *reason)
{
    assert(reason != NULL);
    s_abort_calls++;
}

static void test_rejects_envelope_before_flash(void)
{
    fake_request_t fake;
    fake_reset(&fake, S3_OTA_IMAGE_HEADER_SIZE);
    s3_ota_http_result_t result = run(
        &fake, DDJ_OTA_HEADER_SIZE + S3_OTA_IMAGE_HEADER_SIZE - 1u);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST);
    assert(fake.offset == 0u && s_begin_calls == 0u);

    result = run(&fake, DDJ_OTA_HEADER_SIZE + S3_OTA_MAX_IMAGE_SIZE + 1u);
    assert(result.code == S3_OTA_HTTP_PAYLOAD_TOO_LARGE);
    assert(fake.offset == 0u && s_begin_calls == 0u);

    fake_reset(&fake, S3_OTA_IMAGE_HEADER_SIZE);
    s_parse_result = DDJ_OTA_MANIFEST_BAD_MAGIC;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST && s_begin_calls == 0u);

    fake_reset(&fake, S3_OTA_IMAGE_HEADER_SIZE);
    s_signature_valid = false;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_FORBIDDEN && s_begin_calls == 0u);

    fake_reset(&fake, S3_OTA_IMAGE_HEADER_SIZE);
    s_manifest.image_size++;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST && s_begin_calls == 0u);

    fake_reset(&fake, S3_OTA_IMAGE_HEADER_SIZE);
    fake.bytes[DDJ_OTA_HEADER_SIZE] = 0u;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST && s_begin_calls == 0u);
}

static void test_fragmentation_and_success(void)
{
    fake_request_t fake;
    fake_reset(&fake, 6000u);
    fake.max_chunk = 7u;
    s3_ota_http_result_t result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_OK);
    assert(fake.offset == fake.size);
    assert(s_begin_calls == 1u && s_write_calls > 1u && s_finish_calls == 1u);
    assert(s_abort_calls == 0u);
}

static void test_receive_failures_abort_only_after_begin(void)
{
    fake_request_t fake;
    fake_reset(&fake, 6000u);
    fake.timeout_budget = 5u;
    s3_ota_http_result_t result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_TIMEOUT);
    assert(s_begin_calls == 0u && s_abort_calls == 0u);

    fake_reset(&fake, 6000u);
    fake.fail_at = DDJ_OTA_HEADER_SIZE + 100u;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST);
    assert(s_begin_calls == 1u && s_abort_calls == 1u);

    fake_reset(&fake, 6000u);
    fake.max_chunk = 128u;
    fake.advance_per_recv_ms = S3_OTA_UPLOAD_PROGRESS_WINDOW_MS;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_TIMEOUT);
    assert(s_begin_calls == 0u && s_abort_calls == 0u);
}

static void test_ota_failures_never_report_success(void)
{
    fake_request_t fake;
    fake_reset(&fake, 6000u);
    s_begin_result = ESP_ERR_INVALID_STATE;
    s3_ota_http_result_t result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_CONFLICT);
    assert(s_write_calls == 0u && s_finish_calls == 0u);

    fake_reset(&fake, 6000u);
    s_write_fail_call = 1u;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_INTERNAL_ERROR);
    /* s3_ota_write owns immediate flash/hash cleanup and preserves its precise
     * failure text; the HTTP layer must not issue a second generic abort. */
    assert(s_abort_calls == 0u && s_finish_calls == 0u);

    fake_reset(&fake, 6000u);
    s_write_fail_call = 2u;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_INTERNAL_ERROR);
    assert(s_abort_calls == 0u && s_finish_calls == 0u);

    fake_reset(&fake, 6000u);
    s_finish_result = ESP_ERR_INVALID_CRC;
    result = run(&fake, fake.size);
    assert(result.code == S3_OTA_HTTP_BAD_REQUEST);
    assert(s_finish_calls == 1u);
}

int main(void)
{
    test_rejects_envelope_before_flash();
    test_fragmentation_and_success();
    test_receive_failures_abort_only_after_begin();
    test_ota_failures_never_report_success();
    puts("s3 OTA HTTP production-core tests passed");
    return 0;
}
