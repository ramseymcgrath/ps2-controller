#include "status_indicator.h"

#include <stdio.h>
#include <stdbool.h>

#include "pico/stdlib.h"
#include "pico/time.h"        // repeating_timer, add_repeating_timer_ms
#include "hardware/i2c.h"

#include "matrix_render.h"
#include "matrix_driver.h"

#define MATRIX_I2C    i2c0
#define MATRIX_SDA    4u
#define MATRIX_SCL    5u
#define MATRIX_BAUD   400000u
#define MATRIX_ADDR   0x70u

static bool s_enabled = false;
// s_state is shared between core0 thread context (set) and the core0 render
// timer IRQ; relaxed atomics make the access well-defined (mirrors the prior
// WS2812 module). s_frame is touched only by the render timer.
static status_state_t s_state = STATUS_BOOT;
static uint8_t s_frame[MATRIX_FRAME_BYTES];

static bool animated(status_state_t s) {
    return s == STATUS_SEARCHING || s == STATUS_ERROR;
}

static bool render_cb(repeating_timer_t *t) {
    (void)t;
    if (!s_enabled) return true;

    status_state_t st = __atomic_load_n(&s_state, __ATOMIC_RELAXED);
    uint16_t phase = (uint16_t)(to_ms_since_boot(get_absolute_time()) & 0xFFFF);

    uint8_t next[MATRIX_FRAME_BYTES];
    matrix_render_frame(st, phase, next);

    // Push on change, or every tick while the state animates.
    bool changed = animated(st);
    if (!changed)
        for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++)
            if (next[i] != s_frame[i]) { changed = true; break; }

    if (changed) {
        for (unsigned i = 0; i < MATRIX_FRAME_BYTES; i++) s_frame[i] = next[i];
        matrix_driver_show(s_frame);
    }
    return true;
}

void status_indicator_init(void) {
    if (s_enabled) return;                       // idempotent

    if (!matrix_driver_init(MATRIX_I2C, MATRIX_SDA, MATRIX_SCL, MATRIX_BAUD, MATRIX_ADDR)) {
        printf("status_led: matrix init failed; LED disabled\n");
        return;                                  // driver logged the cause
    }

    matrix_clear(s_frame);
    matrix_driver_show(s_frame);                 // blank (BOOT)

    static repeating_timer_t timer;
    if (!add_repeating_timer_ms(-20, render_cb, NULL, &timer)) {   // 50 Hz
        printf("status_led: timer start failed; LED disabled\n");
        return;
    }
    s_enabled = true;
}

void status_indicator_set(status_state_t s) {
    if (!s_enabled) return;                      // no-op when disabled
    __atomic_store_n(&s_state, s, __ATOMIC_RELAXED);
}
