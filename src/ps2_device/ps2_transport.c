#include "ps2_transport.h"

#include <stddef.h>
#include "hardware/gpio.h"
#include "pico/platform.h"

#include "psxSPI.pio.h"

#define SEL_OFFSET 2u   // SEL = pin_base + 2

// Registry so the shared SEL ISR can map a GPIO back to its transport. Small
// fixed set (one per port). Registered in ps2_transport_init (core0).
#define MAX_TRANSPORTS 2
static ps2_transport_t *s_registry[MAX_TRANSPORTS];
static size_t s_registry_n;

bool ps2_try_recv_cmd(ps2_transport_t *t, uint8_t *out) {
    if (pio_sm_is_rx_fifo_empty(t->pio, t->sm_cmd))
        return false;
    *out = (uint8_t)(pio_sm_get(t->pio, t->sm_cmd) >> 24);
    return true;
}

bool ps2_try_send(ps2_transport_t *t, uint8_t byte) {
    if (pio_sm_is_tx_fifo_full(t->pio, t->sm_dat))
        return false;
    pio_sm_put(t->pio, t->sm_dat, ~(uint32_t)byte & 0xFFu);
    return true;
}

void __time_critical_func(ps2_restart_pio)(ps2_transport_t *t) {
    const uint32_t mask = (1u << t->sm_cmd) | (1u << t->sm_dat);
    pio_set_sm_mask_enabled(t->pio, mask, false);
    pio_restart_sm_mask(t->pio, mask);
    pio_sm_exec(t->pio, t->sm_cmd, pio_encode_jmp(t->off_cmd));
    pio_sm_exec(t->pio, t->sm_dat, pio_encode_jmp(t->off_dat));
    pio_sm_clear_fifos(t->pio, t->sm_cmd);
    pio_sm_drain_tx_fifo(t->pio, t->sm_dat);
    pio_enable_sm_mask_in_sync(t->pio, mask);
}

// Shared SEL-rising ISR (core0). Routes by GPIO to the owning transport's hook.
// Touches no PIO — core1 stays the sole PIO owner.
static void __time_critical_func(sel_isr)(uint gpio, uint32_t events) {
    if (!(events & GPIO_IRQ_EDGE_RISE))
        return;
    for (size_t i = 0; i < s_registry_n; i++) {
        ps2_transport_t *t = s_registry[i];
        if (gpio == t->pin_base + SEL_OFFSET) {
            if (t->sel_hook)
                t->sel_hook();
            return;
        }
    }
}

void ps2_transport_set_sel_hook(ps2_transport_t *t, void (*hook)(void)) {
    t->sel_hook = hook;
}

void ps2_transport_enable_sel(ps2_transport_t *t, bool enabled) {
    gpio_set_irq_enabled(t->pin_base + SEL_OFFSET, GPIO_IRQ_EDGE_RISE, enabled);
}

void ps2_transport_global_init(void) {
    // Register the shared callback once with a disabled dummy pin; per-port pins
    // are enabled later via ps2_transport_enable_sel. Using the SDK shared
    // handler means it ACKs the IRQ for us.
    gpio_set_irq_callback(sel_isr);
    irq_set_enabled(IO_IRQ_BANK0, true);
}

void ps2_transport_init(ps2_transport_t *t, PIO pio, uint pin_base) {
    t->pio = pio;
    t->pin_base = pin_base;
    t->sel_hook = NULL;

    for (uint i = 0; i < 5; i++) {          // DAT..ACK as inputs, no pulls
        gpio_set_dir(pin_base + i, GPIO_IN);
        gpio_disable_pulls(pin_base + i);
    }

    t->sm_cmd = (uint)pio_claim_unused_sm(pio, true);
    t->sm_dat = (uint)pio_claim_unused_sm(pio, true);
    t->off_cmd = (uint)pio_add_program(pio, &cmd_reader_program);
    t->off_dat = (uint)pio_add_program(pio, &dat_writer_program);
    cmd_reader_program_init(pio, t->sm_cmd, t->off_cmd, pin_base);
    dat_writer_program_init(pio, t->sm_dat, t->off_dat, pin_base);

    // Fast, strong DAT drive for a clean falling edge.
    gpio_set_slew_rate(pin_base + 0, GPIO_SLEW_RATE_FAST);
    gpio_set_drive_strength(pin_base + 0, GPIO_DRIVE_STRENGTH_12MA);

    if (s_registry_n < MAX_TRANSPORTS)
        s_registry[s_registry_n++] = t;
}
