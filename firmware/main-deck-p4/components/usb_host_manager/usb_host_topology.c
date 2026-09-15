/* SPDX-License-Identifier: Apache-2.0 */
#include "usb_host_topology.h"

#include <string.h>

void usb_host_topology_init(usb_host_topology_t *topology)
{
    if (topology) {
        memset(topology, 0, sizeof(*topology));
    }
}

bool usb_host_topology_observe(usb_host_topology_t *topology,
                               uint8_t address,
                               bool direct_root_child,
                               uint8_t root_port_index)
{
    if (!topology || address == 0u ||
        address >= USB_HOST_TOPOLOGY_ADDRESS_COUNT) {
        return false;
    }
    topology->entries[address] = (usb_host_topology_entry_t) {
        .observed = true,
        .present = true,
        .direct_root_child = direct_root_child,
        .root_port_index = root_port_index,
    };
    return true;
}

bool usb_host_topology_remove(usb_host_topology_t *topology, uint8_t address)
{
    if (!topology || address == 0u ||
        address >= USB_HOST_TOPOLOGY_ADDRESS_COUNT) {
        return false;
    }
    usb_host_topology_entry_t *entry = &topology->entries[address];
    entry->observed = true;
    entry->present = false;
    entry->direct_root_child = false;
    entry->root_port_index = 0u;
    return true;
}

bool usb_host_topology_matches_root(const usb_host_topology_t *topology,
                                    uint8_t address,
                                    uint8_t root_port_index,
                                    bool require_direct_root,
                                    bool *known_out)
{
    if (known_out) {
        *known_out = false;
    }
    if (!topology || address == 0u ||
        address >= USB_HOST_TOPOLOGY_ADDRESS_COUNT) {
        return false;
    }
    const usb_host_topology_entry_t *entry = &topology->entries[address];
    if (!entry->observed || !entry->present) {
        return false;
    }
    if (known_out) {
        *known_out = true;
    }
    return entry->root_port_index == root_port_index &&
           (!require_direct_root || entry->direct_root_child);
}
