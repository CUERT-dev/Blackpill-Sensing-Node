#include "uart_sender.h"
GpResult UartSender_Init(UartSender *sender, UartWriteFn write, void *user)
{
    if (sender == NULL) {
        return GP_BAD_ARG;
    }
    else if(write == NULL) {
        sender->write = NULL;
        sender->user = NULL;
        sender->initialized = false;
        return GP_BAD_ARG;
    }
    sender->write = write;
    sender->user = user;
    sender->initialized = true;
    return GP_OK;
}
GpResult UartSender_Send(UartSender *sender, const TargetSample *command)
{
    if (sender == NULL || command == NULL) {
        return GP_BAD_ARG;
    }
    else if (!sender->initialized || sender->write == NULL) {
    return GP_INVALID;
}
    uint8_t buffer[GP_UART_FRAME_LEN];
    size_t len = 0;
    GpResult result = GP_EncodeUart(command, buffer, sizeof(buffer), &len);
    if (result != GP_OK) {
        return result;
    }
    return sender->write(sender->user, buffer, len);
}