#pragma once
#include "dj_link_discovery.h"
#include "esp_err.h"

/* After NVS/common P4 startup. Default-off worker owns all network I/O and model.
 * Only the JC1060 target links this component. No Web/USB/audio callbacks here. */
esp_err_t dj_link_service_init(void);
/* Nonblocking copy; false if unavailable or a publication is underway. */
bool dj_link_service_snapshot(dj_link_discovery_t *out);
/* Small nonblocking status for Settings, including observer/error reasons. */
void dj_link_service_format_status(char *out, size_t cap);
