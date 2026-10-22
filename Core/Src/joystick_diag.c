/**
  ******************************************************************************
  * @file    joystick_diag.c
  * @brief   Blackpill joystick-path debug LEDs + serial drift report
  *
  * Threading model:
  *   - Hooks (ADC / TX) do: counters, ticks, one BSRR write, and a short
  *     PRIMASK-protected update of the statistics window. No HAL calls except
  *     HAL_GetTick() (plain read). They are ISR-safe and never print.
  *   - ALL printing (formatting, ring buffer, UART) happens in JoyDiag_Poll(),
  *     which must run in thread/task context (main loop or diagnostics task).
  ******************************************************************************
  */
#include "joystick_diag.h"
#include <string.h>

/* ------------------------------------------------------------------ helpers */
#define JD_ELAPSED(now, t, ms)   ((int32_t)((now) - (t)) >= (int32_t)(ms))
#define JD_ABS(v)                (((v) < 0) ? -(v) : (v))

/* LED_FAULT must stay last of the pulse LEDs: indices below it are pulse LEDs
 * expired in Poll; FAULT is driven by Poll only. */
enum { LED_ADC = 0, LED_MOVE, LED_TX, LED_LEFT, LED_RIGHT, LED_FWD, LED_BACK,
       LED_FAULT, LED_COUNT };

typedef struct { GPIO_TypeDef *port; uint16_t pin; } JdLed;

static const JdLed s_led[LED_COUNT] = {
    { JD_LED1_ADC_PORT,   JD_LED1_ADC_PIN   },
    { JD_LED2_MOVE_PORT,  JD_LED2_MOVE_PIN  },
    { JD_LED3_TX_PORT,    JD_LED3_TX_PIN    },
    { JD_LED_LEFT_PORT,   JD_LED_LEFT_PIN   },
    { JD_LED_RIGHT_PORT,  JD_LED_RIGHT_PIN  },
    { JD_LED_FWD_PORT,    JD_LED_FWD_PIN    },
    { JD_LED_BACK_PORT,   JD_LED_BACK_PIN   },
    { JD_LED4_FAULT_PORT, JD_LED4_FAULT_PIN },
};

/* Statistics window: filled by the ADC hook, drained by Poll. */
typedef struct {
    uint32_t sum_x, sum_y, n;
    uint16_t min_x, max_x, min_y, max_y;
} JdWin;

/* ------------------------------------------------------------------- state */
static volatile uint32_t s_adc_ok, s_adc_fail, s_last_adc_tick, s_moving_since;
static volatile uint32_t s_rail_x_since, s_rail_y_since;
static volatile uint16_t s_raw_x, s_raw_y;
static volatile uint8_t  s_moving_now, s_rail_x_on, s_rail_y_on;
static volatile uint32_t s_tx_ok, s_tx_fail, s_last_tx_tick;
static volatile uint32_t s_pulse_off_tick[LED_FAULT];
static JdWin s_win;                         /* guarded by PRIMASK              */
static volatile uint8_t s_rest_req;         /* set by JoyDiag_CaptureRest()    */

/* Poll-only state */
static uint32_t s_last_eval, s_hb_last, s_fault_led_until, s_boot_tick;
static uint32_t s_adc_fail_seen, s_tx_fail_seen, s_adc_fail_until, s_tx_fail_until;
static uint8_t  s_hb_state;
static volatile uint32_t s_fault_live, s_fault_ever;

static JoyDiagWriteFn s_write;
static void          *s_write_user;
static uint8_t  s_ring[JD_RING_SIZE];
static uint16_t s_head, s_tail;
static uint32_t s_print_drop, s_print_seq, s_last_print;
static bool     s_rest_ok;
static int32_t  s_rest_x, s_rest_y;

/* ------------------------------------------------------------- pin helpers */
static void clk_enable(const GPIO_TypeDef *p)
{
    if      (p == GPIOA) { __HAL_RCC_GPIOA_CLK_ENABLE(); }
    else if (p == GPIOB) { __HAL_RCC_GPIOB_CLK_ENABLE(); }
    else if (p == GPIOC) { __HAL_RCC_GPIOC_CLK_ENABLE(); }
    else if (p == GPIOD) { __HAL_RCC_GPIOD_CLK_ENABLE(); }
    else if (p == GPIOE) { __HAL_RCC_GPIOE_CLK_ENABLE(); }
    else if (p == GPIOH) { __HAL_RCC_GPIOH_CLK_ENABLE(); }
}

/* A LED whose PORT is NULL is "not fitted": every helper ignores it. */
static inline void led_set(uint8_t i)
{ if (s_led[i].port != NULL) { s_led[i].port->BSRR = s_led[i].pin; } }
static inline void led_clear(uint8_t i)
{ if (s_led[i].port != NULL) { s_led[i].port->BSRR = (uint32_t)s_led[i].pin << 16; } }
static inline bool led_is_on(uint8_t i)
{ return (s_led[i].port != NULL) && ((s_led[i].port->ODR & s_led[i].pin) != 0u); }

static inline void led_pulse(uint8_t i, uint32_t now)
{
    s_pulse_off_tick[i] = now + JD_PULSE_STRETCH_MS;  /* deadline first ...   */
    led_set(i);                                       /* ... then light it    */
}

#if JD_HEARTBEAT_ENABLE
static inline void hb_write(bool on)
{
#if JD_HEARTBEAT_ACTIVE_LOW
    on = !on;
#endif
    if (on) { JD_HEARTBEAT_PORT->BSRR = JD_HEARTBEAT_PIN; }
    else    { JD_HEARTBEAT_PORT->BSRR = (uint32_t)JD_HEARTBEAT_PIN << 16; }
}
#endif

/* +1 pushed to high raw side, -1 pushed to low raw side, 0 neutral */
static inline int8_t axis_dir(uint32_t raw)
{
    if (raw > JD_ADC_CENTER + JD_DEADBAND)  { return 1; }
    if (raw + JD_DEADBAND < JD_ADC_CENTER)  { return -1; }
    return 0;
}

/* ------------------------------------------------------- serial output path */
#ifdef JD_DEBUG_UART_HANDLE
extern UART_HandleTypeDef JD_DEBUG_UART_HANDLE;
static uint8_t s_uart_buf[JD_UART_CHUNK];

/* Non-blocking: only starts a transfer if the UART is idle; data is copied
 * so the ring buffer can be reused immediately. */
static uint32_t uart_sink(void *user, const uint8_t *data, uint32_t len)
{
    UART_HandleTypeDef *h = &JD_DEBUG_UART_HANDLE;
    (void)user;
    if (h->gState != HAL_UART_STATE_READY) { return 0u; }
    if (len > JD_UART_CHUNK) { len = JD_UART_CHUNK; }
    memcpy(s_uart_buf, data, len);
    return (HAL_UART_Transmit_IT(h, s_uart_buf, (uint16_t)len) == HAL_OK) ? len : 0u;
}
#endif

void JoyDiag_SetWriteFn(JoyDiagWriteFn fn, void *user)
{
    s_write = fn;
    s_write_user = user;
}

/* all-or-nothing: a line is never cut in half; if full, count a drop */
static void ring_put_line(const char *line, uint32_t len)
{
    uint32_t used = (s_head >= s_tail) ? (uint32_t)(s_head - s_tail)
                                       : (uint32_t)(JD_RING_SIZE - s_tail + s_head);
    uint32_t i;
    if (len > (JD_RING_SIZE - 1u - used)) { s_print_drop++; return; }
    for (i = 0; i < len; i++) {
        s_ring[s_head] = (uint8_t)line[i];
        s_head = (uint16_t)((s_head + 1u) % JD_RING_SIZE);
    }
}

static void ring_drain(void)
{
    uint32_t n, took;
    if (s_write == NULL || s_head == s_tail) { return; }
    n = (s_head > s_tail) ? (uint32_t)(s_head - s_tail)
                          : (uint32_t)(JD_RING_SIZE - s_tail);   /* contiguous */
    took = s_write(s_write_user, &s_ring[s_tail], n);
    if (took > n) { took = n; }
    s_tail = (uint16_t)((s_tail + took) % JD_RING_SIZE);
}

/* tiny integer formatters (no printf: small, no heap, no reentrancy issues) */
static char *p_str(char *p, const char *s) { while (*s) { *p++ = *s++; } return p; }
static char *p_u(char *p, uint32_t v)
{
    char t[10]; uint8_t n = 0;
    do { t[n++] = (char)('0' + (v % 10u)); v /= 10u; } while (v != 0u);
    while (n) { *p++ = t[--n]; }
    return p;
}
static char *p_i(char *p, int32_t v)    /* always shows sign: +12 / -3 / +0 */
{
    if (v < 0) { *p++ = '-'; v = -v; } else { *p++ = '+'; }
    return p_u(p, (uint32_t)v);
}
static char *p_hex8(char *p, uint32_t v)
{
    static const char d[] = "0123456789ABCDEF";
    *p++ = '0'; *p++ = 'x'; *p++ = d[(v >> 4) & 0xFu]; *p++ = d[v & 0xFu];
    return p;
}

static void print_banner(void)
{
    char b[200]; char *p = b;
    p = p_str(p, "JD BOOT center=");   p = p_u(p, JD_ADC_CENTER);
    p = p_str(p, " deadband=");        p = p_u(p, JD_DEADBAND);
    p = p_str(p, " warn=");            p = p_u(p, JD_DRIFT_WARN);
    p = p_str(p, " noise=");           p = p_u(p, JD_NOISE_WARN);
    p = p_str(p, " period_ms=");       p = p_u(p, JD_PRINT_PERIOD_MS);
    p = p_str(p, " steer_axis=");      p = p_str(p, JD_STEER_AXIS_IS_X ? "X" : "Y");
    p = p_str(p, "\r\nJD KEY rx,ry=latest raw; ax,ay=window avg; sx,sy=noise(max-min); "
                 "dx,dy=avg-center; rdx,rdy=avg-rest; S=L/R/N T=F/B/N\r\n");
    ring_put_line(b, (uint32_t)(p - b));
}

/* One report line from the finished window. Called from Poll only. */
static void print_report(uint32_t now, const JdWin *w, uint32_t fault)
{
    char b[192]; char *p = b;
    const char *st = "OK";
    char s_ch = 'N', t_ch = 'N';

    p = p_str(p, "JD seq=");  p = p_u(p, s_print_seq++);
    p = p_str(p, " t=");      p = p_u(p, now);

    if (w->n == 0u) {                                   /* ADC not delivering */
        p = p_str(p, " NOADC");
    } else {
        int32_t ax = (int32_t)((w->sum_x + w->n / 2u) / w->n);
        int32_t ay = (int32_t)((w->sum_y + w->n / 2u) / w->n);
        int32_t dx = ax - (int32_t)JD_ADC_CENTER;
        int32_t dy = ay - (int32_t)JD_ADC_CENTER;
        int32_t spx = (int32_t)w->max_x - (int32_t)w->min_x;
        int32_t spy = (int32_t)w->max_y - (int32_t)w->min_y;
        bool out   = (JD_ABS(dx) > (int32_t)JD_DEADBAND) || (JD_ABS(dy) > (int32_t)JD_DEADBAND);
        bool offs  = (JD_ABS(dx) > (int32_t)JD_DRIFT_WARN) || (JD_ABS(dy) > (int32_t)JD_DRIFT_WARN);
        bool noisy = (spx > (int32_t)JD_NOISE_WARN) || (spy > (int32_t)JD_NOISE_WARN);
        bool creep = false;
        int8_t sd, td;

        /* rest capture: forced by API, or auto once after boot if stick is
         * near centre and quiet (so a pushed stick is never taken as rest) */
        if (s_rest_req ||
            (!s_rest_ok && JD_ELAPSED(now, s_boot_tick, JD_REST_CAPTURE_MS) &&
             JD_ABS(dx) <= (int32_t)(2u * JD_DEADBAND) &&
             JD_ABS(dy) <= (int32_t)(2u * JD_DEADBAND) &&
             spx <= (int32_t)(2u * JD_NOISE_WARN) && spy <= (int32_t)(2u * JD_NOISE_WARN))) {
            s_rest_x = ax; s_rest_y = ay; s_rest_ok = true; s_rest_req = 0u;
        }
        if (s_rest_ok) {
            creep = (JD_ABS(ax - s_rest_x) > (int32_t)JD_DRIFT_WARN) ||
                    (JD_ABS(ay - s_rest_y) > (int32_t)JD_DRIFT_WARN);
        }

        /* verdict, worst first */
        if      (out)   { st = "OUT"; }    /* outside deadband: pushed OR centre drifted */
        else if (creep) { st = "CREEP"; }  /* moved away from captured rest              */
        else if (offs)  { st = "OFFS"; }   /* rest offset from 2048, still in deadband   */
        else if (noisy) { st = "NOISY"; }  /* jitter: loose wiper / supply noise         */

#if JD_STEER_AXIS_IS_X
        sd = axis_dir((uint32_t)ax); td = axis_dir((uint32_t)ay);
#else
        sd = axis_dir((uint32_t)ay); td = axis_dir((uint32_t)ax);
#endif
        if (sd != 0) { s_ch = ((sd > 0) == (JD_STEER_HIGH_IS_RIGHT != 0)) ? 'R' : 'L'; }
        if (td != 0) { t_ch = ((td > 0) == (JD_THROTTLE_HIGH_IS_FWD != 0)) ? 'F' : 'B'; }

        p = p_str(p, " rx="); p = p_u(p, s_raw_x);
        p = p_str(p, " ry="); p = p_u(p, s_raw_y);
        p = p_str(p, " ax="); p = p_u(p, (uint32_t)ax);
        p = p_str(p, " ay="); p = p_u(p, (uint32_t)ay);
        p = p_str(p, " sx="); p = p_u(p, (uint32_t)spx);
        p = p_str(p, " sy="); p = p_u(p, (uint32_t)spy);
        p = p_str(p, " dx="); p = p_i(p, dx);
        p = p_str(p, " dy="); p = p_i(p, dy);
        if (s_rest_ok) {
            p = p_str(p, " rdx="); p = p_i(p, ax - s_rest_x);
            p = p_str(p, " rdy="); p = p_i(p, ay - s_rest_y);
        } else {
            p = p_str(p, " rdx=- rdy=-");
        }
        p = p_str(p, " S="); *p++ = s_ch;
        p = p_str(p, " T="); *p++ = t_ch;
        p = p_str(p, " st="); p = p_str(p, st);
    }
    p = p_str(p, " drop="); p = p_u(p, s_print_drop);
    p = p_str(p, " flt=");  p = p_hex8(p, fault);
    p = p_str(p, "\r\n");
    ring_put_line(b, (uint32_t)(p - b));
}

/* -------------------------------------------------------------------- init */
void JoyDiag_Init(void)
{
    GPIO_InitTypeDef g = {0};
    uint8_t i;

    for (i = 0; i < LED_COUNT; i++) {
        if (s_led[i].port == NULL) { continue; }
        clk_enable(s_led[i].port);
        led_clear(i);                              /* defined level first */
        g.Pin   = s_led[i].pin;
        g.Mode  = GPIO_MODE_OUTPUT_PP;
        g.Pull  = GPIO_NOPULL;
        g.Speed = GPIO_SPEED_FREQ_LOW;
        HAL_GPIO_Init(s_led[i].port, &g);
    }
#if JD_HEARTBEAT_ENABLE
    clk_enable(JD_HEARTBEAT_PORT);
    hb_write(false);
    g.Pin   = JD_HEARTBEAT_PIN;
    g.Mode  = GPIO_MODE_OUTPUT_PP;
    g.Pull  = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_LOW;
    HAL_GPIO_Init(JD_HEARTBEAT_PORT, &g);
    hb_write(false);
#endif

#if JD_BOOT_LAMP_TEST
    /* Walk every fitted LED then heartbeat: proves wiring/polarity once. */
    for (i = 0; i < LED_COUNT; i++) {
        if (s_led[i].port == NULL) { continue; }
        led_set(i);  HAL_Delay(150);  led_clear(i);
    }
#if JD_HEARTBEAT_ENABLE
    hb_write(true);  HAL_Delay(150);  hb_write(false);
#endif
#endif

    {
        uint32_t now = HAL_GetTick();
        s_last_adc_tick   = now;
        s_last_tx_tick    = now;
        s_moving_since    = now;
        s_last_eval       = now - JD_POLL_MIN_MS;   /* first Poll evaluates */
        s_hb_last         = now;
        s_fault_led_until = now;
        s_adc_fail_until  = now;
        s_tx_fail_until   = now;
        s_boot_tick       = now;
        s_last_print      = now;
    }

#if JD_PRINT_ENABLE && defined(JD_DEBUG_UART_HANDLE)
    JoyDiag_SetWriteFn(uart_sink, NULL);
#endif
#if JD_PRINT_ENABLE
    if (s_write != NULL) { print_banner(); }
#endif
}

void JoyDiag_CaptureRest(void) { s_rest_req = 1u; }

/* ------------------------------------------------------------------- hooks */
void JoyDiag_OnAdcSample(uint16_t raw_x, uint16_t raw_y, bool read_ok)
{
    uint32_t now = HAL_GetTick();
    int8_t dx, dy, sd, td;

    if (!read_ok) {                 /* failed read is NOT proof of life */
        s_adc_fail++;
        return;
    }
    s_adc_ok++;
    s_raw_x = raw_x;
    s_raw_y = raw_y;
    s_last_adc_tick = now;
    led_pulse(LED_ADC, now);

    /* statistics window for the serial report (short critical section) */
    {
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        if (s_win.n == 0u) {
            s_win.sum_x = 0u; s_win.sum_y = 0u;
            s_win.min_x = s_win.max_x = raw_x;
            s_win.min_y = s_win.max_y = raw_y;
        } else {
            if (raw_x < s_win.min_x) { s_win.min_x = raw_x; }
            if (raw_x > s_win.max_x) { s_win.max_x = raw_x; }
            if (raw_y < s_win.min_y) { s_win.min_y = raw_y; }
            if (raw_y > s_win.max_y) { s_win.max_y = raw_y; }
        }
        s_win.sum_x += raw_x;
        s_win.sum_y += raw_y;
        s_win.n++;
        __set_PRIMASK(pm);
    }

    dx = axis_dir(raw_x);
    dy = axis_dir(raw_y);

    /* neutral / moving */
    if (dx != 0 || dy != 0) {
        if (!s_moving_now) { s_moving_since = now; }
        s_moving_now = 1u;
        led_pulse(LED_MOVE, now);
    } else {
        s_moving_now = 0u;
    }

    /* direction LEDs: light the side the stick is pushed to */
#if JD_STEER_AXIS_IS_X
    sd = dx; td = dy;
#else
    sd = dy; td = dx;
#endif
    if (sd != 0) { led_pulse(((sd > 0) == (JD_STEER_HIGH_IS_RIGHT != 0)) ? LED_RIGHT : LED_LEFT, now); }
    if (td != 0) { led_pulse(((td > 0) == (JD_THROTTLE_HIGH_IS_FWD != 0)) ? LED_FWD : LED_BACK, now); }

    /* axis pinned at a rail (open wire / lost 3.3 V or GND / bad pot) */
    if (raw_x <= JD_RAIL_LOW || raw_x >= JD_RAIL_HIGH) {
        if (!s_rail_x_on) { s_rail_x_since = now; s_rail_x_on = 1u; }
    } else { s_rail_x_on = 0u; }

    if (raw_y <= JD_RAIL_LOW || raw_y >= JD_RAIL_HIGH) {
        if (!s_rail_y_on) { s_rail_y_since = now; s_rail_y_on = 1u; }
    } else { s_rail_y_on = 0u; }
}

void JoyDiag_OnUartTx(bool accepted)
{
    uint32_t now = HAL_GetTick();

    if (accepted) {
        s_tx_ok++;
        s_last_tx_tick = now;
        led_pulse(LED_TX, now);
    } else {
        s_tx_fail++;
    }
}

/* -------------------------------------------------------------------- poll */
void JoyDiag_Poll(void)
{
    uint32_t now = HAL_GetTick();
    uint32_t mask = 0u;
    uint8_t  i;

#if JD_PRINT_ENABLE
    ring_drain();                       /* cheap; feeds the UART when idle */
#endif

    /* 1) expire stretched stage pulses (cheap, runs on every call) */
    for (i = 0; i < LED_FAULT; i++) {
        if (led_is_on(i) && JD_ELAPSED(now, s_pulse_off_tick[i], 0u)) {
            led_clear(i);
        }
    }

    /* 2) fault evaluation, at most once per JD_POLL_MIN_MS */
    if (!JD_ELAPSED(now, s_last_eval, JD_POLL_MIN_MS)) { return; }
    s_last_eval = now;

    if (s_adc_fail != s_adc_fail_seen) {
        s_adc_fail_seen = s_adc_fail;
        s_adc_fail_until = now + JD_FAULT_HOLD_MS;
    }
    if (s_tx_fail != s_tx_fail_seen) {
        s_tx_fail_seen = s_tx_fail;
        s_tx_fail_until = now + JD_FAULT_HOLD_MS;
    }

    if ((int32_t)(now - s_adc_fail_until) < 0) { mask |= JD_FAULT_ADC_READ_FAIL; }
    if ((int32_t)(now - s_tx_fail_until)  < 0) { mask |= JD_FAULT_TX_FAIL; }

    if (JD_ELAPSED(now, s_last_adc_tick, JD_ADC_STALE_MS)) {
        mask |= JD_FAULT_ADC_STALE;
    }
    if (s_rail_x_on && JD_ELAPSED(now, s_rail_x_since, JD_RAIL_FAULT_MS)) {
        mask |= JD_FAULT_AXIS_X_RAIL;
    }
    if (s_rail_y_on && JD_ELAPSED(now, s_rail_y_since, JD_RAIL_FAULT_MS)) {
        mask |= JD_FAULT_AXIS_Y_RAIL;
    }
    /* joystick off neutral but no accepted TX since it moved (skip if ADC is
     * already stale, otherwise the old "moving" flag would blame the UART). */
    if (s_moving_now && !(mask & JD_FAULT_ADC_STALE)) {
        uint32_t ref = ((int32_t)(s_last_tx_tick - s_moving_since) > 0)
                       ? s_last_tx_tick : s_moving_since;
        if (JD_ELAPSED(now, ref, JD_TX_STALE_MS)) { mask |= JD_FAULT_TX_STALE; }
    }

    s_fault_live  = mask;
    s_fault_ever |= mask;

    /* FAULT LED: on while faulty, stays on JD_FAULT_HOLD_MS after it clears */
    if (mask != 0u) {
        s_fault_led_until = now + JD_FAULT_HOLD_MS;
        led_set(LED_FAULT);
    } else if (JD_ELAPSED(now, s_fault_led_until, 0u)) {
        led_clear(LED_FAULT);
    }

#if JD_HEARTBEAT_ENABLE
    /* healthy 500 ms toggle, fault/stale 250 ms toggle */
    if (JD_ELAPSED(now, s_hb_last, (mask != 0u) ? JD_FAULT_TOGGLE_MS
                                                 : JD_HEALTHY_TOGGLE_MS)) {
        s_hb_last  = now;
        s_hb_state = (uint8_t)!s_hb_state;
        hb_write(s_hb_state != 0u);
    }
#endif

#if JD_PRINT_ENABLE
    /* 3) serial drift report: one line per JD_PRINT_PERIOD_MS (<= 5 lines/s) */
    if (s_write != NULL && JD_ELAPSED(now, s_last_print, JD_PRINT_PERIOD_MS)) {
        JdWin w;
        uint32_t pm = __get_PRIMASK();
        __disable_irq();
        w = s_win;                      /* snapshot ... */
        s_win.n = 0u;                   /* ... and start a new window */
        __set_PRIMASK(pm);
        s_last_print = now;
        print_report(now, &w, mask);
    }
#endif
}

/* ---------------------------------------------------------------- readback */
uint32_t JoyDiag_FaultMask(void) { return s_fault_live; }

bool JoyDiag_GetStats(JoyDiagStats *out)
{
    if (out == NULL) { return false; }
    out->adc_ok_count    = s_adc_ok;
    out->adc_fail_count  = s_adc_fail;
    out->tx_ok_count     = s_tx_ok;
    out->tx_fail_count   = s_tx_fail;
    out->last_raw_x      = s_raw_x;
    out->last_raw_y      = s_raw_y;
    out->fault_mask      = s_fault_live;
    out->fault_ever_mask = s_fault_ever;
    return true;
}
