# Blackpill-Sensing-Node

CUERT Learning Phase GP, revised 13 October 2026.

Ali maintains this repository. Boody implements two-axis ADC joystick calibration;
Rodaina implements the Blackpill UART sender, using Ahmed's shared command codec.
Her API is `UartSender_Init(sender, write, user)` and
`UartSender_Send(sender, command)`. Ali supplies the nonblocking UART callback,
owns acquisition sequences and invokes the libraries from his application tasks.
The application sends joystick commands over UART to ESP32, which forwards them
over CAN to Bluepill. Motion uses an open-loop command path.

Throttle uses a BTS7960 driver and a DC Motor operating at 20 V. Steering uses
the 86CM85 stepper and EM882S driver. Neutral joystick steering stops pulses;
it does not command an absolute angle or automatic centering.

Use command-only protocol version 2 and the revised contracts in
`../EmbeddedSystemsDocs/Learning Phase/Tasks/GP/CUERT_Learning_Phase_GP.docx`.
API revision 10 declarations are in the GP `Contracts/gp_types.h` reference.
There are no motor sensor inputs, PID modules or application return packets in
the GP scope. Operator arm/deadman, physical E stop and local debug logs remain.

The checked-in firmware is a starter scaffold. Ali owns ADC/UART initialization,
RTOS tasks, adapters and harnesses. Developers start from Testing and submit
module PRs to Maintaining; do not claim hardware acceptance from compilation.
