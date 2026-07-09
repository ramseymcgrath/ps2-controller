#include "status_indicator.h"

#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/time.h"       // repeating_timer, add_repeating_timer_ms
#include "hardware/pio.h"

#include "ws2812.pio.h"      // SDK program: ws2812_program, ws2812_program_init

#include "status_color.h"

#define STATUS_PIO   pio2
#define WS2812_FREQ  800000

static PIO  s_pio = STATUS_PIO;
static uint s_sm;
// s_enabled and s_state are shared between core0 thread context (init / set) and
// the core0 render-timer IRQ. Accessed via relaxed atomics so the IRQ-vs-thread
// access is a well-defined single-copy op (not just volatile), consistent with
// the activity accumulator below. Relaxed suffices: neither publishes other data.
static bool s_enabled = false;

static status_state_t s_state = STATUS_BOOT;
// Atomic activity accumulator (note_input thread -> render IRQ, both core0):
// the producer adds with __atomic_fetch_add, the render tick drains it to 0 with
// __atomic_exchange_n. Lock-free; no lost or double-counted activity.
static volatile uint32_t s_pending_activity = 0;
static PSXInputState s_prev_input;                // core0 note_input only
static uint8_t s_hue = 0;                         // render tick only

// Pack 0xRRGGBB into the WS2812 wire order (GRB, MSB-first) and push it. The
// SDK ws2812 program left-shifts 24 bits out of the top of each 32-bit word.
static inline void ws2812_put(uint32_t rgb) {
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    uint32_t grb = ((uint32_t)g << 16) | ((uint32_t)r << 8) | b;
    if (!pio_sm_is_tx_fifo_full(s_pio, s_sm))
        pio_sm_put(s_pio, s_sm, grb << 8u);
}

static bool render_cb(repeating_timer_t *t) {
    (void)t;
    if (!__atomic_load_n(&s_enabled, __ATOMIC_RELAXED))
        return true;

    status_state_t st = __atomic_load_n(&s_state, __ATOMIC_RELAXED);
    if (st == STATUS_CONNECTED) {
        uint32_t act = __atomic_exchange_n(&s_pending_activity, 0, __ATOMIC_RELAXED);
        s_hue = (uint8_t)(s_hue +
            (uint8_t)((act * STATUS_RAINBOW_GAIN) >> STATUS_RAINBOW_SHIFT));
    }

    uint16_t phase = (uint16_t)(to_ms_since_boot(get_absolute_time()) & 0xFFFF);
    ws2812_put(status_color(st, phase, s_hue));
    return true;
}

void status_indicator_init(void) {
    s_prev_input = ds2_neutral_state();

    // Claim the SM first. It is the only step here that can fail at runtime
    // (pio_add_program can't fail after pio_can_add_program succeeds), so
    // claiming before adding means a failure leaves nothing loaded in pio2's
    // instruction memory. Each later failure path unwinds what it acquired, so
    // a disabled LED leaves no residue on pio2.
    int sm = pio_claim_unused_sm(s_pio, false);  // false: don't panic on failure
    if (sm < 0) {
        printf("status_led: no free pio2 SM; LED disabled\n");
        return;
    }
    s_sm = (uint)sm;

    if (!pio_can_add_program(s_pio, &ws2812_program)) {
        printf("status_led: no pio2 program room; LED disabled\n");
        pio_sm_unclaim(s_pio, s_sm);
        return;
    }
    uint off = pio_add_program(s_pio, &ws2812_program);
    ws2812_program_init(s_pio, s_sm, off, STATUS_LED_PIN, WS2812_FREQ, false);

    static repeating_timer_t timer;
    if (!add_repeating_timer_ms(-20, render_cb, NULL, &timer)) {  // 50 Hz
        printf("status_led: timer start failed; LED disabled\n");
        pio_sm_set_enabled(s_pio, s_sm, false);  // ws2812_program_init enabled it
        pio_remove_program(s_pio, &ws2812_program, off);
        pio_sm_unclaim(s_pio, s_sm);
        return;
    }
    __atomic_store_n(&s_enabled, true, __ATOMIC_RELAXED);
}

void status_indicator_set(status_state_t s) {
    __atomic_store_n(&s_state, s, __ATOMIC_RELAXED);
}

void status_indicator_note_input(const PSXInputState *s) {
    __atomic_fetch_add(&s_pending_activity, input_activity(s, &s_prev_input), __ATOMIC_RELAXED);
    s_prev_input = *s;
}
