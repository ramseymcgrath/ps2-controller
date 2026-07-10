#ifndef PORT_ROUTER_H
#define PORT_ROUTER_H

// Connection-order assignment of Bluetooth controllers to PS2 ports.
// Pure and host-testable: devices are opaque non-NULL pointers (NULL = empty).

#define PS2_NUM_PORTS 2
#define PORT_NONE (-1)

typedef struct {
    const void *owner[PS2_NUM_PORTS];
} port_router_t;

void port_router_init(port_router_t *r);

// Assign dev to the lowest free port. If dev is already assigned, returns its
// existing port. Returns PORT_NONE if all ports are taken. NULL dev -> PORT_NONE.
int port_router_assign(port_router_t *r, const void *dev);

// Return the port dev is assigned to, or PORT_NONE.
int port_router_lookup(const port_router_t *r, const void *dev);

// Free whatever port dev holds (no-op if dev is unassigned or NULL).
void port_router_release(port_router_t *r, const void *dev);

#endif // PORT_ROUTER_H
