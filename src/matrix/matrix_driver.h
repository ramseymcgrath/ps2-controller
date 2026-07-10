#ifndef MATRIX_DRIVER_H
#define MATRIX_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "hardware/i2c.h"

// Init the I2C bus + GPIO, run the HT16K33 init sequence (osc on, display on,
// brightness 0x0F), and claim a DMA channel. Returns true on success; on
// failure the driver stays disabled and show()/set_brightness() are no-ops.
// Blocking; call once on core0 after the system clock is set.
bool matrix_driver_init(i2c_inst_t *i2c, unsigned sda, unsigned scl, unsigned baud, uint8_t addr);

// Set HT16K33 brightness 0..15 (blocking single-byte write). For bring-up.
void matrix_driver_set_brightness(unsigned level);

// Push a 16-byte frame to display RAM as a 17-byte [0x00, d0..d15] transaction.
// Waits for any prior DMA transfer to finish first.
void matrix_driver_show(const uint8_t frame[16]);

#endif // MATRIX_DRIVER_H
