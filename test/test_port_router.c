#include "unity/unity.h"
#include "port_router.h"

void setUp(void) {}
void tearDown(void) {}

static const void *A = (const void *)1;
static const void *B = (const void *)2;
static const void *C = (const void *)3;

static void test_first_gets_port0_second_port1(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));
    TEST_ASSERT_EQUAL_INT(1, port_router_assign(&r, B));
}

static void test_full_returns_none(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);
    port_router_assign(&r, B);
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_assign(&r, C));
}

static void test_reassign_same_device_is_idempotent(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, A));  // same port
}

static void test_lookup(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);
    port_router_assign(&r, B);
    TEST_ASSERT_EQUAL_INT(0, port_router_lookup(&r, A));
    TEST_ASSERT_EQUAL_INT(1, port_router_lookup(&r, B));
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, C));
}

static void test_release_frees_lowest_slot_for_reuse(void) {
    port_router_t r; port_router_init(&r);
    port_router_assign(&r, A);           // port 0
    port_router_assign(&r, B);           // port 1
    port_router_release(&r, A);          // free port 0
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, A));
    TEST_ASSERT_EQUAL_INT(0, port_router_assign(&r, C));   // C reuses port 0
    TEST_ASSERT_EQUAL_INT(1, port_router_lookup(&r, B));   // B untouched
}

static void test_null_and_unknown_are_safe(void) {
    port_router_t r; port_router_init(&r);
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_assign(&r, NULL));
    TEST_ASSERT_EQUAL_INT(PORT_NONE, port_router_lookup(&r, NULL));
    port_router_release(&r, NULL);       // no crash
    port_router_release(&r, C);          // unknown: no crash
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_first_gets_port0_second_port1);
    RUN_TEST(test_full_returns_none);
    RUN_TEST(test_reassign_same_device_is_idempotent);
    RUN_TEST(test_lookup);
    RUN_TEST(test_release_frees_lowest_slot_for_reuse);
    RUN_TEST(test_null_and_unknown_are_safe);
    return UNITY_END();
}
