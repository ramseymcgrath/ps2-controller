#include "port_router.h"
#include <stddef.h>

void port_router_init(port_router_t *r) {
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        r->owner[i] = NULL;
}

int port_router_lookup(const port_router_t *r, const void *dev) {
    if (dev == NULL)
        return PORT_NONE;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == dev)
            return i;
    return PORT_NONE;
}

int port_router_assign(port_router_t *r, const void *dev) {
    if (dev == NULL)
        return PORT_NONE;
    int existing = port_router_lookup(r, dev);
    if (existing != PORT_NONE)
        return existing;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == NULL) {
            r->owner[i] = dev;
            return i;
        }
    return PORT_NONE;
}

void port_router_release(port_router_t *r, const void *dev) {
    if (dev == NULL)
        return;
    for (int i = 0; i < PS2_NUM_PORTS; i++)
        if (r->owner[i] == dev)
            r->owner[i] = NULL;
}
