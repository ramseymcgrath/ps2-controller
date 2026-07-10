#ifndef SHARED_INPUT_H
#define SHARED_INPUT_H
#include <stdbool.h>
#include "input_state.h"
#include "port_router.h"   // PS2_NUM_PORTS

// Per-port cross-core publish/snapshot (seqlock). Single writer per slot
// (core0, the BluePad32 thread) publishes the latest PSXInputState for a port;
// the reader (core1, the PS2 poll-response loop) takes a tear-free snapshot.
// `port` must be < PS2_NUM_PORTS. firmware-only (RP2350 sync barrier).

void          shared_input_init(void);                              // all slots -> neutral, disconnected
void          shared_input_publish(unsigned port, const PSXInputState *s);  // core0 writer
PSXInputState shared_input_snapshot(unsigned port);                 // core1 reader (consistent)

void shared_input_set_connected(unsigned port, bool connected);
bool shared_input_connected(unsigned port);

#endif // SHARED_INPUT_H
