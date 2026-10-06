#include "wifi_link_control.h"

wifi_link_control_action_t wifi_link_control_next(bool desired_active,
                                                  bool currently_active,
                                                  bool transition_busy)
{
    if (desired_active == currently_active) {
        return WIFI_LINK_CONTROL_IDLE;
    }
    if (transition_busy) {
        return WIFI_LINK_CONTROL_WAIT_TRANSITION;
    }
    return desired_active ? WIFI_LINK_CONTROL_START : WIFI_LINK_CONTROL_STOP;
}
