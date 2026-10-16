
#include "joystick.h"

/*
 * Set the output sample to a safe neutral state.
 *
 * This is called before validation so that if any subsequent
 * operation fails, the caller receives a neutral invalid sample.
 *
 * sequence: Acquisition sequence supplied by the caller.
 * out:      Pointer to the output sample.
 */
static void set_neutral(TargetSample *out, uint16_t sequence)
{
    out->sequence    = sequence; /* Preserve the supplied sequence number. */
    out->throttle_pm = 0U;       /* No forward throttle demand. */
    out->steering_pm = 0;        /* No steering-rate demand. */
    out->flags       = 0U;       /* Invalid sample; no requests are active. */
}

/*
 * Validate the joystick calibration before using it.
 *
 * For each axis, the calibration must satisfy:
 *
 *     min < center < max
 *
 * The deadband must be strictly smaller than both:
 *
 *     center - min
 *     max - center
 *
 * This prevents invalid calibration spans and ensures the deadband
 * does not consume the entire travel on either side of the center.
 */
static bool calibration_is_valid(const JoystickCalibration *cal)
{
    uint8_t i;

    /* Validate both joystick axes: 0 = throttle, 1 = steering. */
    for (i = 0U; i < JOYSTICK_AXIS_COUNT; i++)
    {
        /*
         * Check the ordering first.
         * This also prevents unsigned subtraction underflow
         * when calculating the spans below.
         */
        if ((cal->min[i] >= cal->center[i]) ||
            (cal->center[i] >= cal->max[i]))
        {
            return false;
        }

        /*
         * Calculate the ADC distance from the center to each endpoint.
         * Cast to uint32_t before subtraction to make the arithmetic
         * explicitly unsigned and wide enough for these ADC values.
         */
        const uint32_t lower_span =
            (uint32_t)cal->center[i] - (uint32_t)cal->min[i];

        const uint32_t upper_span =
            (uint32_t)cal->max[i] - (uint32_t)cal->center[i];

        /*
         * The deadband must be strictly smaller than both spans.
         * Equality is invalid because it leaves no usable travel
         * on the corresponding side of the joystick.
         */
        if (((uint32_t)cal->deadband[i] >= lower_span) ||
            ((uint32_t)cal->deadband[i] >= upper_span))
        {
            return false;
        }
    }

    /* Both axes have valid calibration parameters. */
    return true;
}

/*
 * Convert an ADC displacement into a per-mille command.
 *
 * offset: Displacement from the start of the active range.
 * span:   Total displacement from the start to the endpoint.
 *
 * Returns a value from 0 to JOYSTICK_FULL_PM (1000).
 *
 * Values outside the active range are clamped:
 *   offset <= 0  -> 0
 *   offset >= span -> 1000
 *
 * Integer arithmetic is used, so the result is truncated
 * rather than rounded.
 */
static int32_t scale_pm(int32_t offset, int32_t span)
{
    /* Invalid or non-positive displacement produces zero demand. */
    if ((offset <= 0) || (span <= 0))
    {
        return 0;
    }

    /* Clamp the command at the maximum endpoint. */
    if (offset >= span)
    {
        return JOYSTICK_FULL_PM;
    }

    /*
     * Linear normalization:
     *
     * command = offset / span * 1000
     *
     * Multiplication is performed before division to preserve
     * more precision in the integer calculation.
     */
    return (offset * JOYSTICK_FULL_PM) / span;
}

/*
 * Normalize the throttle axis.
 *
 * Throttle is forward-only:
 *   - At or below center + deadband: 0
 *   - Above center + deadband:      0..1000
 *   - At or above max:              1000
 *
 * Reverse joystick travel never produces negative throttle.
 */
static uint16_t normalize_throttle(
    uint16_t raw,
    const JoystickCalibration *cal)
{
    /*
     * The active throttle range begins after the center deadband.
     * No forward command is generated inside the deadband.
     */
    const int32_t start =
        (int32_t)cal->center[0] + (int32_t)cal->deadband[0];

    /*
     * Normalize the distance from 'start' to the calibrated maximum.
     * scale_pm() handles values below the threshold and clamps
     * values beyond the maximum.
     */
    return (uint16_t)scale_pm(
        (int32_t)raw - start,
        (int32_t)cal->max[0] - start
    );
}

/*
 * Normalize the steering axis.
 *
 * Steering is signed:
 *   - Above the deadband: positive demand.
 *   - Below the deadband: negative demand.
 *   - Inside the deadband: zero demand.
 *
 * The left and right sides are normalized independently because
 * their calibrated travel spans may be different.
 */
static int16_t normalize_steering(
    uint16_t raw,
    const JoystickCalibration *cal)
{
    /* Convert the ADC count to signed arithmetic for comparisons. */
    const int32_t value = (int32_t)raw;

    /* Upper and lower edges of the steering deadband. */
    const int32_t upper =
        (int32_t)cal->center[1] + (int32_t)cal->deadband[1];

    const int32_t lower =
        (int32_t)cal->center[1] - (int32_t)cal->deadband[1];

    /*
     * Positive steering:
     * Scale the displacement above the upper deadband boundary
     * toward the calibrated maximum.
     */
    if (value > upper)
    {
        return (int16_t)scale_pm(
            value - upper,
            (int32_t)cal->max[1] - upper
        );
    }

    /*
     * Negative steering:
     * Scale the displacement below the lower deadband boundary
     * toward the calibrated minimum.
     *
     * The minus sign converts the positive magnitude returned
     * by scale_pm() into a negative steering command.
     */
    if (value < lower)
    {
        return (int16_t)(
            -scale_pm(
                lower - value,
                lower - (int32_t)cal->min[1]
            )
        );
    }

    /* The joystick is within the deadband: no steering demand. */
    return 0;
}

/*
 * Public API: read both joystick axes and create one TargetSample.
 *
 * read:          ADC callback supplied by the maintainer.
 * user:          Opaque context passed to the callback.
 * cal:           Measured calibration parameters for both axes.
 * sequence:      Acquisition sequence supplied by the caller.
 * request_flags: Only ARM and DEADMAN request bits are accepted.
 * out:           Destination for the normalized command.
 *
 * Success:
 *   - Reads throttle and steering.
 *   - Normalizes both axes.
 *   - Sets GP_FLAG_VALID and the accepted request flags.
 *
 * Failure:
 *   - Returns an appropriate GpResult error.
 *   - Leaves a valid output pointer containing a neutral,
 *     invalid sample with the supplied sequence number.
 *
 * This function does not configure the ADC, transmit UART data,
 * control motors, allocate memory, or increment the sequence.
 */
GpResult Joystick_ReadSample(
    AdcReadFn read,
    void *user,
    const JoystickCalibration *cal,
    uint16_t sequence,
    uint8_t request_flags,
    TargetSample *out)
{
    /* Temporary storage for the two raw ADC readings. */
    uint16_t raw_throttle = 0U;
    uint16_t raw_steering = 0U;

    /*
     * The output pointer must be checked first because it cannot
     * be initialized safely when it is NULL.
     */
    if (out == NULL)
    {
        return GP_BAD_ARG;
    }

    /*
     * Initialize the output to a neutral invalid sample before
     * any other validation or ADC operation.
     */
    set_neutral(out, sequence);

    /* The callback and calibration pointer are required. */
    if ((read == NULL) || (cal == NULL))
    {
        return GP_BAD_ARG;
    }

    /*
     * Accept only GP_FLAG_ARM and GP_FLAG_DEADMAN as request bits.
     * GP_FLAG_VALID is set internally after successful acquisition.
     * Reserved bits must not be accepted.
     */
    if ((request_flags & (uint8_t)(~JOYSTICK_REQUEST_MASK)) != 0U)
    {
        return GP_BAD_ARG;
    }

    /* Reject invalid calibration before performing ADC reads. */
    if (!calibration_is_valid(cal))
    {
        return GP_INVALID;
    }

    /*
     * Read axis 0: throttle.
     * The callback decides how the actual ADC hardware is accessed.
     */
    if (!read(user, 0U, &raw_throttle))
    {
        return GP_IO_ERROR;
    }

    /*
     * Read axis 1: steering.
     * If this read fails, the output remains neutral and invalid.
     */
    if (!read(user, 1U, &raw_steering))
    {
        return GP_IO_ERROR;
    }

    /*
     * Both ADC reads succeeded.
     * Convert raw ADC counts into normalized command values.
     */
    out->throttle_pm = normalize_throttle(raw_throttle, cal);
    out->steering_pm = normalize_steering(raw_steering, cal);

    /*
     * Mark the sample valid and copy the operator's accepted
     * ARM/DEADMAN request bits.
     *
     * Reserved flag bits remain zero because they were rejected
     * during validation.
     */
    out->flags = (uint8_t)(GP_FLAG_VALID | request_flags);

    /* The sample has been acquired and normalized successfully. */
    return GP_OK;
}
