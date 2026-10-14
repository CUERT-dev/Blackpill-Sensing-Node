/*
 * Integration example. `maintainer_uart_write` is supplied by Ali/the
 * maintainer and must copy the complete frame into a bounded TX queue before
 * returning GP_OK. This example deliberately does not initialize UART/HAL.
 */
#include "uart_sender.h"

/* Implement this adapter in the hardware/application layer. */
extern GpResult maintainer_uart_write(void *user,
                                      const uint8_t *bytes,
                                      size_t length);

GpResult uart_sender_example(void *uart_queue_context)
{
    UartSender sender = {0};
    TargetSample command = {
        .sequence = 1u,
        .throttle_pm = 0u,
        .steering_pm = 0,
        .flags = GP_FLAG_VALID
    };

    GpResult result = UartSender_Init(
    &sender, maintainer_uart_write, uart_queue_context);

if (result != GP_OK) {
    return result;
}
    /* Caller should handle GP_PENDING / GP_IO_ERROR; no retry is done here. */
   return UartSender_Send(&sender, &command);
}
