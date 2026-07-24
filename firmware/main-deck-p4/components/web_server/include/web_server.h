#pragma once

/*
 * Wi-Fi connectivity-probe hooks.
 *
 * The web layer must not call wifi_link directly: wifi_link already depends on
 * web_server (it starts and stops it), so a direct call back would make the two
 * components mutually dependent. app_main owns the wiring instead, exactly as
 * it does for the Settings Wi-Fi toggle and the deck activity callback.
 *
 * The status struct is deliberately a plain copy rather than a shared type, so
 * neither component has to include the other's header.
 */
typedef struct {
    int  state;          /* 0 idle, 1 running, 2 ok, 3 failed */
    char detail[96];   /* long enough for "update available: <release>" */
    char address[16];
} web_server_probe_status_t;

typedef int  (*web_server_probe_start_fn)(int mode, const char *arg);
/* mode: 0 = link probe, 1 = update check, 2 = install `arg` (the release the
 * page saw). esp_err_t returned as int. */
typedef void (*web_server_probe_status_fn)(web_server_probe_status_t *out);

void web_server_set_probe_hooks(web_server_probe_start_fn start,
                                web_server_probe_status_fn status);

#include "esp_err.h"

esp_err_t web_server_start(void);
void web_server_stop(void);

esp_err_t dns_server_start(void);
void dns_server_stop(void);
