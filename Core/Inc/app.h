#ifndef APP_H
#define APP_H

#include <stdbool.h>
#include <stdint.h>
#include "gp_types.h"

/*
 * Blackpill application layer (maintainer code, Ali). GP API revision 10.
 *
 * App_Init(): call once after the RTOS kernel is initialized and before
 * osKernelStart(). Creates the mutex and the acquisition and UART TX tasks.
 */
void App_Init(void);

/*
 * Local diagnostics for Mostafa (separate debug serial, not the command UART).
 */
typedef struct {
  uint32_t acquire_count;       /* Joystick_ReadSample calls                     */
  uint32_t acquire_fail;        /* calls that did not return GP_OK               */
  GpResult last_acquire_result;
  uint32_t send_ok;             /* UartSender_Send returned GP_OK                */
  uint32_t send_busy;           /* GP_PENDING: packet dropped, link busy         */
  uint32_t send_error;          /* any other result                              */
  GpResult last_send_result;
  uint32_t lock_fail;           /* mutex timeout: sample or send skipped         */
  uint16_t last_sequence;       /* sequence of the latest acquisition            */
  uint16_t raw_throttle, raw_steering;
  uint32_t input_rail_reject; /* implausible pair replaced with invalid stop */
  TargetSample command;        /* latest acquired command, including failure stop */
  uint32_t tx_complete;        /* HAL completed a frame on USART1 */
  uint32_t uart_error_count, last_uart_error;
  uint32_t rx_bytes, rx_start_fail;
  uint8_t rx_tail[16];          /* circular history; next slot = rx_bytes % 16 */
  uint8_t tx_frame[GP_UART_FRAME_BYTES]; /* last frame accepted by HAL */
} AppDiagnostics;

/* Snapshot protected against UART interrupts and task switching. */
void App_GetDiagnostics(AppDiagnostics *out);
/* USART2 / PA2 text monitor, 115200 8N1. Call init after UART init,
 * poll only from the default task. Does not write to command USART1. */
void App_DebugInit(void);
void App_DebugPoll(void);
/* Rearm after HAL_UART_IRQHandler, or recover from task context. */
void App_UartRxService(void);

/* Blackpill LED adapter. on = true lights the LED (LED pin is active low). */
void App_LedSet(bool on);

#endif /* APP_H */
