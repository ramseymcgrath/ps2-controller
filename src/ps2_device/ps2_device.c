#include "ps2_device.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"     // tight_loop_contents()

#include "ds2_protocol.h"
#include "ps2_transport.h"
#include "shared_input.h"

#define PS2_ADDR_CONTROLLER 0x01u   // 0x81 = memory card, ignored (stay Hi-Z)

static ds2_state_t s_state;

// Set by the SEL-rising ISR (core0) at each transaction boundary; polled by the
// core1 loop below to abandon the in-flight transaction and re-sync at the
// address gate. This replaces per-transaction multicore_reset_core1() +
// multicore_launch_core1(): those SDK calls busy-wait unbounded and run a FIFO
// handshake with only the SIO-FIFO IRQ masked, so re-entering them from the SEL
// ISR (which fires every transaction) can corrupt an in-flight handshake and
// hang core0 — which also runs Bluetooth. core1 is launched once per connection.
// The PIO SMs are re-synced by ps2_restart_pio() in the ISR; this flag only
// aborts core1's software position so it stops emitting a stale response.
static volatile bool s_restart;

static void ps2_signal_restart(void) {
    s_restart = true;
}

// Wait for one CMD byte, but bail out if a transaction restart was signaled.
// Returns false when aborted. Non-blocking poll so a mid-transaction wait can
// never hang past the current transaction.
static bool recv_cmd(uint8_t *out) {
    while (!s_restart) {
        if (ps2_try_recv_cmd(out))
            return true;
        tight_loop_contents();
    }
    return false;
}

// Send one DAT byte, bailing out if a restart was signaled. false => aborted.
static bool send_dat(uint8_t byte) {
    while (!s_restart) {
        if (ps2_try_send(byte))
            return true;
        tight_loop_contents();
    }
    return false;
}

void ps2_device_thread(void) {
    // Re-arm cross-core lockout for this core. core1 stays running for the whole
    // connection; ps2_device_start() initialises s_state once before launch, so
    // the config/mode handshake state persists across transactions.
    multicore_lockout_victim_init();

    uint8_t resp[32];
    uint8_t req[32];

    while (true) {
        s_restart = false;   // begin a fresh transaction attempt

        uint8_t addr;
        if (!recv_cmd(&addr))            continue;   // aborted -> resync
        if (addr != PS2_ADDR_CONTROLLER) continue;   // not the controller address

        // The ID byte is shifted out on DAT during the same exchange in which
        // the console shifts the command byte in, so it must be queued before we
        // know the command. It equals resp[0] from ds2_response().
        if (!send_dat(ds2_id_byte(&s_state))) continue;
        uint8_t cmd;
        if (!recv_cmd(&cmd))                  continue;

        // Tear-free snapshot of the latest mapped controller state (core0).
        PSXInputState in = shared_input_snapshot();

        // resp[0] is the ID already sent; resp[1..] is 0x5A + payload. req is
        // unknown at build time, so offset-dependent descriptor commands
        // (0x46/0x4C) serve their offset-0 form here — a known limitation to
        // reconcile on the bench (see docs/bringup.md).
        size_t rn = ds2_response(&s_state, cmd, &in, NULL, 0, resp, sizeof resp);

        // Stream the remaining response bytes, capturing the console's request
        // bytes on the same exchanges for ds2_apply_request().
        size_t ri = 0;
        bool complete = true;
        for (size_t i = 1; i < rn; i++) {
            if (!send_dat(resp[i])) { complete = false; break; }
            uint8_t rx;
            if (!recv_cmd(&rx))     { complete = false; break; }
            if (ri < sizeof req)
                req[ri++] = rx;
        }

        // Only mutate protocol state for a transaction that ran to completion;
        // a restart mid-frame means the console aborted, so drop it.
        if (complete && !s_restart)
            ds2_apply_request(&s_state, cmd, req, ri);
    }
}

void ps2_device_start(void) {
    ds2_init(&s_state);
    s_restart = false;
    ps2_transport_set_sel_hook(ps2_signal_restart);
    ps2_transport_enable_sel(true);
    multicore_launch_core1(ps2_device_thread);   // one-time launch for the connection
}

void ps2_device_stop(void) {
    ps2_transport_enable_sel(false);     // stop further SEL ISRs first
    ps2_transport_set_sel_hook(NULL);
    multicore_reset_core1();             // one-time teardown (not per-transaction)
    // Present a centered, all-released pad rather than a dropout.
    PSXInputState neutral = ds2_neutral_state();
    shared_input_publish(&neutral);
}
