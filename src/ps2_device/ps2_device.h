#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

// core1 entry point (pass to multicore_launch_core1). Runs the DualShock 2
// controller-port protocol forever: gate on the 0x01 address, send the ID byte,
// stream the ds2_response() frame while capturing the console's request bytes,
// then apply them. Reads controller state via shared_input_snapshot(); drives
// the bus via ps2_transport. The SEL-rising ISR resets this thread at each
// transaction boundary. Never returns. Protocol state is NOT initialised here
// (it must survive per-transaction relaunches) — ps2_device_start() does that.
void ps2_device_thread(void);

// Bring the PS2 device online (call on controller connect, from core0):
// reset protocol state to power-on, install the SEL restart hook, enable the
// SEL IRQ, and launch core1.
void ps2_device_start(void);

// Take the PS2 device offline (call on controller disconnect, from core0):
// disable the SEL IRQ, reset core1, and present a neutral pad to the console.
void ps2_device_stop(void);

#endif // PS2_DEVICE_H
