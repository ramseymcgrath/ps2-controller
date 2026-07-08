#include "gamepad_map.h"
#include "ds2_ids.h"

// BluePad32 uni_gamepad.h values (mirrored; keep in sync with the SDK header).
#define BP_DPAD_UP    0x01
#define BP_DPAD_DOWN  0x02
#define BP_DPAD_RIGHT 0x04
#define BP_DPAD_LEFT  0x08
#define BP_BTN_A      0x0001  // Cross
#define BP_BTN_B      0x0002  // Circle
#define BP_BTN_X      0x0008  // Square
#define BP_BTN_Y      0x0010  // Triangle
#define BP_BTN_L1     0x0020  // was 0x0010? verify against SDK in Task 11
#define BP_BTN_R1     0x0040
#define BP_BTN_L3     0x0100
#define BP_BTN_R3     0x0200
// misc buttons live in a separate BluePad32 field; wired up in Task 11.

// BluePad32 analog input ranges and the constants derived from them.
#define BP_AXIS_CENTER_OFFSET     512  // axis is signed -512..511; +offset shifts to 0..1023
#define BP_ANALOG_TO_U8_DIV       4    // downscale a 0..1023 (10-bit) value to 0..255 (8-bit)
#define TRIGGER_DIGITAL_THRESHOLD 512  // brake/throttle press point that latches the L2/R2 bit

static uint8_t axis_to_u8(int32_t v) {
    // BluePad32 axis range -512..511 -> 0..255, neutral 0x80. Use '/' (not '>>') so the
    // scaling stays well-defined even if a controller reports an out-of-range value.
    int32_t s = (v + BP_AXIS_CENTER_OFFSET) / BP_ANALOG_TO_U8_DIV;
    if (s < 0)   s = 0;
    if (s > 255) s = 255;
    return (uint8_t)s;
}

void map_gamepad_to_psx(const gamepad_snapshot_t *g, PSXInputState *out) {
    uint8_t b1 = 0xFF, b2 = 0xFF;       // active-low, start all-released

    // buttons1 (BTNL): dpad + Start/Select/L3/R3 (Start/Select added in Task 11)
    { uint8_t b_target = b1;
      if (g->dpad & BP_DPAD_UP)    b_target &= (uint8_t)~PS_UP;
      if (g->dpad & BP_DPAD_DOWN)  b_target &= (uint8_t)~PS_DOWN;
      if (g->dpad & BP_DPAD_LEFT)  b_target &= (uint8_t)~PS_LEFT;
      if (g->dpad & BP_DPAD_RIGHT) b_target &= (uint8_t)~PS_RIGHT;
      if (g->buttons & BP_BTN_L3)  b_target &= (uint8_t)~PS_L3;
      if (g->buttons & BP_BTN_R3)  b_target &= (uint8_t)~PS_R3;
      b1 = b_target; }

    // buttons2 (BTNH): face + shoulders + triggers-as-digital
    { uint8_t b_target = b2;
      if (g->buttons & BP_BTN_A) b_target &= (uint8_t)~PS_X;
      if (g->buttons & BP_BTN_B) b_target &= (uint8_t)~PS_CIR;
      if (g->buttons & BP_BTN_X) b_target &= (uint8_t)~PS_SQU;
      if (g->buttons & BP_BTN_Y) b_target &= (uint8_t)~PS_TRI;
      if (g->buttons & BP_BTN_L1) b_target &= (uint8_t)~PS_L1;
      if (g->buttons & BP_BTN_R1) b_target &= (uint8_t)~PS_R1;
      if (g->brake    > TRIGGER_DIGITAL_THRESHOLD) b_target &= (uint8_t)~PS_L2;
      if (g->throttle > TRIGGER_DIGITAL_THRESHOLD) b_target &= (uint8_t)~PS_R2;
      b2 = b_target; }

    out->buttons1 = b1;
    out->buttons2 = b2;
    out->lx = axis_to_u8(g->axis_x);
    out->ly = axis_to_u8(g->axis_y);
    out->rx = axis_to_u8(g->axis_rx);
    out->ry = axis_to_u8(g->axis_ry);
    out->l2 = (uint8_t)(g->brake    / BP_ANALOG_TO_U8_DIV);
    out->r2 = (uint8_t)(g->throttle / BP_ANALOG_TO_U8_DIV);
}
