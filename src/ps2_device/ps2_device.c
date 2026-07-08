#include "ps2_device.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"     // tight_loop_contents()

#include "ds2_protocol.h"
#include "ps2_transport.h"
#include "shared_input.h"

#define PS2_ADDR_CONTROLLER 0x01u   // 0x81 = memory card, ignored (stay Hi-Z)
#define RESP_CAP 32
#define REQ_CAP  32

static ds2_state_t s_state;

// Set by the SEL-rising ISR (core0) when the console deselects (transaction
// end); polled by the core1 loop. This is the ONLY thing the ISR does — it
// touches no PIO state — so core1 is the sole owner of the PIO FIFOs and there
// is no cross-core FIFO race. It replaces per-transaction multicore_reset/launch
// (which busy-wait unbounded in the SDK and can hang core0/Bluetooth if the ISR
// re-enters an in-flight FIFO handshake). core1 is launched once per connection.
static volatile bool s_restart;

static void ps2_signal_restart(void) {
    s_restart = true;
}

// Wait for one CMD byte, bailing out if the transaction ended (restart). Returns
// false when aborted. Non-blocking poll so a mid-transaction wait can never hang
// past the current transaction.
static bool recv_cmd(uint8_t *out) {
    while (!s_restart) {
        if (ps2_try_recv_cmd(out))
            return true;
        tight_loop_contents();
    }
    return false;
}

// Send one DAT byte, bailing out if the transaction ended. false => aborted.
static bool send_dat(uint8_t byte) {
    while (!s_restart) {
        if (ps2_try_send(byte))
            return true;
        tight_loop_contents();
    }
    return false;
}

// Handle one console transaction. Returns having either completed a full frame
// (state applied) or bailed at the first sign the transaction is not ours / was
// cut short. Reaching ds2_apply_request() means every byte was exchanged on the
// wire, so the transaction genuinely completed.
static void process_one_transaction(void) {
    uint8_t resp[RESP_CAP];
    uint8_t req[REQ_CAP];

    uint8_t addr;
    if (!recv_cmd(&addr) || addr != PS2_ADDR_CONTROLLER)
        return;

    // The ID byte goes out on DAT during the same exchange that shifts the
    // command byte in, so it is queued before we know the command.
    if (!send_dat(ds2_id_byte(&s_state)))
        return;
    uint8_t cmd;
    if (!recv_cmd(&cmd))
        return;

    PSXInputState in = shared_input_snapshot();  // tear-free snapshot from core0

    // resp[0] is the ID already sent; resp[1..] is 0x5A + payload. req is unknown
    // at build time, so offset-dependent descriptor commands (0x46/0x4C) serve
    // their offset-0 form — a known limitation to reconcile on the bench.
    size_t rn = ds2_response(&s_state, cmd, &in, NULL, 0, resp, sizeof resp);

    size_t ri = 0;
    for (size_t i = 1; i < rn; i++) {
        if (!send_dat(resp[i]))
            return;                    // cut short: do not apply a partial frame
        uint8_t rx;
        if (!recv_cmd(&rx))
            return;
        if (ri < sizeof req)
            req[ri++] = rx;
    }

    ds2_apply_request(&s_state, cmd, req, ri);
}

void ps2_device_thread(void) {
    // Re-arm cross-core lockout for this core. core1 runs continuously for the
    // whole connection; ps2_device_start() initialised s_state once before
    // launch, so config/mode persist across transactions.
    multicore_lockout_victim_init();

    for (;;) {
        s_restart = false;
        process_one_transaction();

        // Wait for the console to finish the transaction (SEL rise sets
        // s_restart via the ISR), then re-sync the PIO for the next one. Doing
        // the restart here — on core1, during the inter-transaction gap — keeps
        // core1 the sole PIO-FIFO owner and never restarts mid-transaction.
        while (!s_restart)
            tight_loop_contents();
        ps2_restart_pio();
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
