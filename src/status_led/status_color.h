#ifndef STATUS_COLOR_H
#define STATUS_COLOR_H

#include <stdint.h>
#include "input_state.h"   // PSXInputState

// Public, settable indicator state.
typedef enum {
    STATUS_BOOT = 0,   // pre-BT init
    STATUS_SEARCHING,  // BT up, no gamepad
    STATUS_CONNECTED,  // gamepad paired
    STATUS_ERROR,      // fault
} status_state_t;

// Per-channel brightness cap (of 255). ~19%: a status LED at full white is
// ~60 mA and glaring. Every color function keeps each channel <= this.
#define STATUS_MAX_BRIGHTNESS 48u

// Activity weighting: each newly-pressed button contributes this much "activity"
// (comparable to ~1/4 of a full single-stick deflection of 512).
#define STATUS_BTN_WEIGHT 32u

// Rainbow speed: hue += (activity * GAIN) >> SHIFT per render tick.
#define STATUS_RAINBOW_GAIN  1u
#define STATUS_RAINBOW_SHIFT 6u

// Activity magnitude from one input update: stick deflection from center plus
// newly-pressed-button energy. Neutral sticks + no new presses -> 0.
uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev);

// HSV wheel at full saturation/value, scaled to STATUS_MAX_BRIGHTNESS.
// Returns 0xRRGGBB; no channel exceeds STATUS_MAX_BRIGHTNESS.
uint32_t hue_to_rgb(uint8_t hue);

// Full palette. `phase` is a free-running millisecond counter (used for
// breathing/blink timing); `hue` is the rainbow phase for CONNECTED.
// Returns 0xRRGGBB.
uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue);

#endif // STATUS_COLOR_H
