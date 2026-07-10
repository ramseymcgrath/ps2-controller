#include "matrix_driver.h"

#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/dma.h"
#include "hardware/regs/i2c.h"   // I2C_IC_DATA_CMD_STOP_BITS, I2C_IC_ENABLE_ENABLE_BITS

// HT16K33 command bytes.
#define HT16K33_OSC_ON   0x21u   // system setup: oscillator on
#define HT16K33_DISP_ON  0x81u   // display on, blink off
#define HT16K33_DIM_BASE 0xE0u   // | brightness(0..15)

static i2c_inst_t *s_i2c;
static uint8_t     s_addr;
static bool        s_ok;
#ifndef MATRIX_SHOW_BLOCKING
static int         s_dma = -1;
static uint16_t    s_tx[17];     // [ptr, 16 data] as IC_DATA_CMD command words
#endif

static bool wr(uint8_t byte) {   // one blocking command byte
    return i2c_write_blocking(s_i2c, s_addr, &byte, 1, false) == 1;
}

bool matrix_driver_init(i2c_inst_t *i2c, unsigned sda, unsigned scl, unsigned baud, uint8_t addr) {
    s_i2c = i2c; s_addr = addr; s_ok = false;

    i2c_init(i2c, baud);
    gpio_set_function(sda, GPIO_FUNC_I2C);
    gpio_set_function(scl, GPIO_FUNC_I2C);
    gpio_pull_up(sda);
    gpio_pull_up(scl);

    if (!wr(HT16K33_OSC_ON) || !wr(HT16K33_DISP_ON) || !wr(HT16K33_DIM_BASE | 0x0Fu)) {
        printf("matrix: HT16K33 not responding; LED disabled\n");
        return false;
    }

#ifndef MATRIX_SHOW_BLOCKING
    // Pin the DMA target in IC_TAR (I2C must be disabled to change it) so DMA
    // writes to IC_DATA_CMD address the HT16K33 without per-transfer setup.
    i2c_hw_t *hw = i2c_get_hw(i2c);
    hw->enable = 0;
    hw->tar = addr;
    hw->enable = I2C_IC_ENABLE_ENABLE_BITS;

    s_dma = dma_claim_unused_channel(false);   // false: don't panic on failure
    if (s_dma < 0) {
        printf("matrix: no free DMA channel; LED disabled\n");
        return false;
    }
#endif

    s_ok = true;
    return true;
}

void matrix_driver_set_brightness(unsigned level) {
    if (!s_ok) return;
    wr((uint8_t)(HT16K33_DIM_BASE | (level & 0x0Fu)));
}

#ifdef MATRIX_SHOW_BLOCKING
void matrix_driver_show(const uint8_t frame[16]) {
    if (!s_ok) return;
    uint8_t buf[17];
    buf[0] = 0x00;                              // display RAM pointer
    for (unsigned i = 0; i < 16u; i++) buf[i + 1u] = frame[i];
    i2c_write_blocking(s_i2c, s_addr, buf, 17, false);
}
#else
void matrix_driver_show(const uint8_t frame[16]) {
    if (!s_ok) return;
    dma_channel_wait_for_finish_blocking(s_dma);   // ensure prior transfer done

    s_tx[0] = 0x00;                                // display RAM pointer
    for (unsigned i = 0; i < 16u; i++)
        s_tx[i + 1u] = frame[i];
    s_tx[16] |= I2C_IC_DATA_CMD_STOP_BITS;         // assert STOP after last byte

    dma_channel_config c = dma_channel_get_default_config(s_dma);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, i2c_get_dreq(s_i2c, true));
    dma_channel_configure(s_dma, &c,
        &i2c_get_hw(s_i2c)->data_cmd,   // dst: I2C data/command register
        s_tx,                           // src: 17 command words
        17,                             // count
        true);                          // start now
}
#endif
