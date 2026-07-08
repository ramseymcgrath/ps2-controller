#include "unity/unity.h"
#include "input_state.h"
#include "ds2_ids.h"
#include "ds2_protocol.h"

void setUp(void) {}
void tearDown(void) {}

static void test_neutral_state_defaults(void) {
    PSXInputState s = ds2_neutral_state();
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.buttons1);   // all released (active-low)
    TEST_ASSERT_EQUAL_HEX8(0xFF, s.buttons2);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.rx);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.ry);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.lx);
    TEST_ASSERT_EQUAL_HEX8(0x80, s.ly);
}

static void test_analog_poll_neutral(void) {
    ds2_state_t st;
    ds2_init(&st);
    st.mode = MODE_ANALOG;         // pretend a game already enabled analog
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    // FF is the transport idle byte; the frame from ID onward is 8 bytes:
    const uint8_t expect[] = {0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

static void test_digital_poll_neutral(void) {
    ds2_state_t st;
    ds2_init(&st);
    // Mode defaults to DIGITAL
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    // Digital frame is 4 bytes: ID (0x41), constant (0x5A), buttons1 (0xFF), buttons2 (0xFF)
    const uint8_t expect[] = {0x41, 0x5A, 0xFF, 0xFF};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, out, n);
}

static void test_response_respects_cap(void) {
    ds2_state_t st;
    ds2_init(&st);
    st.mode = MODE_ANALOG;  // Analog needs 8 bytes total
    PSXInputState in = ds2_neutral_state();

    // Small buffer with canary byte
    uint8_t buf[9];
    buf[8] = 0xAA;  // Canary byte at index 8

    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, buf, 8);

    // Should return at most the capacity (8)
    TEST_ASSERT_LESS_THAN_OR_EQUAL(8, n);

    // Canary byte must not be clobbered
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[8]);

    // The frame should still be the expected 8 bytes
    const uint8_t expect[] = {0x73, 0x5A, 0xFF, 0xFF, 0x80, 0x80, 0x80, 0x80};
    TEST_ASSERT_EQUAL_UINT(sizeof expect, n);
    TEST_ASSERT_EQUAL_HEX8_ARRAY(expect, buf, n);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_state_defaults);
    RUN_TEST(test_analog_poll_neutral);
    RUN_TEST(test_digital_poll_neutral);
    RUN_TEST(test_response_respects_cap);
    return UNITY_END();
}
