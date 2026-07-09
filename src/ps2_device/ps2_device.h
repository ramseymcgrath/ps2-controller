#ifndef PS2_DEVICE_H
#define PS2_DEVICE_H

// core1 entry point (pass to multicore_launch_core1). Runs the DualShock 2
// controller-port protocol forever: gate on the 0x01 address, send the ID byte,
// stream the ds2_response() frame while capturing the console's request bytes,
// then apply them. Reads controller state via shared_input_snapshot(); drives
// the bus via ps2_transport. Its wait loops poll a restart flag the SEL-rising
// ISR sets, so it abandons an interrupted transaction and re-syncs at the
// address gate without a core reset. Launched once per connection (never
// per-transaction). Protocol state is initialised by ps2_device_start(), not
// here, so it persists across transactions.
void ps2_device_thread(void);

// One-time init from core0 (main): register the shared SEL callback and
// initialize the port-0 transport (pio0, GP5-9). Call before controllers
// connect. (Task 4 extends this to init both ports and launch core1 once.)
void ps2_device_global_init(void);

// Bring the PS2 device online (call on controller connect, from core0):
// reset protocol state to power-on, install the SEL restart hook, enable the
// SEL IRQ, and launch core1.
void ps2_device_start(void);

// Take the PS2 device offline (call on controller disconnect, from core0):
// disable the SEL IRQ, reset core1, and present a neutral pad to the console.
void ps2_device_stop(void);

#endif // PS2_DEVICE_H
