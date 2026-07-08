#include "unity/unity.h"
#include "gamepad_map.h"
#include "ds2_ids.h"

void setUp(void) {}
void tearDown(void) {}

// Button/dpad/misc bit masks come from gamepad_map.h (BP_DPAD_*/BP_BTN_*/BP_MISC_*),
// the single source of truth that bluepad32_platform.c static_asserts against <uni.h>.

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
    TEST_ASSERT_EQUAL_HEX8(0x80, out.rx);
    TEST_ASSERT_EQUAL_HEX8(0x80, out.ry);
}

// --- D-pad (buttons1, active-low) ---

static void test_dpad_all_directions(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.dpad = BP_DPAD_UP | BP_DPAD_DOWN | BP_DPAD_LEFT | BP_DPAD_RIGHT;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_UP);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_DOWN);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_LEFT);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_RIGHT);
}

static void test_dpad_up_clears_only_up(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.dpad = BP_DPAD_UP;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_UP);
    TEST_ASSERT_EQUAL_HEX8(PS_DOWN, out.buttons1 & PS_DOWN);   // still released
}

// --- Face buttons (buttons2, active-low) ---

static void test_face_buttons_map_to_ds2(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.buttons = BP_BTN_A | BP_BTN_B | BP_BTN_X | BP_BTN_Y;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_X);    // A  -> Cross
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_CIR);  // B  -> Circle
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_SQU);  // X  -> Square
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_TRI);  // Y  -> Triangle
}

static void test_cross_only_leaves_others_released(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.buttons = BP_BTN_A;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_X);
    TEST_ASSERT_EQUAL_HEX8(PS_CIR, out.buttons2 & PS_CIR);
    TEST_ASSERT_EQUAL_HEX8(PS_SQU, out.buttons2 & PS_SQU);
    TEST_ASSERT_EQUAL_HEX8(PS_TRI, out.buttons2 & PS_TRI);
}

// --- Shoulders (buttons2) ---

static void test_shoulders_map(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.buttons = BP_BTN_L1 | BP_BTN_R1;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_L1);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_R1);
}

// --- Stick clicks L3/R3 (buttons1) ---

static void test_stick_clicks_map(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.buttons = BP_BTN_L3 | BP_BTN_R3;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_L3);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_R3);
}

// --- Start / Select from misc_buttons (buttons1) ---

static void test_start_select_map(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.misc_buttons = BP_MISC_START | BP_MISC_SELECT;
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_START);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons1 & PS_SELECT);
}

// --- Sticks ---

static void test_left_stick_full_left(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.axis_x = -512;                 // BluePad32 min
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.lx);
}

static void test_left_stick_full_right(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.axis_x = 511;                  // BluePad32 max
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.lx);
}

static void test_right_stick_maps(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.axis_rx = -512;                // full left
    g.axis_ry = 511;                 // full down
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.rx);
    TEST_ASSERT_EQUAL_HEX8(0xFF, out.ry);
}

// --- Triggers as digital L2/R2 (buttons2) ---

static void test_trigger_threshold_sets_l2(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.brake = 1023;                  // full left trigger
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_L2); // pressed => bit clear
}

static void test_trigger_threshold_sets_r2(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.throttle = 1023;               // full right trigger
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(0x00, out.buttons2 & PS_R2);
}

static void test_light_trigger_below_threshold_stays_released(void) {
    gamepad_snapshot_t g = neutral_snapshot();
    g.brake = 100;                   // below TRIGGER_DIGITAL_THRESHOLD (512)
    PSXInputState out;
    map_gamepad_to_psx(&g, &out);
    TEST_ASSERT_EQUAL_HEX8(PS_L2, out.buttons2 & PS_L2); // still released
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_maps_to_neutral);
    RUN_TEST(test_dpad_all_directions);
    RUN_TEST(test_dpad_up_clears_only_up);
    RUN_TEST(test_face_buttons_map_to_ds2);
    RUN_TEST(test_cross_only_leaves_others_released);
    RUN_TEST(test_shoulders_map);
    RUN_TEST(test_stick_clicks_map);
    RUN_TEST(test_start_select_map);
    RUN_TEST(test_left_stick_full_left);
    RUN_TEST(test_left_stick_full_right);
    RUN_TEST(test_right_stick_maps);
    RUN_TEST(test_trigger_threshold_sets_l2);
    RUN_TEST(test_trigger_threshold_sets_r2);
    RUN_TEST(test_light_trigger_below_threshold_stays_released);
    return UNITY_END();
}
