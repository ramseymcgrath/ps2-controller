#ifndef PS2_TRANSPORT_H
#define PS2_TRANSPORT_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/pio.h"

// One PS2 controller-port bus instance. DAT/CMD/SEL/CLK/ACK are consecutive
// GPIOs starting at pin_base. After init, all FIFO access is core1-only.
typedef struct {
    PIO  pio;
    uint sm_cmd;
    uint sm_dat;
    uint off_cmd;
    uint off_dat;
    uint pin_base;               // DAT; CMD=+1, SEL=+2, CLK=+3, ACK=+4
    void (*sel_hook)(void);
} ps2_transport_t;

// Register the shared SEL-rising GPIO callback once (core0), before any init.
void ps2_transport_global_init(void);

// Claim 2 SMs on `pio` and configure them for `pin_base`. The program is added
// to each PIO block on first use.
void ps2_transport_init(ps2_transport_t *t, PIO pio, uint pin_base);

bool ps2_try_recv_cmd(ps2_transport_t *t, uint8_t *out);   // core1
bool ps2_try_send(ps2_transport_t *t, uint8_t byte);       // core1
void ps2_restart_pio(ps2_transport_t *t);                  // core1, between transactions

void ps2_transport_set_sel_hook(ps2_transport_t *t, void (*hook)(void));
void ps2_transport_enable_sel(ps2_transport_t *t, bool enabled);

#endif // PS2_TRANSPORT_H
