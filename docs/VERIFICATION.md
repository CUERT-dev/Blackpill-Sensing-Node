# Blackpill verification — 22 October 2026

Status: firmware builds; physical operation remains unverified.

## Completed checks

- Fixed eight application source/include paths in `cmake/stm32cubemx/CMakeLists.txt`: the project uses `Core/Src` and `Core/Inc`.
- Debug and Release configurations compile and link successfully. ELF, HEX and BIN outputs are in `build/Debug` and `build/Release`.
- Debug: 30,856 bytes flash, 20,736 bytes RAM. Release: 17,644 bytes flash, 20,728 bytes RAM.
- Builds used the installed Arm GCC 12.2.1. `docs/BUILDING.md` requests 14.2.Rel1, so these do not establish CI toolchain parity.
- Reviewed initialization, RTOS tasks, ADC acquisition, GPIO switches, UART interrupt wiring and packet encoding. USART1 interrupt is enabled and calls the HAL handler.
- Host UART tests could not execute: Windows application control blocks the installed MinGW linker. This is an environment failure, not a passing or failing test result.
- No present STM32/ST-Link or USB serial adapter was found. Nothing was flashed.

## Open findings

1. **ADC scan timing needs resolution before sign-off.** `Core/Src/main.c` configures a two-rank scan without DMA or discontinuous mode. `AdcRead` starts both conversions together, then polls and reads the shared data register twice. An interrupt or scheduling delay can allow rank 2 to finish before rank 1 is read, losing the first result and potentially causing the second poll to time out. This is a code-derived risk, not an observed board failure. Use a two-element DMA transfer, or explicitly trigger one conversion at a time (with matching configuration), then validate axis independence under interrupt load.
2. **Calibration is provisional.** `Core/Src/app.c` uses 0/2048/4095 with a 100-count deadband. Measure both axes at minimum, rest and maximum before accepting neutral and endpoint behavior.
3. **Diagnostics do not confirm physical delivery.** `send_ok` counts successful transmit starts, not completed transmissions or receiver acceptance. There is no diagnostic output task currently calling `App_GetDiagnostics`, and the LED adapter is not called as a heartbeat. A dark LED alone does not prove a fault.

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
