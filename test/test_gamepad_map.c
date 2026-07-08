#include "unity/unity.h"
#include "gamepad_map.h"
#include "ds2_ids.h"

void setUp(void) {}
void tearDown(void) {}

// BluePad32 bit values (from uni_gamepad.h) mirrored for the host test.
#define DPAD_UP 0x01
#define BP_BTN_X 0x0008  // square on DS
#define BP_BTN_A 0x0001  // cross

static gamepad_snapshot_t neutral_snapshot(void) {
    gamepad_snapshot_t g = {0};
    return g; // all zero: no dpad, no buttons, sticks centered at 0, triggers 0
}

static void test_neutral_maps_to_neutral(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.buttons1);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.buttons2);
    TEST_ASSERT_EQUAL_HEX8(0x80, out.lx);
    TEST_ASSERT_EQUAL_HEX8(0x80, out.ly);
}

static void test_dpad_up_clears_up_bit(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.dpad = DPAD_UP;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_UP); // active-low: pressed => 0
}

static void test_left_stick_full_left(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.axis_x = -512;                 // BluePad32 min
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.lx);
}

static void test_trigger_threshold_sets_l2(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.brake = 1023;                  // full left trigger
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_L2); // pressed => bit clear
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_maps_to_neutral);
    RUN_TEST(test_dpad_up_clears_up_bit);
    RUN_TEST(test_left_stick_full_left);
    RUN_TEST(test_trigger_threshold_sets_l2);
    return UNITY_END();
}
