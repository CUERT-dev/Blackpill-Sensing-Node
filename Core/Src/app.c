/**
 * @file    app.c
 * @brief   Blackpill application (maintainer code): joystick acquisition task
 *          and UART TX task on FreeRTOS / CMSIS-RTOS V2. GP API revision 10.
 *
 *   AcquireTask (10 ms) -> latest_command (mutex) -> SendTask (20 ms) -> USART1
 *
 * Libraries (joystick, uart_sender, gp_protocol) are used unchanged.
 * This file owns: ADC callback, UART write callback, arm/deadman switches,
 * calibration, source sequence, tasks, mutex, local diagnostics, LED adapter.
 */
#include "app.h"
#include "app_adc.h"
#include "joystick_diag.h"
#include "main.h"
#include "cmsis_os2.h"
#include "joystick.h"   /* brings gp_types.h: UartSender API is declared there */
#include <string.h>

extern ADC_HandleTypeDef  hadc1;
extern UART_HandleTypeDef huart1;

/* Starting proposals from the task PDF. Not measured. Freeze in GP_CONFIG.md. */
#define ACQUIRE_PERIOD_MS   10U
#define SEND_PERIOD_MS      20U
#define LOCK_TIMEOUT_MS      5U

static AppAdcContext  adc_context = { .adc = &hadc1 };
static UartSender     sender;
static TargetSample   latest_command;     /* starts as neutral invalid stop (all zero) */
static osMutexId_t    latest_mutex;
static uint16_t       sequence = 1U;      /* assigned here, wraps modulo 65536 */
static uint8_t        uart_tx_buffer[GP_UART_FRAME_BYTES];
static volatile AppDiagnostics diag;
static uint8_t uart_rx_byte;

/* PLACEHOLDER values: replace with the measured joystick calibration. */
static const JoystickCalibration calibration = {
  .min      = { 0U,    0U    },
  .center   = { 2048U, 2048U },
  .max      = { 4095U, 4095U },
  .deadband = { 100U,  100U  }
};

static uint32_t MsToTicks(uint32_t ms)
{
  return (uint32_t)(((uint64_t)ms * osKernelGetTickFreq()) / 1000U);
}

/*
 * UART write callback (contract from the task PDF).
 * GP_OK       : whole frame copied into uart_tx_buffer, transmit started.
 * GP_PENDING  : busy, nothing consumed.
 * GP_IO_ERROR : failure.
 * The temporary 'bytes' pointer is never kept. Only SendTask calls this.
 */
static GpResult BlackpillUartWrite(void *user, const uint8_t *bytes, size_t length)
{
  UART_HandleTypeDef *uart = (UART_HandleTypeDef *)user;

  if ((uart == NULL) || (bytes == NULL) || (length == 0U))
  {
    return GP_BAD_ARG;
  }

  if (length > sizeof(uart_tx_buffer))
  {
    return GP_NO_SPACE;
  }

  if (uart->gState != HAL_UART_STATE_READY)
  {
    return GP_PENDING;
  }

  memcpy(uart_tx_buffer, bytes, length);

  const HAL_StatusTypeDef status =
      HAL_UART_Transmit_IT(uart, uart_tx_buffer, (uint16_t)length);

  if (status == HAL_BUSY)
  {
    return GP_PENDING;
  }

  if (status == HAL_OK)
  {
    for (size_t i = 0U; i < length; ++i) { diag.tx_frame[i] = bytes[i]; }
  }

  return (status == HAL_OK) ? GP_OK : GP_IO_ERROR;
}

/* Arm and deadman: switches to ground with pull-up. Pressed = pin low. */
static uint8_t ReadRequestFlags(void)
{
  uint8_t flags = 0U;

  if (HAL_GPIO_ReadPin(GPIOB, Arm_Pin) == GPIO_PIN_RESET)
  {
    flags |= GP_FLAG_ARM;
  }

  if (HAL_GPIO_ReadPin(GPIOB, Deadman_Pin) == GPIO_PIN_RESET)
  {
    flags |= GP_FLAG_DEADMAN;
  }

  return flags;
}

/*
 * Acquisition task, every ACQUIRE_PERIOD_MS.
 * On any failure Joystick_ReadSample leaves a neutral invalid stop packet in
 * 'sample'. It is stored and sent like any other, with its own new sequence.
 */
static void AcquireTask(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();

  for (;;)
  {
    TargetSample sample;
    const uint16_t this_sequence = sequence;

    GpResult result = Joystick_ReadSample(App_AdcRead, &adc_context,
                                                &calibration, this_sequence,
                                                ReadRequestFlags(), &sample);
    /* Diagnostics use X=steering, Y=throttle. */
    JoyDiag_OnAdcSample(adc_context.raw[1], adc_context.raw[0], result == GP_OK);
    /* Treat both axes at the same supply rail as an implausible input pair,
     * not a valid full-steering command. Do not invent a valid neutral input. */
    if (result == GP_OK &&
        ((adc_context.raw[0] <= JD_RAIL_LOW && adc_context.raw[1] <= JD_RAIL_LOW) ||
         (adc_context.raw[0] >= JD_RAIL_HIGH && adc_context.raw[1] >= JD_RAIL_HIGH)))
    {
      sample.throttle_pm = 0U;
      sample.steering_pm = 0;
      sample.flags = 0U;
      result = GP_INVALID;
      diag.input_rail_reject++;
    }
    diag.raw_throttle = adc_context.raw[0];
    diag.raw_steering = adc_context.raw[1];
    diag.command = sample;
    sequence++;

    if (osMutexAcquire(latest_mutex, MsToTicks(LOCK_TIMEOUT_MS)) == osOK)
    {
      latest_command = sample;
      (void)osMutexRelease(latest_mutex);
    }
    else
    {
      diag.lock_fail++;
    }

    diag.acquire_count++;
    diag.last_acquire_result = result;
    diag.last_sequence = this_sequence;
    if (result != GP_OK)
    {
      diag.acquire_fail++;
    }

    next_wake += MsToTicks(ACQUIRE_PERIOD_MS);
    (void)osDelayUntil(next_wake);
  }
}

/*
 * UART TX task, every SEND_PERIOD_MS.
 * Copies the latest command and queues the 14-byte packet. When the link is
 * busy the packet is dropped; the next period sends newer data.
 */
static void SendTask(void *argument)
{
  (void)argument;
  uint32_t next_wake = osKernelGetTickCount();

  for (;;)
  {
    TargetSample copy;

    if (osMutexAcquire(latest_mutex, MsToTicks(LOCK_TIMEOUT_MS)) == osOK)
    {
      copy = latest_command;
      (void)osMutexRelease(latest_mutex);

      const GpResult result = UartSender_Send(&sender, &copy);

      JoyDiag_OnUartTx(result == GP_OK);
      diag.last_send_result = result;
      if (result == GP_OK)
      {
        diag.send_ok++;
      }
      else if (result == GP_PENDING)
      {
        diag.send_busy++;
      }
      else
      {
        diag.send_error++;
      }
    }
    else
    {
      diag.lock_fail++;
    }

    next_wake += MsToTicks(SEND_PERIOD_MS);
    (void)osDelayUntil(next_wake);
  }
}

void App_Init(void)
{
  static const osMutexAttr_t mutex_attr = {
    .name = "latest",
    .attr_bits = osMutexPrioInherit
  };
  static const osThreadAttr_t acquire_attr = {
    .name = "acquire",
    .stack_size = 1536U,   /* bytes; check with osThreadGetStackSpace */
    .priority = (osPriority_t)osPriorityAboveNormal
  };
  static const osThreadAttr_t send_attr = {
    .name = "uart_tx",
    .stack_size = 1536U,
    .priority = (osPriority_t)osPriorityNormal
  };

  if (UartSender_Init(&sender, BlackpillUartWrite, &huart1) != GP_OK)
  {
    Error_Handler();
  }

  latest_mutex = osMutexNew(&mutex_attr);
  if (latest_mutex == NULL)
  {
    Error_Handler();
  }

  if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U) != HAL_OK)
  {
    diag.rx_start_fail++;
  }

  if ((osThreadNew(AcquireTask, NULL, &acquire_attr) == NULL) ||
      (osThreadNew(SendTask,    NULL, &send_attr)    == NULL))
  {
    Error_Handler();
  }
}

void App_GetDiagnostics(AppDiagnostics *out)
{
  if (out == NULL)
  {
    return;
  }

  /* Protect against both task switches and UART callbacks. */
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  *out = diag;
  __set_PRIMASK(mask);
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart == &huart1) { diag.tx_complete++; }
}

void HAL_UART_RxCpltCallback(UART_HandleTypeDef *uart)
{
  if (uart == &huart1)
  {
    diag.rx_tail[diag.rx_bytes % 16U] = uart_rx_byte;
    diag.rx_bytes++;
    /* Rearm only after HAL_UART_IRQHandler returns: restarting here clears
     * ErrorCode before HAL's subsequent ErrorCallback can observe it. */
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *uart)
{
  if (uart == &huart1)
  {
    diag.last_uart_error = HAL_UART_GetError(uart);
    diag.uart_error_count++;
  }
}

/* Rearm after the complete HAL IRQ handler, or recover from the monitor task.
 * The critical section avoids racing a callback or the other caller. */
void App_UartRxService(void)
{
  const uint32_t mask = __get_PRIMASK();
  __disable_irq();
  if (huart1.RxState == HAL_UART_STATE_READY)
  {
    if (HAL_UART_Receive_IT(&huart1, &uart_rx_byte, 1U) != HAL_OK)
    {
      diag.rx_start_fail++;
    }
  }
  __set_PRIMASK(mask);
}

void App_LedSet(bool on)
{
  HAL_GPIO_WritePin(LED_GPIO_Port, LED_Pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
