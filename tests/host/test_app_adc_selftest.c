#ifdef NDEBUG
#undef NDEBUG
#endif
#include "app_adc.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

ADC_HandleTypeDef hadc1;
static unsigned init_calls, config_calls, starts, polls, delays;
static unsigned fail_poll, fail_init;
static uint32_t selected;
static ADC_ChannelConfTypeDef configs[5];

HAL_StatusTypeDef HAL_ADC_Init(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1);
  return ++init_calls == fail_init ? HAL_ERROR : HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_ConfigChannel(ADC_HandleTypeDef *adc, ADC_ChannelConfTypeDef *c)
{
  assert(adc == &hadc1 && config_calls < 5U);
  configs[config_calls++] = *c;
  selected = c->Channel;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Start(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1 && adc->Init.NbrOfConversion == 1U &&
         adc->Init.ScanConvMode == DISABLE && adc->Init.DiscontinuousConvMode == DISABLE);
  ++starts;
  return HAL_OK;
}
HAL_StatusTypeDef HAL_ADC_Stop(ADC_HandleTypeDef *adc)
{ assert(adc == &hadc1); return HAL_OK; }
HAL_StatusTypeDef HAL_ADC_PollForConversion(ADC_HandleTypeDef *adc, uint32_t timeout)
{
  assert(adc == &hadc1 && timeout == 5U);
  return ++polls == fail_poll ? HAL_TIMEOUT : HAL_OK;
}
uint32_t HAL_ADC_GetValue(ADC_HandleTypeDef *adc)
{
  assert(adc == &hadc1);
  return selected == ADC_CHANNEL_VREFINT ? 1500U : (selected == ADC_CHANNEL_9 ? 1945U : 1946U);
}
void HAL_Delay(uint32_t delay) { assert(delay == 1U); ++delays; }

int main(void)
{
  const ADC_InitTypeDef original = { ENABLE, ENABLE, 1U, 2U };
  for (unsigned mode = 0U; mode < 3U; ++mode)
  {
    init_calls = config_calls = starts = polls = delays = 0U;
    fail_poll = mode == 1U ? 2U : 0U;
    fail_init = mode == 2U ? 1U : 0U;
    hadc1.Init = original;
    AppAdcSelfTest t;
    App_AdcSelfTestRun(&t);
    assert(t.restore_status == HAL_OK);
    assert(memcmp(&hadc1.Init, &original, sizeof(original)) == 0);
    assert(init_calls == 2U);
    /* Whatever happens to a measurement, restore both original rank mappings. */
    assert(configs[config_calls - 2U].Channel == ADC_CHANNEL_9 &&
           configs[config_calls - 2U].Rank == 1U);
    assert(configs[config_calls - 1U].Channel == ADC_CHANNEL_8 &&
           configs[config_calls - 1U].Rank == 2U);
    assert(configs[config_calls - 1U].SamplingTime == ADC_SAMPLETIME_84CYCLES);
    if (mode == 2U) { assert(starts == 0U); }
    else
    {
      assert(starts == 3U && delays == 3U);
      assert(t.throttle == 1945U && t.vref == 1500U);
      assert(t.steering_status == (mode == 1U ? HAL_TIMEOUT : HAL_OK));
      if (mode == 0U) { assert(t.steering == 1946U); }
    }
  }
  puts("ADC selftest restoration tests: PASS");
  return 0;
}
