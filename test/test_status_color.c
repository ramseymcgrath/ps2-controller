#include "unity/unity.h"
#include "status_color.h"
#include "input_state.h"

void setUp(void) {}
void tearDown(void) {}

static uint8_t R(uint32_t c) { return (c >> 16) & 0xFF; }
static uint8_t G(uint32_t c) { return (c >> 8) & 0xFF; }
static uint8_t B(uint32_t c) { return c & 0xFF; }
static int brightness(uint32_t c) { return R(c) + G(c) + B(c); }

// --- input_activity ---

static void test_activity_neutral_is_zero(void) {
    PSXInputState n = ds2_neutral_state();
    TEST_ASSERT_EQUAL_UINT32(0, input_activity(&n, &n));
}

static void test_activity_full_deflection(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState cur = n;
    cur.lx = 0x00; cur.ly = 0x00; cur.rx = 0x00; cur.ry = 0x00; // 128*4
    TEST_ASSERT_EQUAL_UINT32(512, input_activity(&cur, &n));
}

static void test_activity_more_deflection_is_larger(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState small = n, big = n;
    small.lx = 0x90;                 // |0x90-0x80| = 16
    big.lx   = 0xC0;                 // |0xC0-0x80| = 64
    TEST_ASSERT_TRUE(input_activity(&big, &n) > input_activity(&small, &n));
}

static void test_activity_fresh_press_adds_energy(void) {
    PSXInputState n = ds2_neutral_state();
    PSXInputState cur = n;
    cur.buttons2 = (uint8_t)~0x01;   // one button pressed (bit cleared)
    TEST_ASSERT_EQUAL_UINT32(STATUS_BTN_WEIGHT, input_activity(&cur, &n));
}

static void test_activity_held_button_does_not_readd(void) {
    PSXInputState prev = ds2_neutral_state();
    prev.buttons2 = (uint8_t)~0x01;  // already pressed last frame
    PSXInputState cur = prev;        // still pressed, no new edge
    TEST_ASSERT_EQUAL_UINT32(0, input_activity(&cur, &prev));
}

// --- hue_to_rgb ---

static void test_hue_zero_is_red(void) {
    uint32_t c = hue_to_rgb(0);
    TEST_ASSERT_EQUAL_UINT8(STATUS_MAX_BRIGHTNESS, R(c));
    TEST_ASSERT_EQUAL_UINT8(0, G(c));
    TEST_ASSERT_EQUAL_UINT8(0, B(c));
}

static void test_hue_never_exceeds_cap(void) {
    for (int h = 0; h < 256; h++) {
        uint32_t c = hue_to_rgb((uint8_t)h);
        TEST_ASSERT_TRUE(R(c) <= STATUS_MAX_BRIGHTNESS);
        TEST_ASSERT_TRUE(G(c) <= STATUS_MAX_BRIGHTNESS);
        TEST_ASSERT_TRUE(B(c) <= STATUS_MAX_BRIGHTNESS);
    }
}

static void test_hue_wraps_near_red(void) {
    // hue is circular: 255 must sit one small step from red (0), so s_hue
    // overflowing 255->0 has no visible seam. Full red channel, no green,
    // at most a couple units of blue.
    uint32_t c = hue_to_rgb(255);
    TEST_ASSERT_EQUAL_UINT8(STATUS_MAX_BRIGHTNESS, R(c));
    TEST_ASSERT_EQUAL_UINT8(0, G(c));
    TEST_ASSERT_TRUE(B(c) <= 2);
}

// --- status_color ---

static void test_boot_is_off(void) {
    TEST_ASSERT_EQUAL_UINT32(0, status_color(STATUS_BOOT, 0, 0));
}

static void test_connected_is_hue(void) {
    TEST_ASSERT_EQUAL_UINT32(hue_to_rgb(100), status_color(STATUS_CONNECTED, 0, 100));
}

static void test_searching_breathes_monotonic_rising(void) {
    // phase>>3 gives the breathing counter; rising half is counter 0..127.
    int b0   = brightness(status_color(STATUS_SEARCHING, 0,        0));
    int bmid = brightness(status_color(STATUS_SEARCHING, 64 << 3,  0));
    int bhi  = brightness(status_color(STATUS_SEARCHING, 127 << 3, 0));
    TEST_ASSERT_TRUE(b0 < bmid);
    TEST_ASSERT_TRUE(bmid < bhi);
}

static void test_error_blinks_red(void) {
    uint32_t off = status_color(STATUS_ERROR, 0, 0);     // (0>>8)&1 == 0
    uint32_t on  = status_color(STATUS_ERROR, 256, 0);   // (256>>8)&1 == 1
    TEST_ASSERT_EQUAL_UINT32(0, off);
    TEST_ASSERT_EQUAL_UINT8(STATUS_MAX_BRIGHTNESS, R(on));
    TEST_ASSERT_EQUAL_UINT8(0, G(on));
    TEST_ASSERT_EQUAL_UINT8(0, B(on));
}

static void test_all_states_respect_cap(void) {
    status_state_t states[] = {STATUS_BOOT, STATUS_SEARCHING, STATUS_CONNECTED, STATUS_ERROR};
    for (unsigned s = 0; s < 4; s++)
        for (int p = 0; p < 2048; p += 7) {
            uint32_t c = status_color(states[s], (uint16_t)p, (uint8_t)p);
            TEST_ASSERT_TRUE(R(c) <= STATUS_MAX_BRIGHTNESS);
            TEST_ASSERT_TRUE(G(c) <= STATUS_MAX_BRIGHTNESS);
            TEST_ASSERT_TRUE(B(c) <= STATUS_MAX_BRIGHTNESS);
        }
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_activity_neutral_is_zero);
    RUN_TEST(test_activity_full_deflection);
    RUN_TEST(test_activity_more_deflection_is_larger);
    RUN_TEST(test_activity_fresh_press_adds_energy);
    RUN_TEST(test_activity_held_button_does_not_readd);
    RUN_TEST(test_hue_zero_is_red);
    RUN_TEST(test_hue_never_exceeds_cap);
    RUN_TEST(test_hue_wraps_near_red);
    RUN_TEST(test_boot_is_off);
    RUN_TEST(test_connected_is_hue);
    RUN_TEST(test_searching_breathes_monotonic_rising);
    RUN_TEST(test_error_blinks_red);
    RUN_TEST(test_all_states_respect_cap);
    return UNITY_END();
}
