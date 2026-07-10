#include "ps2_device.h"

#include "pico/multicore.h"
#include "pico/stdlib.h"     // tight_loop_contents()
#include "hardware/pio.h"

#include "ds2_protocol.h"
#include "ps2_transport.h"
#include "shared_input.h"

#define PS2_ADDR_CONTROLLER 0x01u   // 0x81 = memory card, ignored (stay Hi-Z)
#define RESP_CAP 32
#define REQ_CAP  32

// Per-port state. port 0 = pio0/GP5-9, port 1 = pio1/GP10-14.
static ps2_transport_t  s_transport[PS2_NUM_PORTS];
static ds2_state_t      s_ds2[PS2_NUM_PORTS];
static volatile bool    s_active[PS2_NUM_PORTS];
static volatile bool    s_restart[PS2_NUM_PORTS];   // set by SEL ISR (core0)

static void ps2_signal_restart_p0(void) { s_restart[0] = true; }
static void ps2_signal_restart_p1(void) { s_restart[1] = true; }
static void (*const s_restart_hook[PS2_NUM_PORTS])(void) = {
    ps2_signal_restart_p0, ps2_signal_restart_p1,
};

// Wait for one CMD byte on `port`, bailing if its transaction ended (SEL rise)
// OR the port was stopped (s_active cleared by ps2_device_stop). Checking
// s_active is essential: stop() disarms the SEL IRQ, so once it fires nothing
// can ever set s_restart again — without this guard a stop racing an in-flight
// transaction would spin core1 forever and starve both ports. core1.
static bool recv_cmd(unsigned port, uint8_t *out) {
    while (!s_restart[port] && s_active[port]) {
        if (ps2_try_recv_cmd(&s_transport[port], out))
            return true;
        tight_loop_contents();
    }
    return false;
}

static bool send_dat(unsigned port, uint8_t byte) {
    while (!s_restart[port] && s_active[port]) {
        if (ps2_try_send(&s_transport[port], byte))
            return true;
        tight_loop_contents();
    }
    return false;
}

// Continue a transaction on `port` after its address byte was already read.
static void process_transaction(unsigned port, uint8_t addr) {
    if (addr != PS2_ADDR_CONTROLLER)
        return;                                  // 0x81 memcard: stay Hi-Z

    ds2_state_t *st = &s_ds2[port];
    if (!send_dat(port, ds2_id_byte(st)))
        return;
    uint8_t cmd;
    if (!recv_cmd(port, &cmd))
        return;

    PSXInputState in = shared_input_snapshot(port);
    uint8_t resp[RESP_CAP], req[REQ_CAP];
    size_t rn = ds2_response(st, cmd, &in, NULL, 0, resp, sizeof resp);

    size_t ri = 0;
    for (size_t i = 1; i < rn; i++) {
        if (!send_dat(port, resp[i]))
            return;
        uint8_t rx;
        if (!recv_cmd(port, &rx))
            return;
        if (ri < sizeof req)
            req[ri++] = rx;
    }
    ds2_apply_request(st, cmd, req, ri);
}

// Single core1 loop for all ports. The PS2 SIO selects ports sequentially, so
// at most one port has an in-flight transaction; we poll both and service the
// one whose address byte arrived, then re-sync that port after its SEL rise.
static void ps2_device_thread(void) {
    multicore_lockout_victim_init();

    for (;;) {
        for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
            if (!s_active[p])
                continue;

            uint8_t addr;
            if (!ps2_try_recv_cmd(&s_transport[p], &addr))
                continue;                        // no transaction on this port now

            s_restart[p] = false;
            process_transaction(p, addr);

            // Wait for the console to finish (SEL rise) or the port to go
            // inactive, then re-sync this port's PIO on core1.
            while (!s_restart[p] && s_active[p])
                tight_loop_contents();
            ps2_restart_pio(&s_transport[p]);
            s_restart[p] = false;
        }
    }
}

void ps2_device_global_init(void) {
    ps2_transport_global_init();
    ps2_transport_init(&s_transport[0], pio0, 5);
    ps2_transport_init(&s_transport[1], pio1, 10);
    for (unsigned p = 0; p < PS2_NUM_PORTS; p++) {
        ps2_transport_set_sel_hook(&s_transport[p], s_restart_hook[p]);
        s_active[p] = false;
        s_restart[p] = false;
    }
    multicore_launch_core1(ps2_device_thread);   // one-time; services all ports
}

void ps2_device_start(unsigned port) {
    ds2_init(&s_ds2[port]);
    s_restart[port] = false;
    ps2_transport_enable_sel(&s_transport[port], true);
    s_active[port] = true;
}

void ps2_device_stop(unsigned port) {
    s_active[port] = false;
    ps2_transport_enable_sel(&s_transport[port], false);
    PSXInputState neutral = ds2_neutral_state();
    shared_input_publish(port, &neutral);
}
