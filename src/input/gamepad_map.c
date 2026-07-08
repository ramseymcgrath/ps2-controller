#include "gamepad_map.h"
#include "ds2_ids.h"

// The BluePad32 snapshot bit masks (BP_DPAD_*/BP_BTN_*/BP_MISC_*) live in
// gamepad_map.h; bluepad32_platform.c static_asserts them against <uni.h>.

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

    // buttons1 (BTNL): dpad + Select/Start/L3/R3
    { uint8_t b_target = b1;
      if (g->dpad & BP_DPAD_UP)          b_target &= (uint8_t)~PS_UP;
      if (g->dpad & BP_DPAD_DOWN)        b_target &= (uint8_t)~PS_DOWN;
      if (g->dpad & BP_DPAD_LEFT)        b_target &= (uint8_t)~PS_LEFT;
      if (g->dpad & BP_DPAD_RIGHT)       b_target &= (uint8_t)~PS_RIGHT;
      if (g->misc_buttons & BP_MISC_SELECT) b_target &= (uint8_t)~PS_SELECT;
      if (g->misc_buttons & BP_MISC_START)  b_target &= (uint8_t)~PS_START;
      if (g->buttons & BP_BTN_L3)        b_target &= (uint8_t)~PS_L3;
      if (g->buttons & BP_BTN_R3)        b_target &= (uint8_t)~PS_R3;
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
