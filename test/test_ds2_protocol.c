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
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG;
    PSXInputState in = ds2_neutral_state();
    uint8_t buf[5];
    buf[4] = 0xAA;                 // canary one past cap
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, buf, 4); // cap=4 < 8-byte frame
    TEST_ASSERT_TRUE(n <= 4);      // never reports more than cap
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[4]);  // canary intact => no overflow past cap
    const uint8_t prefix[] = {0x73, 0x5A, 0xFF, 0xFF};  // truncated analog prefix
    TEST_ASSERT_EQUAL_HEX8_ARRAY(prefix, buf, 4);
}

static void test_digital_poll_respects_cap(void) {
    ds2_state_t st; ds2_init(&st);   // mode DIGITAL
    PSXInputState in = ds2_neutral_state();
    uint8_t buf[4];
    buf[3] = 0xAA;                 // canary one past cap
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, buf, 3); // cap=3 < 4-byte frame
    TEST_ASSERT_TRUE(n <= 3);
    TEST_ASSERT_EQUAL_HEX8(0xAA, buf[3]);  // canary intact
    const uint8_t prefix[] = {0x41, 0x5A, 0xFF};
    TEST_ASSERT_EQUAL_HEX8_ARRAY(prefix, buf, 3);
}

static void test_config_mode_id_is_F3(void) {
    ds2_state_t st; ds2_init(&st);
    st.mode = MODE_ANALOG;
    st.config = true;                       // in config/escape mode
    PSXInputState in = ds2_neutral_state();
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    TEST_ASSERT_TRUE(n >= 1);
    TEST_ASSERT_EQUAL_HEX8(0xF3, out[0]);   // ID reflects config, not mode
}

static void test_analog_poll_reflects_input(void) {
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG;
    PSXInputState in = ds2_neutral_state();
    in.buttons2 &= (uint8_t)~PS_X;   // press Cross (active-low: clear the bit)
    in.lx = 0x00;                    // left stick full left
    in.ry = 0xFF;                    // right stick full down
    uint8_t out[32];
    size_t n = ds2_response(&st, CMD_POLL, &in, NULL, 0, out, sizeof out);
    TEST_ASSERT_EQUAL_UINT(8, n);
    TEST_ASSERT_EQUAL_HEX8(in.buttons1, out[2]);
    TEST_ASSERT_EQUAL_HEX8(in.buttons2, out[3]);   // Cross bit cleared
    TEST_ASSERT_EQUAL_HEX8(0xFF, out[5]);          // ry
    TEST_ASSERT_EQUAL_HEX8(0x00, out[6]);          // lx
}

static void test_enter_config_sets_flag(void) {
    ds2_state_t st; ds2_init(&st); st.mode = MODE_ANALOG;
    const uint8_t req[] = {0x00, 0x01, 0x00, 0x00, 0x00, 0x00};  // enter
    ds2_apply_request(&st, CMD_CONFIG, req, sizeof req);
    TEST_ASSERT_TRUE(st.config);
}

static void test_exit_config_clears_flag(void) {
    ds2_state_t st; ds2_init(&st); st.config = true;
    const uint8_t req[] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};  // exit
    ds2_apply_request(&st, CMD_CONFIG, req, sizeof req);
    TEST_ASSERT_FALSE(st.config);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_neutral_state_defaults);
    RUN_TEST(test_analog_poll_neutral);
    RUN_TEST(test_digital_poll_neutral);
    RUN_TEST(test_response_respects_cap);
    RUN_TEST(test_digital_poll_respects_cap);
    RUN_TEST(test_config_mode_id_is_F3);
    RUN_TEST(test_analog_poll_reflects_input);
    RUN_TEST(test_enter_config_sets_flag);
    RUN_TEST(test_exit_config_clears_flag);
    return UNITY_END();
}
