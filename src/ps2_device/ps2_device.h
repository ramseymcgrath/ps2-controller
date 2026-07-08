#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

// core1 entry point (pass to multicore_launch_core1). Runs the DualShock 2
// controller-port protocol forever: gate on the 0x01 address, send the ID byte,
// stream the ds2_response() frame while capturing the console's request bytes,
// then apply them. Reads controller state via shared_input_snapshot(); drives
// the bus via ps2_transport. Relies on the SEL-rising ISR (Task 14 hook) to
// reset this thread at each transaction boundary. Never returns.
void ps2_device_thread(void);

#endif // PS2_DEVICE_H
