#ifndef APP_ADC_H
#define APP_ADC_H
#include "stm32f4xx_hal.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
  ADC_HandleTypeDef *adc;
  uint16_t raw[2]; /* throttle PB1/rank 1, steering PB0/rank 2 */
  bool ready;
} AppAdcContext;

/* Requires a two-rank discontinuous scan with one conversion per trigger. */
bool App_AdcRead(void *user, uint8_t axis, uint16_t *raw);

/* Boot-only independent channel/reference test; restores the running scan. */
typedef struct {
  uint16_t throttle, steering, vref;
  uint8_t throttle_status, steering_status, vref_status, restore_status;
} AppAdcSelfTest;
void App_AdcSelfTestRun(AppAdcSelfTest *out);
#endif
