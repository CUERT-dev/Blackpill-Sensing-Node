#include "app.h"
#include "app_adc.h"
#include "main.h"
#include "joystick_diag.h"
#include <stdio.h>
#include <string.h>

extern UART_HandleTypeDef huart2;
static uint32_t last_report;
static uint32_t previous_acquire, previous_tx, previous_rx;
static uint32_t debug_write_fail;
static char report[1024];

/* Bounded, task-context output on the separate debug UART. No ISR prints.
 * Sensing and command transmission can preempt this low-priority task. */
static void write_text(const char *text)
{
  if (HAL_UART_Transmit(&huart2, (const uint8_t *)text,
                        (uint16_t)strlen(text), 100U) != HAL_OK)
  {
    debug_write_fail++;
  }
}

static void hex_bytes(char *out, const uint8_t *bytes, size_t n)
{
  static const char digits[] = "0123456789ABCDEF";
  for (size_t i = 0U; i < n; ++i)
  {
    *out++ = digits[bytes[i] >> 4];
    *out++ = digits[bytes[i] & 15U];
    *out++ = ' ';
  }
  *out = '\0';
}

void App_DebugInit(void)
{
  write_text("\r\nBP BOOT: debug USART2 PA2 TX, 115200 8N1\r\n"
             "BP FW: adc-selftest-v2; LED ON=healthy OFF=fault\r\n"
             "BP LINK: USART1 PA9 TX -> ESP GPIO16 RX; PA10 RX <- ESP GPIO17 TX\r\n"
             "BP NOTE: ADC/command every 10ms, TX every 20ms; reports every 500ms.\r\n"
             "BP NOTE: calibration provisional; RX=0 is normal if ESP sends no reply.\r\n");
  AppAdcSelfTest test;
  App_AdcSelfTestRun(&test);
  (void)snprintf(report, sizeof(report),
    "ADC SELFTEST single-channel PB1=%u status=%u PB0=%u status=%u VREF=%u status=%u restore=%u\r\n"
    "ADC GPIOB MODER=0x%08lX PUPDR=0x%08lX MCU_ID=0x%08lX\r\n"
    "ADC EXPECT: centered 1.567V at 3.3V reference ~=1945 counts; VREF normally nonzero (~1500).\r\n",
    (unsigned)test.throttle, (unsigned)test.throttle_status,
    (unsigned)test.steering, (unsigned)test.steering_status,
    (unsigned)test.vref, (unsigned)test.vref_status, (unsigned)test.restore_status,
    (unsigned long)GPIOB->MODER, (unsigned long)GPIOB->PUPDR,
    (unsigned long)DBGMCU->IDCODE);
  write_text(report);
  if (test.restore_status != HAL_OK)
  {
    write_text("ADC FATAL: scan restore failed; stopping startup.\r\n");
    Error_Handler();
  }
  last_report = HAL_GetTick();
}

void App_DebugPoll(void)
{
  App_UartRxService();
  const uint32_t now = HAL_GetTick();
  if ((uint32_t)(now - last_report) < 500U) { return; }
  const uint32_t elapsed = now - last_report;
  last_report = now;

  AppDiagnostics d;
  App_GetDiagnostics(&d);
  const uint32_t faults = JoyDiag_FaultMask();
  char tx_hex[GP_UART_FRAME_BYTES * 3U + 1U];
  uint8_t rx[16];
  char rx_hex[16U * 3U + 1U];
  const size_t count = (d.rx_bytes < 16U) ? d.rx_bytes : 16U;
  const size_t start = (d.rx_bytes - count) % 16U;
  for (size_t i = 0U; i < count; ++i) { rx[i] = d.rx_tail[(start + i) % 16U]; }
  hex_bytes(tx_hex, d.tx_frame, GP_UART_FRAME_BYTES);
  hex_bytes(rx_hex, rx, count);

  (void)snprintf(report, sizeof(report),
    "BP t=%lu dt=%lu ADC calls=%lu (+%lu) fail=%lu result=%d raw_thr=%u raw_str=%u\r\n"
    "HEALTH=%s faults=0x%02lX input_rail_reject=%lu input=%s\r\n"
    "CMD seq=%u thr=%u str=%d valid=%u arm=%u deadman=%u flags=0x%02X\r\n"
    "TX started=%lu done=%lu (+%lu) busy=%lu error=%lu result=%d lock_fail=%lu\r\n"
    "TX HEX (last started): %s\r\n"
    "RX bytes=%lu (+%lu) restart_fail=%lu UART errors=%lu last_error=0x%08lX tail: %s\r\n"
    "DEBUG write_fail=%lu\r\n",
    (unsigned long)now, (unsigned long)elapsed,
    (unsigned long)d.acquire_count, (unsigned long)(d.acquire_count - previous_acquire),
    (unsigned long)d.acquire_fail, (int)d.last_acquire_result,
    (unsigned)d.raw_throttle, (unsigned)d.raw_steering,
    faults == 0U ? "OK" : "FAULT", (unsigned long)faults,
    (unsigned long)d.input_rail_reject,
    (faults & JD_FAULT_INPUT_PAIR) != 0U ? "BOTH_AXES_SAME_RAIL_CHECK_POWER_PB0_PB1" : "see_ADC",
    (unsigned)d.command.sequence, (unsigned)d.command.throttle_pm,
    (int)d.command.steering_pm, (unsigned)((d.command.flags & GP_FLAG_VALID) != 0U),
    (unsigned)((d.command.flags & GP_FLAG_ARM) != 0U),
    (unsigned)((d.command.flags & GP_FLAG_DEADMAN) != 0U), (unsigned)d.command.flags,
    (unsigned long)d.send_ok, (unsigned long)d.tx_complete,
    (unsigned long)(d.tx_complete - previous_tx), (unsigned long)d.send_busy,
    (unsigned long)d.send_error, (int)d.last_send_result, (unsigned long)d.lock_fail,
    d.send_ok != 0U ? tx_hex : "none",
    (unsigned long)d.rx_bytes, (unsigned long)(d.rx_bytes - previous_rx),
    (unsigned long)d.rx_start_fail, (unsigned long)d.uart_error_count,
    (unsigned long)d.last_uart_error, count != 0U ? rx_hex : "none",
    (unsigned long)debug_write_fail);
  write_text(report);
  previous_acquire = d.acquire_count;
  previous_tx = d.tx_complete;
  previous_rx = d.rx_bytes;
}
