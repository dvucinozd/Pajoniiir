#include "controller_output_policy.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    assert(controller_output_select_route(true, true, false, true) ==
           CONTROLLER_OUTPUT_DYNAMIC);
    assert(controller_output_select_route(true, false, true, true) ==
           CONTROLLER_OUTPUT_DROP);
    assert(controller_output_select_route(true, false, false, true) ==
           CONTROLLER_OUTPUT_DROP);
    assert(controller_output_select_route(false, false, true, true) ==
           CONTROLLER_OUTPUT_BUILTIN_FLX4);
    assert(controller_output_select_route(false, false, false, true) ==
           CONTROLLER_OUTPUT_DROP);
    assert(controller_output_select_route(false, false, true, false) ==
           CONTROLLER_OUTPUT_DROP);
    puts("controller_output_policy tests passed");
    return 0;
}
