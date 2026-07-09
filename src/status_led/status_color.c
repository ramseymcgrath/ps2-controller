#include "status_color.h"
#include <stdlib.h>   // abs

// Amber for the "searching" pulse: full red + ~40% green.
#define AMBER_R STATUS_MAX_BRIGHTNESS
#define AMBER_G ((STATUS_MAX_BRIGHTNESS * 2u) / 5u)

static inline uint32_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

// Scale an 8-bit channel by an 8-bit level (0..255).
static inline uint8_t scale(uint8_t ch, uint8_t level) {
    return (uint8_t)(((uint16_t)ch * level) / 255u);
}

uint32_t input_activity(const PSXInputState *cur, const PSXInputState *prev) {
    int deflection = abs((int)cur->lx - 0x80) + abs((int)cur->ly - 0x80)
                   + abs((int)cur->rx - 0x80) + abs((int)cur->ry - 0x80);
    // Active-low: newly pressed = bit was 1 (released) and is now 0 (pressed).
    uint8_t np1 = (uint8_t)(prev->buttons1 & ~cur->buttons1);
    uint8_t np2 = (uint8_t)(prev->buttons2 & ~cur->buttons2);
    int presses = __builtin_popcount(np1) + __builtin_popcount(np2);
    return (uint32_t)(deflection + presses * (int)STATUS_BTN_WEIGHT);
}

uint32_t hue_to_rgb(uint8_t hue) {
    uint8_t max = STATUS_MAX_BRIGHTNESS;
    uint8_t region = hue / 43u;                 // 0..5
    uint8_t up = scale(max, (uint8_t)((hue % 43u) * 6u));
    uint8_t down = (uint8_t)(max - up);
    switch (region) {
        case 0:  return rgb(max,  up,   0);
        case 1:  return rgb(down, max,  0);
        case 2:  return rgb(0,    max,  up);
        case 3:  return rgb(0,    down, max);
        case 4:  return rgb(up,   0,    max);
        default: return rgb(max,  0,    down);
    }
}

uint32_t status_color(status_state_t s, uint16_t phase, uint8_t hue) {
    switch (s) {
        case STATUS_CONNECTED:
            return hue_to_rgb(hue);
        case STATUS_SEARCHING: {
            uint8_t t = (uint8_t)((phase >> 3) & 0xFF);   // ~2 s period
            uint8_t tri = t < 128 ? (uint8_t)(t * 2) : (uint8_t)((255 - t) * 2);
            return rgb(scale(AMBER_R, tri), scale(AMBER_G, tri), 0);
        }
        case STATUS_ERROR:
            return ((phase >> 8) & 1u) ? rgb(STATUS_MAX_BRIGHTNESS, 0, 0)  // ~2 Hz
                                       : 0;
        case STATUS_BOOT:
        default:
            return 0;
    }
}
