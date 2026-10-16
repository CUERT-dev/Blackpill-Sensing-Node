/**
 * @file    joystick.h
 * @brief   ADC joystick calibration and normalization (GP API revision 10).
 *
 * Reads the two joystick axes through an ADC callback supplied by the
 * maintainer (Ali) and turns them into one TargetSample. This module does
 * not touch the ADC, UART or motors, and has no HAL dependency.
 */
#ifndef JOYSTICK_H
#define JOYSTICK_H

#include "gp_types.h"

#define JOYSTICK_AXIS_COUNT   2U
#define JOYSTICK_FULL_PM      1000
#define JOYSTICK_REQUEST_MASK 0x06U

/**
 * @brief  Read both joystick axes and produce one normalized TargetSample.
 *
 * The ADC callback is called once per axis: axis 0 = throttle, axis 1 =
 * steering. Raw values are ADC counts.
 *
 * Calibration rules (cal, for each axis, raw ADC counts):
 *   min < center < max
 *   deadband < (center - min)  and  deadband < (max - center)
 *
 * Normalization:
 *   - Throttle: raw <= center + deadband gives 0 (this includes all reverse
 *     travel). Above that, scales from center + deadband to max onto 0..1000
 *     and clamps at 1000.
 *   - Steering: raw within center +/- deadband gives 0. Above it, scales
 *     0..+1000 over the span up to max. Below it, scales 0..-1000 over the
 *     span down to min. The two sides use their own spans. Clamped at the
 *     endpoints.
 *
 * Flags in out: GP_FLAG_VALID, plus the GP_FLAG_ARM and GP_FLAG_DEADMAN bits
 * given in request_flags. Bits 3..7 are always zero.
 *
 * On every failure below, out (when not NULL) holds a neutral invalid stop
 * packet: the given sequence, throttle_pm 0, steering_pm 0, flags 0.
 *
 * @param  read           ADC callback from Ali. Returns false if a read
 *                        fails. Must not be NULL.
 * @param  user           Opaque context passed back to read. May be NULL if
 *                        the callback permits it.
 * @param  cal            Measured endpoints and deadband (not modified).
 * @param  sequence       Acquisition sequence from Ali. Copied into out, not
 *                        incremented here.
 * @param  request_flags  Operator request bits. Only GP_FLAG_ARM and
 *                        GP_FLAG_DEADMAN are accepted.
 * @param  out            Caller storage for the result.
 *
 * @retval GP_OK        out holds a valid sample.
 * @retval GP_BAD_ARG   read, cal or out is NULL, or request_flags contains
 *                      any bit other than GP_FLAG_ARM and GP_FLAG_DEADMAN.
 * @retval GP_INVALID   The calibration breaks the rules above.
 * @retval GP_IO_ERROR  The ADC callback returned false for an axis.
 *
 * Units: throttle_pm is per mille, 0..1000 forward demand. steering_pm is
 * per mille, -1000..1000 signed rate demand.
 */
GpResult Joystick_ReadSample(AdcReadFn read, void *user,
                             const JoystickCalibration *cal, uint16_t sequence,
                             uint8_t request_flags, TargetSample *out);

#endif /* JOYSTICK_H */