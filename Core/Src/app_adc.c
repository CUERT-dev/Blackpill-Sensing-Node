#include "app_adc.h"
#include <stddef.h>

#define ADC_TIMEOUT_MS 2U

bool App_AdcRead(void *user, uint8_t axis, uint16_t *raw)
{
  AppAdcContext *ctx = (AppAdcContext *)user;
  if (ctx == NULL || ctx->adc == NULL || raw == NULL || axis > 1U) { return false; }

  if (axis == 0U)
  {
    ctx->ready = false;
    for (uint8_t i = 0U; i < 2U; ++i)
    {
      /* Hardware waits between ranks. An IRQ cannot let rank 2 overwrite
       * rank 1 before it is read, unlike the previous free-running scan. */
      if (HAL_ADC_Start(ctx->adc) != HAL_OK ||
          HAL_ADC_PollForConversion(ctx->adc, ADC_TIMEOUT_MS) != HAL_OK)
      {
        (void)HAL_ADC_Stop(ctx->adc);
        return false;
      }
      ctx->raw[i] = (uint16_t)HAL_ADC_GetValue(ctx->adc);
    }
    /* Disabling the ADC resets the sequencer for the next throttle sample. */
    if (HAL_ADC_Stop(ctx->adc) != HAL_OK) { return false; }
    ctx->ready = true;
  }
  if (!ctx->ready) { return false; }
  *raw = ctx->raw[axis];
  return true;
}
