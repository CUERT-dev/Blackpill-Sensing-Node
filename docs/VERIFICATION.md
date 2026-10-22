# Blackpill verification — 22 October 2026

Status: firmware builds; physical operation remains unverified.

## Completed checks

- Fixed eight application source/include paths in `cmake/stm32cubemx/CMakeLists.txt`: the project uses `Core/Src` and `Core/Inc`.
- Debug and Release configurations compile and link successfully. ELF, HEX and BIN outputs are in `build/Debug` and `build/Release`.
- Current Debug: 42,756 bytes flash, 22,944 bytes RAM. Release: 26,596 bytes flash, 22,944 bytes RAM.
- Builds used the installed Arm GCC 12.2.1. `docs/BUILDING.md` requests 14.2.Rel1, so these do not establish CI toolchain parity.
- Reviewed initialization, RTOS tasks, ADC acquisition, GPIO switches, UART interrupt wiring and packet encoding. USART1 interrupt is enabled and calls the HAL handler.
- Host UART and ADC tests execute successfully: 3/3 CTest suites pass. ADC tests cover separate triggers, independent channels, invalid cache after either rank fails, recovery, neutral-invalid output after a partial scan failure, and restoration of the original scan after successful or failed startup self-test measurements.
- Following the user's centered input measurements (3.3 V supply, PB0/PB1 at 1.567 V), firmware `adc-selftest-v2` adds independent PB0/PB1 and internal-reference readings before the RTOS starts. Hardware results from this self-test are pending.
- User-supplied serial logs confirm debug output and advancing UART TX completion counts, but show both analog values fixed at zero. No firmware was flashed by this session.

## Open findings

1. **ADC scan timing fixed in source; board validation pending.** The ADC and `.ioc` now select a two-rank discontinuous scan, one rank per software trigger. `App_AdcRead` reads each rank before triggering the next and invalidates the cached pair on failure. Host mocks verify this sequence; independent analog inputs and actual timing still need verification on hardware. Both axes at the same supply rail produce an invalid stop, with an explicit diagnostic fault; see `SERIAL_DEBUG.md` for the conservative endpoint policy.
2. **Calibration is provisional.** `Core/Src/app.c` uses 0/2048/4095 with a 100-count deadband. Measure both axes at minimum, rest and maximum before accepting neutral and endpoint behavior.
3. **Diagnostics do not confirm receiver acceptance.** USART2 now prints raw inputs, commands, TX starts/completions, packet bytes, faults and USART1 RX history. USART1 RX rearming is deferred until the complete HAL interrupt handler returns, preserving error codes. The onboard LED is steady on for healthy diagnostics and off for faults. These indicators do not establish ESP reception or CAN forwarding.

## Board acceptance procedure

Use the configured STM32F401xC board and confirm its external crystal matches the 25 MHz clock configuration. Keep motor outputs disabled during command validation.

| Signal | Configured connection |
| --- | --- |
| Throttle analog input | PB1 / ADC1 channel 9, rank 1 |
| Steering analog input | PB0 / ADC1 channel 8, rank 2 |
| Arm switch | PB12, active low with pull-up |
| Deadman switch | PB13, active low with pull-up |
| Command UART TX / RX | PA9 / PA10, USART1 |
| UART format | 115200 baud, 8 data bits, no parity, 1 stop bit |
| Board LED | PC13, active low |

1. Connect ST-Link for debugger access. For packet capture, connect PA9 to a 3.3 V USB UART adapter's RX and connect common ground. Inputs must remain within the board's analog supply range.
2. After resolving the ADC finding, flash the Debug image and observe acquisition and transmission diagnostics over at least 60 seconds. Expected rates are approximately 100 acquisitions and 50 transmit starts per second. Acquisition failures, send errors and lock failures should remain zero during normal operation. Investigate recurring busy drops.
3. Capture USART1 output: expect 14-byte frames beginning `A5 5A 02 01 07`, with valid CRC-16/CCITT-FALSE over bytes 2 through 11 and little-endian CRC in bytes 12 and 13. Verify CRC independently at the receiver. Sequence should advance modulo 65536; skips are expected because acquisition is faster than sending.
4. At rest, expect zero throttle and zero steering after measured calibration. Move each axis independently: throttle should cover 0 to 1000 (reverse travel remains zero); steering should cover -1000 to +1000. The other axis must remain stable.
5. Test all switch combinations: VALID=0x01, ARM=0x02 and DEADMAN=0x04. Pressing a switch connects its input to ground. Arm/deadman are transmitted requests; receiver-side enforcement must be checked separately.
6. Inject a controlled ADC read failure with the debugger or a test build: expect a neutral invalid frame (both demands and flags zero) with advancing sequence. Simply unplugging a joystick can leave an input floating and is not a reliable ADC-failure injection.
7. Repeat while exercising interrupts and after a power cycle. Confirm independent axes, no acquisition failures, valid packets and sufficient task stack space. Archive capture data and measured calibration before marking the board verified.
