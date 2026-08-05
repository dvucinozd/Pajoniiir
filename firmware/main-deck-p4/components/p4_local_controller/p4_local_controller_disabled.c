/* SPDX-License-Identifier: Apache-2.0 */
#include "p4_local_controller.h"

#include <string.h>

bool p4_local_controller_enabled(void)
{
    return false;
}

void p4_local_controller_get_diagnostics(
    p4_local_controller_diagnostics_t *diag_out)
{
    if (diag_out) {
        memset(diag_out, 0, sizeof(*diag_out));
    }
}
