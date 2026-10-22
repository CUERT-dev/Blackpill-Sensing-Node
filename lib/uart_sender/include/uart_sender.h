#ifndef UART_SENDER_H
#define UART_SENDER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "gp-protocol.h"
/*
 * Caller-owned state: zero-initialize and initialize before Send.
 * One writer; serialize access if shared between tasks.
 *
 * Init returns GP_BAD_ARG for NULL sender/write.
 * NULL write clears the sender configuration.
 *
 * Send returns GP_BAD_ARG for NULL sender/command.
 * Send returns GP_INVALID for an uninitialized sender,
 * missing callback, or invalid command.
 *
 * On successful encoding, Send calls write exactly once
 * and returns its result unchanged. No retries or waits.
 *
 * Callback:
 * GP_OK means the entire frame was copied into a bounded
 * adapter-owned TX buffer/queue.
 * GP_PENDING means busy and nothing consumed.
 * GP_IO_ERROR means failure.
 *
 * Never retain the temporary bytes pointer for DMA.
 * user may be NULL if the callback permits it.
 */

GpResult UartSender_Init(UartSender *sender,
 UartWriteFn write, void *user);
GpResult UartSender_Send(UartSender *sender,
 const TargetSample *command);

#endif // UART_SENDER_H
