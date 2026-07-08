#include "ps2_device.h"

#include "pico/multicore.h"

#include "ds2_protocol.h"
#include "ps2_transport.h"
#include "shared_input.h"

#define PS2_ADDR_CONTROLLER 0x01u   // 0x81 = memory card, ignored (stay Hi-Z)

static ds2_state_t s_state;

void ps2_device_thread(void) {
    // Let core0 pause this core for flash ops (e.g. BT key storage).
    // No ds2_init() here: the SEL ISR relaunches this thread at every
    // transaction boundary, so the protocol state (config/mode) must persist
    // across relaunches. ps2_device_start() initialises s_state once on connect.
    multicore_lockout_victim_init();

    uint8_t resp[32];
    uint8_t req[32];

    while (true) {
        // Address byte. Only the controller address is ours.
        if (ps2_recv_cmd() != PS2_ADDR_CONTROLLER)
            continue;

        // The ID byte is shifted out on DAT during the same exchange in which
        // the console shifts the command byte in, so it must be queued before
        // we know the command. It equals resp[0] from ds2_response().
        ps2_send(ds2_id_byte(&s_state));
        uint8_t cmd = ps2_recv_cmd();

        // Tear-free snapshot of the latest mapped controller state (core0).
        PSXInputState in = shared_input_snapshot();

        // Build the frame. resp[0] is the ID already sent; resp[1..] is
        // 0x5A + payload. req is unknown at build time, so offset-dependent
        // descriptor commands (0x46/0x4C) serve their offset-0 form here —
        // a known limitation to reconcile on the bench (see docs/bringup.md).
        size_t rn = ds2_response(&s_state, cmd, &in, NULL, 0, resp, sizeof resp);

        // Stream the remaining response bytes, capturing the console's request
        // bytes on the same exchanges for ds2_apply_request().
        size_t ri = 0;
        for (size_t i = 1; i < rn; i++) {
            ps2_send(resp[i]);
            uint8_t rx = ps2_recv_cmd();
            if (ri < sizeof req)
                req[ri++] = rx;
        }

        ds2_apply_request(&s_state, cmd, req, ri);
    }
}

// SEL-rising ISR hook (runs on core0): abort any mid-transaction blocked read
// on core1 and restart the loop at the address gate for the next transaction.
// s_state is a persistent static, so config/mode survive the relaunch.
static void ps2_restart_core1(void) {
    multicore_reset_core1();
    multicore_launch_core1(ps2_device_thread);
}

void ps2_device_start(void) {
    ds2_init(&s_state);
    ps2_transport_set_sel_hook(ps2_restart_core1);
    ps2_transport_enable_sel(true);
    multicore_launch_core1(ps2_device_thread);
}

void ps2_device_stop(void) {
    ps2_transport_enable_sel(false);     // stop further SEL ISRs first
    ps2_transport_set_sel_hook(NULL);
    multicore_reset_core1();
    // Present a centered, all-released pad rather than a dropout.
    PSXInputState neutral = ds2_neutral_state();
    shared_input_publish(&neutral);
}
