/**
  ******************************************************************************
  * @file    joystick_diag.h
  * @brief   Blackpill joystick-path debug LEDs (4 debug pins + 1 heartbeat LED)
  * @owner   Mostafa (review / diagnostics)
  *
  * PURPOSE
  *   While you move the joystick, four LEDs show WHERE the command path stops:
  *
  *     LED1  ADC    : lights on every good joystick ADC sample
  *     LED2  MOVE   : (optional, off by default) any axis off neutral
  *     LED3  TX     : lights on every frame the UART TX callback ACCEPTED
  *     LED4  FAULT  : lights on ADC fail / axis stuck at rail / stale / TX fail
  *     DIR   LEFT / RIGHT : steering axis pushed left / right
  *     DIR   FWD  / BACK  : throttle axis pushed forward / back
  *   Any LED can be removed to free its pin: set its PORT to NULL.
  *
  *   Rules from the GP brief that this module follows:
  *     - hooks are non-blocking, no printing, no HAL calls, ISR-safe
  *     - no motor outputs, never touches the binary control UART
  *     - local fault is polled at most every 50 ms (JoyDiag_Poll rate-limits)
  *     - heartbeat LED: 500 ms toggle healthy, 250 ms toggle fault/stale
  *
  * WIRING (each debug pin):  MCU pin -> 330 R..1 k resistor -> LED anode,
  *                            LED cathode -> GND.   (3.3 V logic, ~3-8 mA)
  *
  * !! The default pins below are PLACEHOLDERS. Check them against Youanna's
  * !! docs/hardware/PINOUT.md and change ONLY the four JD_LEDx_* lines.
  ******************************************************************************
  */
#ifndef JOYSTICK_DIAG_H
#define JOYSTICK_DIAG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ---------------------------------------------------------------------------
 * 1) PIN MAP - edit here only (must be UNUSED pins on the approved pinout)
 * ------------------------------------------------------------------------- */
#define JD_LED1_ADC_PORT     NULL   /* PB12 is the Arm input; no ADC LED fitted. */
#define JD_LED1_ADC_PIN      GPIO_PIN_12   /* ADC sample alive            */
#define JD_LED2_MOVE_PORT    NULL          /* optional: any-direction LED; */
#define JD_LED2_MOVE_PIN     GPIO_PIN_13   /*  set PORT=GPIOB to enable it */
#define JD_LED3_TX_PORT      GPIOB
#define JD_LED3_TX_PIN       GPIO_PIN_14   /* UART TX frame accepted      */
#define JD_LED4_FAULT_PORT   GPIOB
#define JD_LED4_FAULT_PIN    GPIO_PIN_15   /* any fault (see fault mask)  */

/* Direction LEDs (4 more pins) */
#define JD_LED_LEFT_PORT     GPIOB
#define JD_LED_LEFT_PIN      GPIO_PIN_8    /* steering pushed left        */
#define JD_LED_RIGHT_PORT    GPIOB
#define JD_LED_RIGHT_PIN     GPIO_PIN_9    /* steering pushed right       */
#define JD_LED_FWD_PORT      GPIOB
#define JD_LED_FWD_PIN       GPIO_PIN_10   /* throttle pushed forward     */
#define JD_LED_BACK_PORT     GPIOB
#define JD_LED_BACK_PIN      GPIO_PIN_11   /* throttle pushed back        */

/* Which ADC axis is which, and which direction is "high" raw value.
 * Joystick mounting differs: if the WRONG LED lights, flip the polarity
 * macro (or swap the axis macro) - this is itself a wiring check. */
#define JD_STEER_AXIS_IS_X       1   /* 1: X = steering, Y = throttle; 0: swapped */
#define JD_STEER_HIGH_IS_RIGHT   1   /* 1: raw > centre means RIGHT; 0: means LEFT */
#define JD_THROTTLE_HIGH_IS_FWD  1   /* 1: raw > centre means FORWARD; 0: BACK     */

/* Heartbeat LED (the "one LED on Blackpill"). WeAct Blackpill = PC13, active low.
 * Set JD_HEARTBEAT_ENABLE to 0 if the maintainer already drives it elsewhere. */
#define JD_HEARTBEAT_ENABLE      1
#define JD_HEARTBEAT_PORT        GPIOC
#define JD_HEARTBEAT_PIN         GPIO_PIN_13
#define JD_HEARTBEAT_ACTIVE_LOW  1

/* ---------------------------------------------------------------------------
 * 2) TUNING
 * ------------------------------------------------------------------------- */
#define JD_ADC_CENTER            2048u   /* 12-bit mid-scale                  */
#define JD_DEADBAND              150u    /* +/- counts treated as neutral     */
#define JD_RAIL_LOW              20u     /* raw <= this  => "at low rail"     */
#define JD_RAIL_HIGH             4075u   /* raw >= this  => "at high rail"    */
#define JD_RAIL_FAULT_MS         2000u   /* rail held this long => fault      */

#define JD_PULSE_STRETCH_MS      60u     /* min LED on-time so eye can see it */
#define JD_ADC_STALE_MS          200u    /* no ADC sample for this => fault   */
#define JD_TX_STALE_MS           500u    /* joystick moved but no TX => fault */
#define JD_FAULT_HOLD_MS         1000u   /* keep FAULT LED on after it clears */
#define JD_POLL_MIN_MS           50u     /* brief: poll fault <= every 50 ms  */
#define JD_HEALTHY_TOGGLE_MS     500u
#define JD_FAULT_TOGGLE_MS       250u

/* ---- serial drift report (debug serial only, never the control UART) ---- */
#define JD_PRINT_ENABLE          1
#define JD_PRINT_PERIOD_MS       200u    /* 5 lines/s = brief's healthy-log limit */
#define JD_DRIFT_WARN            60u     /* |avg - 2048| above this => OFFS       */
#define JD_NOISE_WARN            40u     /* (max-min) in one window above => NOISY*/
#define JD_REST_CAPTURE_MS       3000u   /* auto-capture "rest" after this, if stick is near centre */
#define JD_RING_SIZE             512u    /* print ring buffer (bytes)             */
#define JD_UART_CHUNK            64u     /* bytes handed to the UART per attempt  */

/* Name of the DEBUG UART handle created by CubeMX (e.g. huart2). Leave it
 * undefined to use your own sink via JoyDiag_SetWriteFn(). MUST NOT be the
 * ESP32 control UART. Needs the USART global interrupt enabled in NVIC. */
/* #define JD_DEBUG_UART_HANDLE  huart2 */

/* Boot lamp test: lights LED1..4 + heartbeat one by one so you can prove
 * every LED/resistor/pin is wired right BEFORE trusting it. Blocks ~1 s once,
 * at boot only (not in any control path). Set to 0 to disable. */
#define JD_BOOT_LAMP_TEST        1

/* ---------------------------------------------------------------------------
 * 3) FAULT BITS (read with JoyDiag_FaultMask(); log them via Verification_Log)
 * ------------------------------------------------------------------------- */
#define JD_FAULT_ADC_READ_FAIL   (1u << 0)  /* HAL_ADC read returned error     */
#define JD_FAULT_ADC_STALE       (1u << 1)  /* no ADC samples arriving         */
#define JD_FAULT_AXIS_X_RAIL     (1u << 2)  /* X stuck at 0 or 4095            */
#define JD_FAULT_AXIS_Y_RAIL     (1u << 3)  /* Y stuck at 0 or 4095            */
#define JD_FAULT_TX_FAIL         (1u << 4)  /* UART TX callback rejected frame */
#define JD_FAULT_TX_STALE        (1u << 5)  /* joystick moved, nothing sent    */

/** Snapshot for serial evidence (counters only ever increase). */
typedef struct {
    uint32_t adc_ok_count;
    uint32_t adc_fail_count;
    uint32_t tx_ok_count;
    uint32_t tx_fail_count;
    uint16_t last_raw_x;
    uint16_t last_raw_y;
    uint32_t fault_mask;      /* live faults right now            */
    uint32_t fault_ever_mask; /* every fault seen since boot      */
} JoyDiagStats;

/* ---------------------------------------------------------------------------
 * 4) API
 * ------------------------------------------------------------------------- */

/** Configure the 4 debug pins + heartbeat. Call once after MX_GPIO_Init(). */
void JoyDiag_Init(void);

/** HOOK A - call after every joystick ADC conversion (ISR, DMA callback or task).
 *  read_ok = false when the HAL/ADC read failed (raw values then ignored). */
void JoyDiag_OnAdcSample(uint16_t raw_x, uint16_t raw_y, bool read_ok);

/** HOOK B - call right after the UART TX callback/HAL call, with its result.
 *  accepted = true if the frame was queued/started (HAL_OK), false otherwise. */
void JoyDiag_OnUartTx(bool accepted);

/** Call from the main loop or the diagnostics task. Cheap; internally limited
 *  to one evaluation per JD_POLL_MIN_MS. Never blocks. */
void JoyDiag_Poll(void);

/** Non-blocking serial sink. Return how many bytes you ACCEPTED (0..len).
 *  Must not wait. `data` is only valid during the call - copy it. */
typedef uint32_t (*JoyDiagWriteFn)(void *user, const uint8_t *data, uint32_t len);

/** Choose where report lines go. Call after JoyDiag_Init(); NULL = printing off.
 *  Not needed when JD_DEBUG_UART_HANDLE is defined (Init registers it). */
void JoyDiag_SetWriteFn(JoyDiagWriteFn fn, void *user);

/** Declare "stick is at rest NOW": the next report window is stored as the
 *  rest centre, later lines show drift against it (rdx/rdy). */
void JoyDiag_CaptureRest(void);

/** Live fault bits (JD_FAULT_*). */
uint32_t JoyDiag_FaultMask(void);

/** Copy counters/fault state for serial evidence. Returns false if out==NULL. */
bool JoyDiag_GetStats(JoyDiagStats *out);

#ifdef __cplusplus
}
#endif

#endif /* JOYSTICK_DIAG_H */
