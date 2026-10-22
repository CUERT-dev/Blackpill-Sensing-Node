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
} AppDiagnostics;

/* Safe snapshot: copied with the scheduler locked, so fields are consistent. */
void App_GetDiagnostics(AppDiagnostics *out);

/* Blackpill LED adapter. on = true lights the LED (LED pin is active low). */
void App_LedSet(bool on);

#endif /* APP_H */
