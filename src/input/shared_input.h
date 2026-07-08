#ifndef SHARED_INPUT_H
#define SHARED_INPUT_H
#include <stdbool.h>
#include "input_state.h"

// Cross-core publish/snapshot of the mapped controller state.
//
// Single writer (core0, the BluePad32/BT thread) publishes the latest
// PSXInputState; the reader (core1, the PS2 poll-response loop) takes a
// tear-free snapshot. Implemented as a seqlock with DMB barriers rather than
// a bare `volatile` struct: an 8-byte struct is not written atomically, so a
// poll landing mid-update could otherwise serve a half-old/half-new frame.
//
// firmware-only (uses the RP2350 hardware sync barrier); not host-tested.

void          shared_input_init(void);                    // -> neutral, disconnected
void          shared_input_publish(const PSXInputState *s);  // core0 writer
PSXInputState shared_input_snapshot(void);                // core1 reader (consistent)

void shared_input_set_connected(bool connected);
bool shared_input_connected(void);

#endif // SHARED_INPUT_H
