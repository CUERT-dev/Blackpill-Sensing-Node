#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "joystick.h"

/* Mock ADC context to feed controlled raw ADC values to Joystick_ReadSample */
typedef struct {
    uint16_t raw_throttle;
    uint16_t raw_steering;
    bool throttle_fail;
    bool steering_fail;
} MockAdc;

static bool mock_adc_read(void *user, uint8_t axis, uint16_t *raw)
{
    assert(user != NULL);
    assert(raw != NULL);
    MockAdc *adc = (MockAdc *)user;

    if (axis == 0U) { /* Throttle */
        if (adc->throttle_fail) {
            return false;
        }
        *raw = adc->raw_throttle;
        return true;
    } else if (axis == 1U) { /* Steering */
        if (adc->steering_fail) {
            return false;
        }
        *raw = adc->raw_steering;
        return true;
    }

    return false;
}

/* Standard calibration: 12-bit ADC (0..4095), center 2048, deadband 100 */
static const JoystickCalibration standard_cal = {
    .min = {0U, 0U},
    .center = {2048U, 2048U},
    .max = {4095U, 4095U},
    .deadband = {100U, 100U}
};

/* Test 1: Exactly centered joystick produces 0 throttle and 0 steering */
static void test_centered_joystick(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 2048U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 100U, 0U, &out);

    assert(res == GP_OK);
    assert(out.sequence == 100U);
    assert(out.throttle_pm == 0U);
    assert(out.steering_pm == 0);
    assert(out.flags == GP_FLAG_VALID);
    puts("  [PASS] UT-001a: Centered joystick produces zero demand");
}

/* Test 2: Input inside deadband remains strictly zero */
static void test_deadband_suppression(void)
{
    /* Threshold for throttle: center (2048) + deadband (100) = 2148 */
    MockAdc adc = { .raw_throttle = 2140U, .raw_steering = 2100U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 101U, 0U, &out);

    assert(res == GP_OK);
    assert(out.throttle_pm == 0U);
    assert(out.steering_pm == 0);
    assert(out.flags == GP_FLAG_VALID);
    puts("  [PASS] UT-001a_deadband: Deadband suppression verified");
}

/* Test 3: Forward throttle scales linearly */
static void test_forward_throttle_scaling(void)
{
    /* Active span: 2148 to 4095 = 1947 counts. Midpoint = 2148 + 973 = 3121 counts */
    MockAdc adc = { .raw_throttle = 3121U, .raw_steering = 2048U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 102U, 0U, &out);

    assert(res == GP_OK);
    assert(out.throttle_pm >= 495U && out.throttle_pm <= 505U); /* ~500 pm */
    assert(out.steering_pm == 0);
    puts("  [PASS] UT-001b: Forward throttle linear scaling verified");
}

/* Test 4: Maximum forward throttle clamps at 1000 pm */
static void test_max_throttle_clamping(void)
{
    MockAdc adc = { .raw_throttle = 4095U, .raw_steering = 2048U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 103U, 0U, &out);

    assert(res == GP_OK);
    assert(out.throttle_pm == 1000U);
    puts("  [PASS] UT-001c: Maximum throttle clamping at 1000 pm verified");
}

/* Test 5: Reverse deflection is completely suppressed (clamps to 0) */
static void test_reverse_throttle_suppressed(void)
{
    MockAdc adc = { .raw_throttle = 1000U, .raw_steering = 2048U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 104U, 0U, &out);

    assert(res == GP_OK);
    assert(out.throttle_pm == 0U);
    puts("  [PASS] UT-001d: Reverse throttle deflection produces 0 pm");
}

/* Test 6: Steering left span scales down to -1000 pm */
static void test_steering_left_span(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 0U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 105U, 0U, &out);

    assert(res == GP_OK);
    assert(out.steering_pm == -1000);
    puts("  [PASS] UT-001e: Steering left endpoint reaches -1000 pm");
}

/* Test 7: Steering right span scales up to +1000 pm */
static void test_steering_right_span(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 4095U };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 106U, 0U, &out);

    assert(res == GP_OK);
    assert(out.steering_pm == 1000);
    puts("  [PASS] UT-001f: Steering right endpoint reaches +1000 pm");
}

/* Test 8: Operator request flags filtering (accepts ARM and DEADMAN, rejects reserved) */
static void test_flags_handling(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 2048U };
    TargetSample out = {0};

    /* Accept valid ARM and DEADMAN flags */
    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 107U,
                                       GP_FLAG_ARM | GP_FLAG_DEADMAN, &out);
    assert(res == GP_OK);
    assert(out.flags == (GP_FLAG_VALID | GP_FLAG_ARM | GP_FLAG_DEADMAN));

    /* Reject reserved flag bit 0x08 */
    res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 108U, 0x08U, &out);
    assert(res == GP_BAD_ARG);
    assert(out.flags == 0U); /* Neutral invalid stop frame emitted */
    puts("  [PASS] UT-001g: Operator flags filtering and reserved bits rejection verified");
}

/* Test 9: ADC read failure emits safe invalid neutral sample */
static void test_adc_read_failure(void)
{
    MockAdc adc = { .raw_throttle = 3000U, .raw_steering = 3000U, .throttle_fail = true };
    TargetSample out = {0};

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 109U, GP_FLAG_ARM, &out);

    assert(res == GP_IO_ERROR);
    assert(out.sequence == 109U);
    assert(out.throttle_pm == 0U);
    assert(out.steering_pm == 0);
    assert(out.flags == 0U); /* Invalid stop */
    puts("  [PASS] UT-001h: ADC read failure emits safe neutral stop sample");
}

/* Test 10: Invalid calibration rejection (min >= center or deadband >= span) */
static void test_invalid_calibration(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 2048U };
    TargetSample out = {0};

    /* Bad calibration: deadband is larger than span */
    JoystickCalibration bad_cal = standard_cal;
    bad_cal.deadband[0] = 3000U;

    GpResult res = Joystick_ReadSample(mock_adc_read, &adc, &bad_cal, 110U, 0U, &out);

    assert(res == GP_INVALID);
    assert(out.throttle_pm == 0U);
    assert(out.flags == 0U);
    puts("  [PASS] UT-001i: Invalid calibration geometry rejected with GP_INVALID");
}

/* Test 11: Null pointer defense */
static void test_null_pointers(void)
{
    MockAdc adc = { .raw_throttle = 2048U, .raw_steering = 2048U };
    TargetSample out = {0};

    assert(Joystick_ReadSample(NULL, &adc, &standard_cal, 1U, 0U, &out) == GP_BAD_ARG);
    assert(Joystick_ReadSample(mock_adc_read, &adc, NULL, 1U, 0U, &out) == GP_BAD_ARG);
    assert(Joystick_ReadSample(mock_adc_read, &adc, &standard_cal, 1U, 0U, NULL) == GP_BAD_ARG);
    puts("  [PASS] UT-001j: Null pointer arguments safely rejected with GP_BAD_ARG");
}

int main(void)
{
    puts("==================================================");
    puts("Running Boody Joystick ADC Unit Tests (API Rev 10)");
    puts("==================================================");

    test_centered_joystick();
    test_deadband_suppression();
    test_forward_throttle_scaling();
    test_max_throttle_clamping();
    test_reverse_throttle_suppressed();
    test_steering_left_span();
    test_steering_right_span();
    test_flags_handling();
    test_adc_read_failure();
    test_invalid_calibration();
    test_null_pointers();

    puts("==================================================");
    puts("ALL BOODY JOYSTICK TESTS: PASS");
    puts("==================================================");
    return 0;
}
