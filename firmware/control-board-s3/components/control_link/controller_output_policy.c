#include "controller_output_policy.h"

controller_output_route_t controller_output_select_route(
    bool dynamic_profile_active,
    bool dynamic_mapping_found,
    bool confirmed_flx4,
    bool builtin_mapping_found)
{
    if (dynamic_profile_active) {
        return dynamic_mapping_found
            ? CONTROLLER_OUTPUT_DYNAMIC
            : CONTROLLER_OUTPUT_DROP;
    }
    return confirmed_flx4 && builtin_mapping_found
        ? CONTROLLER_OUTPUT_BUILTIN_FLX4
        : CONTROLLER_OUTPUT_DROP;
}
