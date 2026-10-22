#include "app_adc.h"
#include <stddef.h>

extern ADC_HandleTypeDef hadc1;

static HAL_StatusTypeDef read_channel(uint32_t channel, uint16_t *raw)
{
  ADC_ChannelConfTypeDef config = {0};
  config.Channel = channel;
  config.Rank = 1U;
  config.SamplingTime = ADC_SAMPLETIME_480CYCLES;
  HAL_StatusTypeDef status = HAL_ADC_ConfigChannel(&hadc1, &config);
  if (status != HAL_OK) { return status; }
  /* Also gives the internal reference time to stabilize after enabling. */
  HAL_Delay(1U);
  status = HAL_ADC_Start(&hadc1);
  if (status == HAL_OK) { status = HAL_ADC_PollForConversion(&hadc1, 5U); }
  if (status == HAL_OK) { *raw = (uint16_t)HAL_ADC_GetValue(&hadc1); }
  const HAL_StatusTypeDef stop = HAL_ADC_Stop(&hadc1);
  return status == HAL_OK ? stop : status;
}

void App_AdcSelfTestRun(AppAdcSelfTest *out)
{
  if (out == NULL) { return; }
  *out = (AppAdcSelfTest){ .throttle_status = HAL_ERROR,
    .steering_status = HAL_ERROR, .vref_status = HAL_ERROR,
    .restore_status = HAL_ERROR };
  const ADC_InitTypeDef saved = hadc1.Init;
  HAL_StatusTypeDef status = HAL_ADC_Stop(&hadc1);
  if (status != HAL_OK) { return; }
  hadc1.Init.ScanConvMode = DISABLE;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.NbrOfDiscConversion = 0U;
  hadc1.Init.NbrOfConversion = 1U;
  if (HAL_ADC_Init(&hadc1) == HAL_OK)
  {
    out->throttle_status = (uint8_t)read_channel(ADC_CHANNEL_9, &out->throttle);
    out->steering_status = (uint8_t)read_channel(ADC_CHANNEL_8, &out->steering);
    out->vref_status = (uint8_t)read_channel(ADC_CHANNEL_VREFINT, &out->vref);
  }

  /* Restore every sequencer setting touched by this test before tasks run. */
  hadc1.Init = saved;
  status = HAL_ADC_Init(&hadc1);
  ADC_ChannelConfTypeDef config = {0};
  config.SamplingTime = ADC_SAMPLETIME_84CYCLES;
  config.Channel = ADC_CHANNEL_9;
  config.Rank = 1U;
  if (status == HAL_OK) { status = HAL_ADC_ConfigChannel(&hadc1, &config); }
  config.Channel = ADC_CHANNEL_8;
  config.Rank = 2U;
  if (status == HAL_OK) { status = HAL_ADC_ConfigChannel(&hadc1, &config); }
  out->restore_status = (uint8_t)status;
}
