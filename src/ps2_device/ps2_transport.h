#ifndef PS2_TRANSPORT_H
#define PS2_TRANSPORT_H
#include <stdint.h>
#include <stdbool.h>

// PIO-based PlayStation controller-port SPI slave on pio0.
//
// SPI Mode 3, LSB-first, 8-bit, 2.5 MHz (SLOW_CLKDIV 50 in psxSPI.pio). DATA
// and ACK are open-drain, driven via pindirs (drive-low = 0, release-to-Hi-Z
// = 1); the line is never driven high. Adapted from DS4toPS2 (GPL-3.0), whose
// psxSPI.pio came from PicoMemcard.
//
// Pins (see docs/wiring.md): DAT=5, CMD=6, SEL=7, CLK=8, ACK=9.

// Claim two pio0 state machines (cmd_reader + dat_writer), load the programs,
// configure the GPIOs, and register (disabled) the SEL-rising ISR. The
// lifecycle enables the SEL IRQ on controller connect.
void    ps2_transport_init(void);

// Non-blocking single-byte transfers on the console bus: return false
// immediately if the RX FIFO is empty / the TX FIFO is full, else perform the
// transfer and return true. core1 uses these so it can poll for a
// transaction-restart signal instead of blocking mid-transaction (see
// ps2_device.c). Call from core1 only.
bool    ps2_try_recv_cmd(uint8_t *out);
bool    ps2_try_send(uint8_t byte);

// Re-sync both state machines and their FIFOs to the start of a transaction.
// Call from core1 only (the sole PIO-FIFO owner), between transactions.
// RAM-resident (__time_critical_func) as it runs once per transaction.
void    ps2_restart_pio(void);

// Optional hook the SEL-rising ISR runs immediately after ps2_restart_pio().
// The device layer registers this to signal its core1 loop to abandon the
// current transaction and re-sync at the address gate (a lightweight flag, not
// a core reset — see ps2_device.c). NULL ok.
void    ps2_transport_set_sel_hook(void (*hook)(void));

// Enable/disable the SEL-rising IRQ (registered by ps2_transport_init). The
// lifecycle enables it while a controller is connected, disables on drop.
void    ps2_transport_enable_sel(bool enabled);

#endif // PS2_TRANSPORT_H
