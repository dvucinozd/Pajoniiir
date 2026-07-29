#include "p4_ota_pull_gate.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    p4_ota_pull_gate_t gate;
    p4_ota_pull_gate_init(&gate);
    assert(!p4_ota_pull_gate_is_claimed(&gate));

    assert(p4_ota_pull_gate_try_acquire(&gate));
    assert(p4_ota_pull_gate_is_claimed(&gate));
    assert(!p4_ota_pull_gate_try_acquire(&gate));

    /* Task-create/network failure releases the reservation for a retry. */
    p4_ota_pull_gate_release(&gate);
    assert(!p4_ota_pull_gate_is_claimed(&gate));
    assert(p4_ota_pull_gate_try_acquire(&gate));

    p4_ota_pull_gate_release(&gate);
    puts("p4_ota_pull_gate tests passed");
    return 0;
}
