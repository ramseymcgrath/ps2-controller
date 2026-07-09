#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

#include "port_router.h"   // PS2_NUM_PORTS

// Initialize both port transports and launch the single core1 loop that
// services all ports. Call once at startup from core0, after the system clock
// is set and before controllers connect.
void ps2_device_global_init(void);

// Bring port `port` online / offline (core0, on controller connect / disconnect):
// flips the port's active flag and (dis)arms its SEL IRQ; start also resets the
// port's DS2 protocol state, stop publishes a neutral pad. Does NOT launch or
// reset core1 (that happens once in ps2_device_global_init). `port` < PS2_NUM_PORTS.
void ps2_device_start(unsigned port);
void ps2_device_stop(unsigned port);

#endif // PS2_DEVICE_H
