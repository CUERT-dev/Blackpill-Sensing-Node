#ifndef GP_PROTOCOL_H
#define GP_PROTOCOL_H

#include "gp_types.h"

/* Compatibility names for the existing codec implementation. */
#define GP_PROTOCOL_REV        GP_API_REVISION
#define GP_UART_FRAME_LEN      GP_UART_FRAME_BYTES
#define GP_CAN_ID_COMMAND      GP_CAN_COMMAND_ID

/* UART: 14 bytes, multibyte fields little-endian.
 *
 * 0..1   A5 5A
 * 2      Version 2
 * 3      Kind 1
 * 4      Payload length 7
 * 5..6   Sequence
 * 7..8   Throttle
 * 9..10  Steering
 * 11     Flags
 * 12..13 CRC16-CCITT-FALSE over bytes 2..11
 */
#define GP_UART_SOF0           0xA5u
#define GP_UART_SOF1           0x5Au
#define GP_UART_KIND           1u
#define GP_UART_PAYLOAD_LEN    7u

/* Classic CAN: command ID 0x105, DLC 8.
 * Bytes 0..6: sequence, throttle, steering, flags.
 * Byte 7: CRC8, polynomial 0x07, initial value 0.
 */
#define GP_CAN_DLC             8u

#define GP_THROTTLE_MAX        1000u
#define GP_STEERING_MAX        1000

#define GP_FLAGS_REQUEST_MASK \
    (GP_FLAG_ARM | GP_FLAG_DEADMAN)

#define GP_FLAGS_ALL_MASK \
    (GP_FLAG_VALID | GP_FLAGS_REQUEST_MASK)

/* Types and function declarations come from gp_types.h.
 *
 * UART encoding sets written to zero on failure.
 * Decoders leave the output command unchanged on failure.
 * VALID clear requires zero throttle, steering and flags.
 */

#endif /* GP_PROTOCOL_H */