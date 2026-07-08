#ifndef GAMEPAD_MAP_H
#define GAMEPAD_MAP_H
#include <stdint.h>
#include "input_state.h"

// Board-independent mirror of the uni_gamepad_t fields we consume.
// The dpad/buttons/misc_buttons bit layouts below MUST equal BluePad32's
// DPAD_*/BUTTON_*/MISC_BUTTON_* values: bluepad32_platform.c copies these
// gamepad fields in raw, and static_asserts each mask against <uni.h>.
typedef struct {
    uint8_t  dpad;                       // BluePad32 DPAD_* bitmask
    uint32_t buttons;                    // BluePad32 BUTTON_* bitmask
    uint8_t  misc_buttons;               // BluePad32 MISC_BUTTON_* bitmask
    int32_t  axis_x, axis_y;             // left  stick, -512..511
    int32_t  axis_rx, axis_ry;           // right stick, -512..511
    int32_t  brake, throttle;            // L2/R2 analog, 0..1023
} gamepad_snapshot_t;

// Snapshot bit layout — identical to BluePad32 uni_gamepad.h, where each value
// is BIT(n) with n the enum position. Enforced at firmware compile time by
// _Static_assert in bluepad32_platform.c against the real BUTTON_*/DPAD_*/MISC_*.
#define BP_DPAD_UP    0x01u  // BIT(0)
#define BP_DPAD_DOWN  0x02u  // BIT(1)
#define BP_DPAD_RIGHT 0x04u  // BIT(2)
#define BP_DPAD_LEFT  0x08u  // BIT(3)

#define BP_BTN_A      0x0001u  // BIT(0) Cross
#define BP_BTN_B      0x0002u  // BIT(1) Circle
#define BP_BTN_X      0x0004u  // BIT(2) Square
#define BP_BTN_Y      0x0008u  // BIT(3) Triangle
#define BP_BTN_L1     0x0010u  // BIT(4) SHOULDER_L
#define BP_BTN_R1     0x0020u  // BIT(5) SHOULDER_R
#define BP_BTN_L3     0x0100u  // BIT(8) THUMB_L
#define BP_BTN_R3     0x0200u  // BIT(9) THUMB_R

#define BP_MISC_SELECT 0x02u   // BIT(1) Select/Share/Create
#define BP_MISC_START  0x04u   // BIT(2) Start/Options

void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out);

#endif // GAMEPAD_MAP_H
