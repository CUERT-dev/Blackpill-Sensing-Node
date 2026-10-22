# Blackpill serial debugging

The firmware provides readable diagnostic text on **USART2 TX / PA2**, at
**115200 baud, 8 data bits, no parity, 1 stop bit, no flow control**.
USART1 / PA9 continues carrying binary command packets to the ESP.

## Connect and run

1. Build: `cmake --preset Debug`, then `cmake --build --preset Debug`.
2. Flash `build/Debug/Blackpill-Sensing-Node.elf` or its HEX/BIN equivalent using your programmer. Building does not update the board.
3. Connect a **3.3 V USB-to-UART adapter** RX to Blackpill **PA2**, and adapter GND to Blackpill GND. Adapter TX is unnecessary for viewing logs. The Blackpill USB socket is not configured as a USB serial port in this firmware.
4. Open the adapter's COM port in a serial monitor at the settings above. Reset the Blackpill with the monitor open to see its boot banner.
5. Leave **PA9 -> ESP GPIO16**, and common ground, connected for normal command transmission. For optional incoming ESP bytes, connect **ESP GPIO17 -> PA10**.

The boot banner is emitted after UART initialization and before the LED test and scheduler startup. Reports begin once the default task is running, nominally every 500 ms. Debug UART writes have a 100 ms timeout and run only in task context. Control acquisition and transmission keep their existing periods; neither UART interrupt callbacks nor sensing tasks print text.

## Read the report

Example only; actual values and totals vary:

```text
BP t=2500 dt=500 ADC calls=100 (+50) fail=0 result=0 raw_thr=2048 raw_str=2048
CMD seq=100 thr=0 str=0 valid=1 arm=0 deadman=0 flags=0x01
TX started=50 done=50 (+25) busy=0 error=0 result=0 lock_fail=0
TX HEX (last started): A5 5A 02 01 07 ...
RX bytes=0 (+0) restart_fail=0 UART errors=0 last_error=0x00000000 tail: none
DEBUG write_fail=0
```

- `ADC calls`: acquisition attempts; normally about +50 per 500 ms. `fail` is cumulative. Raw values are cached ADC values; if acquisition failed, one or both may be from an earlier attempt. Check `result` and `valid` before treating them as a good measurement.
- `CMD`: latest acquired sequence, normalized throttle (0..1000), steering (-1000..1000), and flags. This is the latest acquisition, which may be newer than the last transmitted packet. Calibration remains provisional.
- `TX started`: complete frames accepted by the UART driver; `done`: USART1 transmission-complete callbacks. Normally `done` rises by about 25 per 500 ms. An in-flight frame can make `started` one ahead of `done`. These counters do not prove that the ESP received or accepted the packet.
- `TX HEX`: exact bytes of the last frame accepted for transmission, not the text `CMD` line. Normal prefix: `A5 5A 02 01 07`. This report samples activity twice per second; it does not print all 50 frames per second.
- `busy`, `error`, `lock_fail`: cumulative skipped transmissions, send errors, and command mutex failures. Investigate increasing values.
- `RX bytes`: bytes physically read by USART1 on PA10. `tail` shows up to the last 16 bytes in arrival order, in hex. This is passive observation; bytes are not interpreted as commands or acknowledgments. The current ESP software does not reply, so zero is normal even on a working link.
- `UART errors`: HAL UART error callbacks; `last_error` is the HAL bitmask (1 parity, 2 noise, 4 framing, 8 overrun, 16 DMA). Reception is rearmed if HAL ended it after an error.
- `DEBUG write_fail`: debug UART writes that failed or timed out.
- GP result numbers: 0 OK, 1 pending/busy, 2 bad argument, 3 invalid, 4 stale, 5 I/O error, 6 insufficient space.

## Locate the failure

| Observation | Next check |
| --- | --- |
| No boot banner after reset | Correct firmware flashed, adapter on PA2, correct COM port and settings, power and clock startup |
| Boot banner but no periodic reports | LED initialization/HAL tick, RTOS startup, allocation failure or fault handler |
| ADC calls rise but failures rise too | ADC acquisition path, analog wiring and calibration; stop packets should still transmit |
| TX starts once, then busy grows and done stays zero | USART1 interrupt path or UART state |
| TX done rises normally, ESP has no UART RX log | Probe PA9 and ESP GPIO16; confirm common ground and ESP receive loop startup |
| ESP prints UART RX but gateway rejects | Check CRC/version and valid, neutral, disarmed startup requirement |

For a physical local loopback test, disconnect the ESP signal connections and connect Blackpill **PA9 to PA10**. With no other transmitter attached, `RX bytes` should rise by approximately 350 per 500 ms (25 packets x 14 bytes), with protocol bytes in `tail`. Remove the loopback and restore ESP wiring afterward. This tests both Blackpill UART directions; it is not a receiver acknowledgment from the ESP.

Hardware reception, actual baud timing, loopback behavior and task stack headroom must still be verified on the board. Local builds alone do not establish those results.
