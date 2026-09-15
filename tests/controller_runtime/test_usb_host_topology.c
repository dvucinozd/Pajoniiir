#include "usb_host_topology.h"

#include <stdio.h>
#include <stdlib.h>

static unsigned checks;
#define CHECK(x) do { checks++; if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); exit(1); \
} } while (0)

int main(void)
{
    usb_host_topology_t topology;
    usb_host_topology_init(&topology);

    bool known = true;
    CHECK(!usb_host_topology_matches_root(&topology, 4u, 0u, true, &known));
    CHECK(!known);
    CHECK(!usb_host_topology_observe(&topology, 0u, true, 0u));
    CHECK(usb_host_topology_observe(&topology, 4u, true, 0u));
    CHECK(usb_host_topology_matches_root(&topology, 4u, 0u, true, &known));
    CHECK(known);
    CHECK(!usb_host_topology_matches_root(&topology, 4u, 1u, true, &known));
    CHECK(known);

    CHECK(usb_host_topology_observe(&topology, 7u, false, 1u));
    CHECK(usb_host_topology_matches_root(&topology, 7u, 1u, false, &known));
    CHECK(!usb_host_topology_matches_root(&topology, 7u, 1u, true, &known));
    CHECK(known);

    CHECK(usb_host_topology_remove(&topology, 4u));
    CHECK(!usb_host_topology_matches_root(&topology, 4u, 0u, true, &known));
    CHECK(!known);
    CHECK(usb_host_topology_observe(&topology, 4u, true, 1u));
    CHECK(usb_host_topology_matches_root(&topology, 4u, 1u, true, &known));

    printf("USB host topology: %u checks passed\n", checks);
    return 0;
}
