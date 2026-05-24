// Test the pure state-selection logic (not hardware output)
#include <unity.h>
#include "led_driver.h"

void setUp(void) {}
void tearDown(void) {}

// Test that state enum values are what we expect
void test_state_values_distinct() {
    TEST_ASSERT_NOT_EQUAL(LED_STATE_PROGRAM, LED_STATE_PREVIEW);
    TEST_ASSERT_NOT_EQUAL(LED_STATE_PROGRAM, LED_STATE_STANDBY);
    TEST_ASSERT_NOT_EQUAL(LED_STATE_AMBER_BREATH, LED_STATE_WHITE_BREATH);
}

// Test LedSettings default-initialises safely
void test_settings_struct_size() {
    LedSettings s = {};
    TEST_ASSERT_EQUAL(0, s.brightness);
    TEST_ASSERT_EQUAL(0, s.standbyBrightness);
    TEST_ASSERT_EQUAL(0, s.animSpeedMs);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_state_values_distinct);
    RUN_TEST(test_settings_struct_size);
    return UNITY_END();
}
