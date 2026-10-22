#ifdef NDEBUG
#undef NDEBUG
#endif
#include "app_adc.h"
#include "joystick.h"
#include <assert.h>
#include <stdio.h>

static ADC_HandleTypeDef adc;
static unsigned starts, polls, stops, reads;
static unsigned fail_start, fail_poll;
static bool fail_stop, conversion_ready;
static const uint16_t values[2] = { 1234U, 3456U };

static void reset(void)
{
  starts = polls = stops = reads = fail_start = fail_poll = 0U;
  fail_stop = conversion_ready = false;
}

HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *handle)
{
  assert(handle == &adc);
  /* Each conversion must have been read before another trigger. */
  assert(!conversion_ready);
  ++starts;
  if (starts == fail_start) { return HAL_ERROR; }
  conversion_ready = true;
  return HAL_OK;
}

HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *handle, uint32_t timeout)
{
  assert(handle == &adc && timeout == 2U && conversion_ready);
  ++polls;
  return polls == fail_poll ? HAL_TIMEOUT : HAL_OK;
}

uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *handle)
{
  assert(handle == &adc && conversion_ready);
  conversion_ready = false;
  return values[reads++ % 2U];
}

HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *handle)
{
  assert(handle == &adc);
  ++stops;
  conversion_ready = false;
  return fail_stop ? HAL_ERROR : HAL_OK;
}

static void test_independent_channels_and_cache(void)
{
  reset();
  AppAdcContext ctx = { .adc = &adc };
  uint16_t raw = 99U;
  assert(!App_AdcRead(&ctx, 1U, &raw)); /* no stale steering before first scan */
  assert(raw == 99U);
  assert(App_AdcRead(&ctx, 0U, &raw) && raw == 1234U);
  assert(starts == 2U && polls == 2U && stops == 1U && reads == 2U);
  assert(App_AdcRead(&ctx, 1U, &raw) && raw == 3456U);
  assert(starts == 2U); /* steering uses the same pair, not a new conversion */
}

static void test_failures_invalidate_cache_and_recover(void)
{
  for (unsigned stage = 0U; stage < 5U; ++stage)
  {
    reset();
    AppAdcContext ctx = { .adc = &adc };
    uint16_t raw;
    assert(App_AdcRead(&ctx, 0U, &raw));
    reset();
    if (stage < 2U) { fail_start = stage + 1U; }
    else if (stage < 4U) { fail_poll = stage - 1U; }
    else { fail_stop = true; }
    assert(!App_AdcRead(&ctx, 0U, &raw));
    assert(stops == 1U);
    assert(!App_AdcRead(&ctx, 1U, &raw));
    reset();
    assert(App_AdcRead(&ctx, 0U, &raw) && raw == values[0]);
    assert(App_AdcRead(&ctx, 1U, &raw) && raw == values[1]);
  }
}

static void test_partial_scan_becomes_invalid_stop(void)
{
  reset();
  fail_poll = 2U;
  AppAdcContext ctx = { .adc = &adc };
  const JoystickCalibration cal = {
    .min = {0U, 0U}, .center = {2048U, 2048U},
    .max = {4095U, 4095U}, .deadband = {100U, 100U}
  };
  TargetSample sample = { .throttle_pm = 1000U, .steering_pm = 1000, .flags = 7U };
  assert(Joystick_ReadSample(App_AdcRead, &ctx, &cal, 42U,
                            GP_FLAG_ARM | GP_FLAG_DEADMAN, &sample) == GP_IO_ERROR);
  assert(sample.sequence == 42U && sample.throttle_pm == 0U &&
         sample.steering_pm == 0 && sample.flags == 0U);
}

int main(void)
{
  test_independent_channels_and_cache();
  test_failures_invalidate_cache_and_recover();
  test_partial_scan_becomes_invalid_stop();
  puts("app ADC tests: PASS");
  return 0;
}
