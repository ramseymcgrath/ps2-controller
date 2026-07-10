#include "unity/unity.h"
#include "matrix_render.h"
#include "matrix_glyphs.h"

void setUp(void) {}
void tearDown(void) {}

static unsigned count_lit(const uint8_t f[16]) {
    unsigned n = 0;
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 8; x++)
            if (matrix_get_pixel(f, x, y) != MATRIX_OFF) n++;
    return n;
}

static void assert_glyph(const uint8_t f[16], const uint8_t g[8], unsigned color) {
    for (unsigned y = 0; y < 8; y++)
        for (unsigned x = 0; x < 8; x++) {
            unsigned want = (g[y] & (1u << x)) ? color : MATRIX_OFF;
            TEST_ASSERT_EQUAL_UINT(want, matrix_get_pixel(f, x, y));
        }
}

static void test_clear_all_off(void) {
    uint8_t f[16]; for (int i = 0; i < 16; i++) f[i] = 0xAB;
    matrix_clear(f);
    for (int i = 0; i < 16; i++) TEST_ASSERT_EQUAL_UINT8(0, f[i]);
}

static void test_set_get_colors(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 1, 2, MATRIX_GREEN);
    matrix_set_pixel(f, 3, 4, MATRIX_RED);
    matrix_set_pixel(f, 5, 6, MATRIX_YELLOW);
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN,  matrix_get_pixel(f, 1, 2));
    TEST_ASSERT_EQUAL_UINT(MATRIX_RED,    matrix_get_pixel(f, 3, 4));
    TEST_ASSERT_EQUAL_UINT(MATRIX_YELLOW, matrix_get_pixel(f, 5, 6));
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF,    matrix_get_pixel(f, 0, 0));
}

static void test_set_off_clears(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 2, 2, MATRIX_YELLOW);
    matrix_set_pixel(f, 2, 2, MATRIX_OFF);
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF, matrix_get_pixel(f, 2, 2));
}

static void test_out_of_range_noop(void) {
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 8, 0, MATRIX_GREEN);
    matrix_set_pixel(f, 0, 8, MATRIX_GREEN);
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_OFF, matrix_get_pixel(f, 8, 0));
}

static void test_col_fixup_pinned(void) {
    // Pins the panel column mapping: logical x=0 -> bit (0+7)&7 = 7. If the
    // bench needs a different rotation, change col_fixup and this test together.
    uint8_t f[16]; matrix_clear(f);
    matrix_set_pixel(f, 0, 0, MATRIX_GREEN);
    TEST_ASSERT_EQUAL_UINT8(0x80, f[0]);   // green plane, row 0, bit 7
    TEST_ASSERT_EQUAL_UINT8(0x00, f[1]);   // red plane, row 0
}

static void test_boot_blank(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_BOOT, 12345, f);
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
}

static void test_1p_digit_1(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_CONNECTED_1P, 0, f);
    assert_glyph(f, GLYPH_1, MATRIX_GREEN);
}

static void test_2p_digit_2(void) {
    uint8_t f[16]; matrix_render_frame(STATUS_CONNECTED_2P, 0, f);
    assert_glyph(f, GLYPH_2, MATRIX_GREEN);
}

static void test_searching_orbits(void) {
    uint8_t f[16];
    matrix_render_frame(STATUS_SEARCHING, 0, f);
    TEST_ASSERT_EQUAL_UINT(1, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN, matrix_get_pixel(f, 0, 0));   // RING[0]
    matrix_render_frame(STATUS_SEARCHING, 64, f);                      // pos = (64>>6)%28 = 1
    TEST_ASSERT_EQUAL_UINT(1, count_lit(f));
    TEST_ASSERT_EQUAL_UINT(MATRIX_GREEN, matrix_get_pixel(f, 1, 0));   // RING[1]
}

static void test_error_blinks(void) {
    uint8_t f[16];
    matrix_render_frame(STATUS_ERROR, 0, f);        // (0>>9)&1 == 0 -> blank
    TEST_ASSERT_EQUAL_UINT(0, count_lit(f));
    matrix_render_frame(STATUS_ERROR, 512, f);      // (512>>9)&1 == 1 -> X
    assert_glyph(f, GLYPH_X, MATRIX_RED);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_clear_all_off);
    RUN_TEST(test_set_get_colors);
    RUN_TEST(test_set_off_clears);
    RUN_TEST(test_out_of_range_noop);
    RUN_TEST(test_col_fixup_pinned);
    RUN_TEST(test_boot_blank);
    RUN_TEST(test_1p_digit_1);
    RUN_TEST(test_2p_digit_2);
    RUN_TEST(test_searching_orbits);
    RUN_TEST(test_error_blinks);
    return UNITY_END();
}
