#ifndef PS2_TRANSPORT_H
#define PS2_TRANSPORT_H
#include <stdint.h>

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

// Blocking single-byte transfers on the console bus.
uint8_t ps2_recv_cmd(void);   // read one CMD byte the console shifted in
void    ps2_send(uint8_t byte); // drive one DAT byte back to the console

// Re-sync both state machines to the start of a transaction. RAM-resident
// (__time_critical_func) because it runs from the SEL ISR.
void    ps2_restart_pio(void);

// Optional hook the SEL-rising ISR runs immediately after ps2_restart_pio().
// The device/lifecycle layer registers this to reset+relaunch the core1
// protocol thread, so each transaction re-starts parsing from byte 0. NULL ok.
void    ps2_transport_set_sel_hook(void (*hook)(void));

#endif // PS2_TRANSPORT_H
