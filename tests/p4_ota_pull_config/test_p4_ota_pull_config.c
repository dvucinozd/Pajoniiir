#include "p4_ota_pull_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill(char *buf, size_t n, char c)
{
    memset(buf, c, n);
    buf[n] = '\0';
}

static void test_ssid_bounds(void)
{
    char buf[64];
    assert(p4_ota_cfg_check_ssid("ZAKLJUCANO") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_ssid("") == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_ssid(NULL) == P4_OTA_CFG_EMPTY);

    fill(buf, P4_OTA_CFG_SSID_MAX, 'a');
    assert(p4_ota_cfg_check_ssid(buf) == P4_OTA_CFG_OK);
    fill(buf, P4_OTA_CFG_SSID_MAX + 1u, 'a');
    assert(p4_ota_cfg_check_ssid(buf) == P4_OTA_CFG_TOO_LONG);

    assert(p4_ota_cfg_check_ssid("bad\tname") == P4_OTA_CFG_BAD_CHARS);
    /* UTF-8 SSIDs are legal and must not be rejected as "bad characters". */
    assert(p4_ota_cfg_check_ssid("Kavana \xc4\x8c\xc5\xa0\xc5\xbd") == P4_OTA_CFG_OK);
}

/* The case that matters: a too-short passphrase is accepted by every layer
 * above and then simply never associates, which looks like a firmware fault on
 * a deck with no console. */
static void test_password_enforces_wpa2_bounds(void)
{
    char buf[80];
    assert(p4_ota_cfg_check_password("12345678") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_password("1234567") == P4_OTA_CFG_TOO_SHORT);
    assert(p4_ota_cfg_check_password("") == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_password(NULL) == P4_OTA_CFG_EMPTY);

    fill(buf, P4_OTA_CFG_PASS_MAX, 'x');
    assert(p4_ota_cfg_check_password(buf) == P4_OTA_CFG_OK);

    /* Exactly 64 chars is a raw PSK, and must be hex to be one. */
    fill(buf, P4_OTA_CFG_PSK_HEX, 'a');
    assert(p4_ota_cfg_check_password(buf) == P4_OTA_CFG_OK);
    fill(buf, P4_OTA_CFG_PSK_HEX, 'z');
    assert(p4_ota_cfg_check_password(buf) == P4_OTA_CFG_BAD_CHARS);

    fill(buf, P4_OTA_CFG_PSK_HEX + 1u, 'a');
    assert(p4_ota_cfg_check_password(buf) == P4_OTA_CFG_TOO_LONG);

    assert(p4_ota_cfg_check_password("has\nnewline!") == P4_OTA_CFG_BAD_CHARS);
}

static void test_url_requires_https_and_a_host(void)
{
    assert(p4_ota_cfg_check_url("https://pajoniiir.zadar.click/ota") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_url("https://host") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_url("http://pajoniiir.zadar.click/ota") == P4_OTA_CFG_NOT_HTTPS);
    assert(p4_ota_cfg_check_url("pajoniiir.zadar.click/ota") == P4_OTA_CFG_NOT_HTTPS);
    assert(p4_ota_cfg_check_url("https://") == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_url("https:///ota") == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_url("") == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_url(NULL) == P4_OTA_CFG_EMPTY);
    assert(p4_ota_cfg_check_url("https://ho st/ota") == P4_OTA_CFG_BAD_CHARS);
}

/* Credentials in the URL would end up in logs and in status output, which is
 * precisely what must never happen with this configuration. */
static void test_url_refuses_embedded_credentials(void)
{
    assert(p4_ota_cfg_check_url("https://user:pass@host/ota") ==
           P4_OTA_CFG_URL_HAS_CREDENTIALS);
    assert(p4_ota_cfg_check_url("https://user@host/ota") ==
           P4_OTA_CFG_URL_HAS_CREDENTIALS);
    /* An '@' after the authority is just a path character and is fine. */
    assert(p4_ota_cfg_check_url("https://host/ota@v2") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_url("https://host/ota?x=a@b") == P4_OTA_CFG_OK);
    assert(p4_ota_cfg_check_url("https://host#frag@x") == P4_OTA_CFG_OK);
}

static void test_url_length_bound(void)
{
    char buf[256];
    int n = snprintf(buf, sizeof(buf), "https://h/");
    while ((size_t)n < P4_OTA_CFG_URL_MAX) buf[n++] = 'a';
    buf[n] = '\0';
    assert(p4_ota_cfg_check_url(buf) == P4_OTA_CFG_OK);
    buf[n++] = 'a'; buf[n] = '\0';
    assert(p4_ota_cfg_check_url(buf) == P4_OTA_CFG_TOO_LONG);
}

static void test_result_names_exist(void)
{
    assert(strcmp(p4_ota_cfg_result_name(P4_OTA_CFG_OK), "ok") == 0);
    assert(strcmp(p4_ota_cfg_result_name(P4_OTA_CFG_TOO_SHORT), "too-short") == 0);
    assert(strcmp(p4_ota_cfg_result_name(P4_OTA_CFG_URL_HAS_CREDENTIALS),
                  "url-has-credentials") == 0);
}

int main(void)
{
    test_ssid_bounds();
    test_password_enforces_wpa2_bounds();
    test_url_requires_https_and_a_host();
    test_url_refuses_embedded_credentials();
    test_url_length_bound();
    test_result_names_exist();
    puts("p4_ota_pull_config tests passed");
    return 0;
}
