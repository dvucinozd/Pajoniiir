#pragma once

#include <stdbool.h>

typedef enum {
    CONTROLLER_OUTPUT_DROP = 0,
    CONTROLLER_OUTPUT_DYNAMIC,
    CONTROLLER_OUTPUT_BUILTIN_FLX4,
} controller_output_route_t;

controller_output_route_t controller_output_select_route(
    bool dynamic_profile_active,
    bool dynamic_mapping_found,
    bool confirmed_flx4,
    bool builtin_mapping_found);
