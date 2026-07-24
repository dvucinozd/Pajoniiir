#pragma once

#include <stdbool.h>
#include <stdint.h>

/*
 * Bounded retry policy for Wi-Fi bring-up.
 *
 * Fixes a real failure mode rather than adding a feature. The worker loop runs
 * until `active == desired`; when a start fails, `active` stays false while
 * `desired` is true, so the loop retries **immediately, forever, with no
 * delay** - a hot loop repeatedly bringing up ESP-Hosted and the Wi-Fi stack,
 * which is the largest internal allocation the firmware makes. On a board that
 * already shows intermittent brownouts, that turns one failed start into a
 * self-sustaining fault.
 *
 * Policy: three attempts, waiting 1 s then 2 s between them, then give up,
 * publish an error and restore the last stable mode. A further attempt requires
 * a new operator request - retrying on its own is what caused the problem.
 *
 * Pure and host-tested: the delays are the whole point, and they are not
 * observable by watching a board that is misbehaving anyway.
 */

#define WIFI_LINK_RETRY_MAX_ATTEMPTS 3u

typedef struct {
    uint8_t attempts;   /* failures recorded so far */
} wifi_link_retry_t;

void wifi_link_retry_reset(wifi_link_retry_t *r);

/* Record a failed attempt. Returns the delay to wait before trying again, or 0
 * when the budget is spent and the caller must stop and restore. */
uint32_t wifi_link_retry_note_failure(wifi_link_retry_t *r);

bool wifi_link_retry_exhausted(const wifi_link_retry_t *r);

/* Attempts already spent, for the error the operator sees. */
uint8_t wifi_link_retry_attempts(const wifi_link_retry_t *r);
