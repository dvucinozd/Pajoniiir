#pragma once

#include <stdbool.h>

/* Pure control decision used by the asynchronous Wi-Fi worker.
 *
 * Probe and pull-OTA own an AP -> STA -> AP transition lease.  A Settings
 * disable request must remain pending until that lease is released; tearing
 * down ESP-Hosted while the transition task is associating, downloading or
 * restoring the AP leaves both components operating on destroyed netifs.
 */
typedef enum {
    WIFI_LINK_CONTROL_IDLE = 0,
    WIFI_LINK_CONTROL_START,
    WIFI_LINK_CONTROL_STOP,
    WIFI_LINK_CONTROL_WAIT_TRANSITION,
} wifi_link_control_action_t;

wifi_link_control_action_t wifi_link_control_next(bool desired_active,
                                                  bool currently_active,
                                                  bool transition_busy);
