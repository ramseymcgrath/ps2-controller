#ifndef GAMEPAD_MAP_H
#define GAMEPAD_MAP_H
#include <stdint.h>
#include "input_state.h"

// Board-independent mirror of the uni_gamepad_t fields we consume.
typedef struct {
    uint8_t  dpad;                       // BluePad32 DPAD_* bitmask
    uint32_t buttons;                    // BluePad32 BUTTON_* bitmask
    int32_t  axis_x, axis_y;             // left  stick, -512..511
    int32_t  axis_rx, axis_ry;           // right stick, -512..511
    int32_t  brake, throttle;            // L2/R2 analog, 0..1023
} gamepad_snapshot_t;

void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out);

#endif // GAMEPAD_MAP_H
