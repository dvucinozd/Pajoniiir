#include "wifi_link_retry.h"

/* 1 s then 2 s. There is no third entry: the third failure ends the sequence,
 * so a "4 s" delay would only ever be waited before giving up anyway. */
static const uint32_t k_backoff_ms[WIFI_LINK_RETRY_MAX_ATTEMPTS - 1u] = {
    1000u,
    2000u,
};

void wifi_link_retry_reset(wifi_link_retry_t *r)
{
    if (!r) return;
    r->attempts = 0u;
}

uint32_t wifi_link_retry_note_failure(wifi_link_retry_t *r)
{
    if (!r) return 0u;
    if (r->attempts < WIFI_LINK_RETRY_MAX_ATTEMPTS) {
        r->attempts++;
    }
    if (r->attempts >= WIFI_LINK_RETRY_MAX_ATTEMPTS) {
        return 0u;   /* spent — the caller restores instead of retrying */
    }
    return k_backoff_ms[r->attempts - 1u];
}

bool wifi_link_retry_exhausted(const wifi_link_retry_t *r)
{
    return r && r->attempts >= WIFI_LINK_RETRY_MAX_ATTEMPTS;
}

uint8_t wifi_link_retry_attempts(const wifi_link_retry_t *r)
{
    return r ? r->attempts : 0u;
}
