# CUERT Formal Verification & Hardware Acceptance Report (GP Rev 10)

> **Document ID:** CUERT-VERIF-GP10-20261022  
> **Verification Owner:** Abdelwadoud (Validation & Formal Testing Lead)  
> **Date:** 22 October 2026 (Cairo Time) — *Demonstration Readiness & Hardware Acceptance Dossier*  
> **Audience:** Basel & Salah (Technical Leadership), Ali & Farah (Subsystem Maintainers), Eyad (CI Lead), Mostafa (Diagnostics)  
> **Candidate 3-SHA Record:**
> - **`Blackpill-Sensing-Node`:** `9629905` (PR #4 Rodaina UART + PR #5 Boody Joystick merged)
> - **`ESP32-Sensing-Node`:** `df3b2d4` (PR #4 Ahmed Gateway Codec merged by Ali)
> - **`Bluepill-Actuator-Node`:** `0369a06` (Farah official maintainer integration merge: 131 host tests)
> - **Specification Revision:** Revision 10 (13 October 2026)
> - **Wiring Revision:** Approved Demo Pinout Rev 2.0 (BTS7960 20V Clamp, EM882S Stepper Pull-Downs, Hardware E-Stop Active)

---

## 1. Executive Summary & Verification Policy

This document constitutes the formal verification report and test evidence matrix required under **Document 10 (`10_Abdelwadod_Verification_plan_automated_cases_and_formal_tests.pdf`)**.

### Strict Acceptance Ground Rules
1. **No Assumed PASS:** Missing required physical evidence is explicitly recorded as **`Not run`**, NEVER `PASS`.
2. **Scope Exclusions:** Only genuine architectural exclusions approved by leadership are recorded as **`Not applicable`** with a documented engineering reason.
3. **Independent Mathematical Derivations:** All CRC vectors, deadband thresholds, voltage clamps, and 64-bit rate slew accumulator behaviors were derived independently of developer source code.
4. **Safety-First Inverted Schedule:** Emergency stop (GP-V05) and power-on safety gates (GP-V01) MUST precede all high-power motor movement tests (GP-V03).
5. **Candidate Traceability:** Any modification to code, configuration (`GP_CONFIG.md`), or bench wiring invalidates previous runs and creates a new candidate requiring a full regression cycle.

---

## 2. Independent Mathematical Derivations & Test Vectors

### 2.1 UART CRC-16/CCITT-FALSE
- **Algorithm:** Polynomial `0x1021`, Initial Value `0xFFFF`, RefIn `False`, RefOut `False`, XorOut `0x0000`.
- **Payload Span:** Exactly bytes 2 through 11 (10 bytes total: Version 2, Kind 1, Length 7, Sequence u16, Throttle u16, Steering i16, Flags u8).
- **Golden Vectors:**
  - Normal (`seq=0x1234, th=500, st=-250, fl=0x05`): CRC16 = `0x3B19`.
  - Maximum (`seq=0xFFFF, th=1000, st=1000, fl=0x07`): CRC16 = `0x6C9F`.
  - Invalid Stop (`seq=0x0001, th=0, st=0, fl=0x00`): CRC16 = `0xABE9`.
  - Check string `"123456789"`: CRC16 = `0x29B1`.

### 2.2 CAN CRC-8/SMBUS
- **Algorithm:** Polynomial `0x07`, Initial Value `0x00`, RefIn `False`, RefOut `False`, XorOut `0x00`.
- **Payload Span:** Exactly CAN frame bytes 0 through 6 (Sequence u16, Throttle u16, Steering i16, Flags u8).
- **Golden Vector:**
  - Data: `[0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]` (Sequence 1, neutral, flags 0)
  - Calculated CRC-8: `0xDF`
  - Standard check string `"123456789"`: CRC-8 = `0xF4`.

### 2.3 64-Bit Fixed-Point Slew Rate Accumulator
- **Formula:**
  $$\Delta\text{demand} = \frac{\text{slew\_pm\_s} \times \Delta t_{\text{ms}} + \text{remainder}}{1000}$$
- **Fractional Test Case:** With $\text{slew} = 50\text{ pm/s}$ and $\Delta t = 10\text{ms}$:
  - Tick 1: $50 \times 10 = 500 \implies \Delta = 0\text{ pm}$, Remainder stored $= 500$.
  - Tick 2: $50 \times 10 + 500 = 1000 \implies \Delta = +1\text{ pm}$, Remainder reset $= 0$.
  - *Proof:* Output advances smoothly by exactly $1\text{ pm}$ every $20\text{ms}$ with zero truncation stall.

### 2.4 Throttle Voltage Safety Clamp
- **System Constraints:** BTS7960 powered from 6S LiPo battery ($V_{\text{nominal}} = 22.2\text{V}$, $V_{\text{max}} = 25.2\text{V}$). DC Motor rated strictly for $20.0\text{V}$.
- **Clamping Factor:**
  $$\text{Duty}_{\text{max}} = \frac{20.0\text{V}}{25.2\text{V}} \times 1000 = 793.65\text{ pm} \implies \text{max\_duty\_pm} \le 793\text{ pm}\; (79.3\%)$$

---

## 3. Master Formal Verification Matrix (GP-V01 to GP-V05)

| Test ID | Formal Requirement | Candidate 3-SHA | Setup & Revision | Input / Action | Numeric Expectation & Tolerance | Observation Point | Serial Log Fields | Independent Physical Evidence | Verdict |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **GP-V01a** | Safe Power-on Startup State | `BP:9629905`<br/>`ESP:df3b2d4`<br/>`BPill:0369a06` | Bench Rails: 3.3V, 5.0V, 20V DC. Logic analyzer connected. | Apply power with floating/neutral joystick. | Both BTS7960 IN/EN and EM882S PUL/DIR lines remain $0.0\text{V} \pm 0.05\text{V}$ (disabled). | Scope Ch1 (PWM), Ch2 (STEP), Ch3 (ENA). | `boot: outputs_disabled, state=DISARMED` | Oscilloscope trace showing 0V rail stability for $>500\text{ms}$. | **Not run** |
| **GP-V01b** | Flashed Candidate 3-SHA Integrity | `BP:9629905`<br/>`ESP:df3b2d4`<br/>`BPill:0369a06` | SWD ST-Link & ESP-IDF Serial Monitor. | Query boot banners via UART/SWD. | Exact match: `BP:9629905`, `ESP:df3b2d4`, `BPill:0369a06`. | Node UART0/1 Banners at 115200 baud. | `node=Blackpill, git=9629905; node=ESP32, git=df3b2d4; node=Bluepill, git=0369a06` | Boot terminal banner logs. | **Not run** |
| **GP-V02a** | Blackpill UART TX Protocol Fidelity | `BP:9629905` | Rigol DS1054Z UART decoder on PA9 (TX). | Full forward demand (`th=1000, st=0, fl=0x07`). | Exactly 14 bytes: `A5 5A 02 01 07 [seq] E8 03 00 00 07 [crc16]`. Framing error = 0. | Logic Analyzer Saleae Ch0/1 @ 24MHz. | `uart_tx: seq=%u, th=1000, st=0, fl=0x07, crc=0x%04X` | Protocol Analyzer export `UART_CAPTURE_THROTTLE_MAX.csv`. | **Not run** |
| **GP-V02b** | ESP32 Split-Stream Reassembly & CAN Forward | `BP:9629905`<br/>`ESP:df3b2d4` | ESP32 receiving UART on GPIO16, TWAI TX on GPIO5. | Inject fragmented UART chunks (3, 7, 4 bytes) with 2ms gap. | Assembles single frame, generates CAN ID `0x105`, DLC 8, CRC8 in byte 7 within $<5\text{ms}$. | Saleae CAN Decoder on CANH/CANL. | `esp_gw: chunk_rx, frame_assembled, can_tx_ok, seq=%u` | Logic analyzer decode screenshot. | **Not run** |
| **GP-V02c** | Bluepill CAN Decode & Command Mapping | `ESP:df3b2d4`<br/>`BPill:0369a06` | Bluepill CAN RX (PA11), GPIO toggle pin instrumented. | Receive CAN ID `0x105` frame with `seq=101, th=500, st=-200, fl=0x07`. | Command decoded successfully; GPIO toggle latency $<150\mu\text{s}$. | Scope Ch1 (CAN ACK), Ch2 (GPIO Toggle Pin). | `bp_dec: seq=101, th=500, st=-200, status=VALID` | Oscilloscope pulse-width capture $<150\mu\text{s}$. | **Not run** |
| **GP-V03a** | Joystick Calibration & Deadband Zone | `BP:9629905` | ADC input fed by precision DC voltage source (0..3.3V). | Sweep throttle voltage from $1.65\text{V}$ (center) up to $1.73\text{V}$ (within deadband). | Output `throttle_pm` remains strictly 0 until $|V - V_{\text{center}}| > 80.6\text{mV}$ (100 counts). | Blackpill UART packet stream. | `joy: raw=2100, center=2048, deadband=100 -> th=0` | Multimeter + UART packet capture `JOY_DEADBAND_VERIF.csv`. | **Not run** |
| **GP-V03b** | Throttle Slew Limiting & Voltage Protection | `BPill:0369a06` | BTS7960 powered from 25.2V pack driving 20V DC Motor. | Step demand instantly from 0 to 1000 pm (`slew_pm_s=500`). | Duty cycle ramps at $500 \pm 10\text{ pm/s}$ and clamps strictly at $793\text{ pm}$ ($79.3\%$). | Rigol Scope Ch1 measuring BTS7960 PWM pin. | `cmd_map: req=1000, ramp=%u, clamped_duty=793` | Scope persistence trace showing $79.3\%$ max duty. | **Not run** |
| **GP-V03c** | Steering Direction Reversal & Deadtime Delay | `BPill:0369a06` | EM882S STEP/DIR connected to logic analyzer. | Instantly flip steering joystick from $+800\text{ pm}$ to $-800\text{ pm}$. | Pulses stop immediately; DIR pin pauses for $t_{\text{deadtime}} \ge 102\text{ms}$ before sign change. | Logic Analyzer Ch0 (STEP), Ch1 (DIR), Ch2 (ENA). | `mot: dir_reversal_started, deadtime_ms=102, applied=0` | Logic Analyzer timing trace showing $\ge 102\text{ms}$ hold. | **Not run** |
| **GP-V04a** | Corrupted Frame & CRC Rejection | `BP:9629905`<br/>`ESP:df3b2d4` | Host Python injector into ESP32 UART. | Inject 14-byte UART packet with flipped CRC bit. | Packet dropped; consumed reported; 0 CAN frames transmitted; no memory leak. | ESP32 TWAI bus monitor. | `uart_rx: crc_mismatch, drop_count=%u, resync_to_SOF` | CAN bus silent; scope confirms 0 transmissions. | **Not run** |
| **GP-V04b** | Stale Data & Queue Expiry Drop | `ESP:df3b2d4` | ESP32 Gateway with `max_queue_ms=50`. | Buffer UART packet and delay forwarding by $65\text{ms}$ ($>50\text{ms}$). | `Gateway_Forward` returns `GP_INVALID`; stale command dropped; CAN bus silent. | ESP32 TWAI monitor. | `gw: drop_stale, queued_ms=65, limit=50` | Serial timestamp difference capture. | **Not run** |
| **GP-V04c** | Disconnect / Link Loss Disarm | `BP:9629905`<br/>`ESP:df3b2d4`<br/>`BPill:0369a06` | Full running pipeline at 200 pm throttle demand. | Physically disconnect UART RX cable between Blackpill and ESP32. | Within $100\text{ms}$ of lost packets, Bluepill enters Safe Stop; PWM duty drops to $0.0\%$. | Scope Ch1 (PWM duty) + stop timer. | `bp_watchdog: timeout_ms=100, emergency_disarm=TRUE` | Oscilloscope capture showing PWM shutdown $<100\text{ms}$. | **Not run** |
| **GP-V05a** | Hardware Emergency Stop Cutoff | Hardware Bench | System running at 500 pm throttle demand. | Depress physical mushroom E-Stop button. | High-power relay cuts BTS7960 $20\text{V}$ rail within $<15\text{ms}$; Bluepill GPIO detects low and de-asserts PWM in $<1\text{ms}$. | Dual-Trigger Scope: Ch1 (E-Stop Switch), Ch2 (20V Rail), Ch3 (PWM Pin). | `safety: ESTOP_ASSERTED, immediate_kill=TRUE` | Scope capture showing rail drop $<15\text{ms}$. | **Not run** |
| **GP-V05b** | Fresh-Neutral / Armed Edge Recovery | Hardware Bench | Post-E-Stop recovery state with joystick at 400 pm demand. | Release physical E-Stop button while joystick is still held forward. | System MUST NOT resume motion; remains halted until joystick returns to neutral AND fresh Arm button pressed. | Rigol Scope Ch1 (PWM). | `safety: lockout_active, require_fresh_neutral_and_arm` | Video evidence & scope capture showing lockout. | **Not run** |
| **GP-V05c** | Dual-Node Diagnostic LED Indication | `BP:9629905`<br/>`BPill:0369a06` | Blackpill (PC13) and Bluepill (PC13) on bench. | Observe LED patterns during Normal, E-Stop, and Disconnect states. | Heartbeat: 1 Hz blink. Disconnected: Fast 5 Hz blink. E-Stop / Error: Solid ON. | Visual inspection & photodiode capture. | `led_diag: mode=HEARTBEAT / ERROR` | Photodiode timing trace on oscilloscope. | **Not run** |

---

## 4. Hardware Verification Sign-Off & Verdict Package

- **Automated Host Unit Tests:** **100% Pass** (161 / 161 test cases passing natively across Blackpill [19], ESP32 [11], and Bluepill [131]).
- **Bench Safety Status:** **CLEARED FOR SUPERVISED LOW-POWER BENCH TESTING (Wheels Elevated).**
- **Hardware Integration Tests:** **Pending Bench Execution** (GP-V01 to GP-V05).
- **Vehicle Demonstration Authorization:** **GATED ON PHYSICAL BENCH VERIFICATION OF GP-V03 THROUGH GP-V05.**

```
Certified by:
Abdelwadoud — Validation & Formal Testing Lead, CUERT
Ali Yasser — Sensing Subsystem Maintainer
Farah Hesham — Actuator Subsystem Maintainer
Date: 22 October 2026, Cairo, Egypt
```
