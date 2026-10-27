# ESP32 to Bluepill CAN link — can-link-v1

Software builds and host interoperability tests pass. Physical delivery is not
verified until the flashed Bluepill reports rising receive/decode counters.

## What was corrected

- Bluepill `Debug`/`Release` deliberately disable CAN. Build/flash **CanBench**
  for this link test: real 500 kbit/s CAN, motor waveforms inhibited. Do not flash
  `build/Debug/Bluepill-Actuator-Node.hex` and expect a CAN receiver.
- ESP transmission now checks `on_tx_done.is_tx_success`. An empty transmit queue
  or successful submission alone never increments `tx_ok`.
- Removed the 50 ms LED delay from the CAN transmit task. It was limiting service
  to at most 20 commands/s while UART sends about 50/s.
- TWAI APIs take **milliseconds**, not FreeRTOS ticks. The task now uses an
  explicitly bounded notification wait for the actual transmit result.
- The driver retains pointers to the frame and its data. A completion timeout
  now disables/deletes the node before those buffers are reused; the previous
  implementation could reuse a still-pending stack frame/data buffer.
- Only one transfer is in flight. Single-shot failures are counted rather than
  indefinitely retransmitting a stale control command. Queued frames older than
  100 ms are discarded, including after link outages.
- ESP reports ACKed successes, failures, error flags, TEC/REC and UART/gateway
  counters every second. CAN startup errors do not hide the UART receive loop.
- Bluepill reports CAN readiness, received frames, decoder results, last decoded
  command, age, ESR/BTR and motor readiness every second without a typed command.
- Corrected the ESP README's outdated GPIO21/22 CAN mapping to GPIO19/18.
- Removed an ESP-IDF component registration accidentally placed inside the
  standalone host-test CMake project.

No protocol arming rules, CRC checks, actuator limits or Bluepill fault-recovery
requirements were bypassed. No synthetic valid joystick commands are generated.

## Configured wiring and protocol

| Node | MCU TX to its transceiver | MCU RX from its transceiver |
| --- | --- | --- |
| ESP32 | GPIO19 | GPIO18 |
| Bluepill | PB9 | PB8 (CAN remap) |

Connect the transceivers' **CANH to CANH**, **CANL to CANL**, and their common
ground. MCU TX/RX pins connect to their own transceivers, not directly across
the two boards. Both use normal, acknowledgment-required Classic CAN at
**500000 bit/s**, standard data ID **0x105**, DLC **8**, little-endian fields
and CRC-8 polynomial 0x07. Neither loopback nor no-ACK self-test mode is enabled.

Bluepill CAN clock is 36 MHz: prescaler 4, BS1 15, BS2 2, SJW 1 gives
36 MHz / (4 x (1 + 15 + 2)) = 500 kbit/s.

## Build and flash

From the **Bluepill-Actuator-Node** folder:

```powershell
cmake --preset CanBench
cmake --build --preset CanBench
```

Flash `build/CanBench/Bluepill-Actuator-Node.hex` using the normal Bluepill
programmer. This is a CAN reception test image; motor outputs remain inhibited.

From **ESP32-Sensing-Node**, use the ESP-IDF terminal or activate this PC's tools:

```powershell
. .\Activate-ESP-IDF.ps1
idf.py build
idf.py -p COMx flash monitor
```

Replace `COMx` with the ESP's actual port. `idf.py flash` uses the generated
bootloader, partition and application offsets; the application BIN alone does
not belong at flash address zero.

## Prove reception

1. Open the Bluepill FTDI monitor at **115200, 8N1, no flow control**.
   Bluepill **PA9 -> FTDI RX**, GND -> GND. FTDI TX -> PA10 is optional for
   `status` and `recover` commands. Use the Bluepill FTDI connector, not the
   Blackpill PA2 connector. Reset Bluepill first.
2. Confirm `BLUE FW can-link-v1` and `configured=1 ready=1 bitrate=500000`.
   A `CAN DISABLED` banner means the wrong Bluepill build was flashed.
3. Reset ESP and confirm `FW can-link-v1` and `CAN ONLINE` with GPIO19/18.
4. Leave Blackpill joystick neutral with ARM released. The existing gateway
   requires an actually valid neutral/disarmed sample before forwarding; it
   cannot manufacture neutral from failed joystick input.
5. Confirm **ESP `tx_ok` rises** and **Bluepill `rx` and `decode` rise**.
   Bluepill `seq`, `thr`, `str` and `flags` show the received command. Sequence
   skips are normal because acquisition runs faster than transmission.
6. Save both consoles over at least 60 seconds. Normal traffic should approach
   50 frames/s, with no growing queue drops, TX failures or CRC rejection.

Examples of log shapes (numbers illustrative):

```text
CAN LINK tx_ok=100 tx_fail=0 ... last_seq=200 state=0 TEC=0 REC=0 ...
BLUE CAN ready=1 configured=1 bitrate=500000 rx=100 decode=100 reject=0 drop=0 seq=200 thr=0 str=0 flags=0x01 ... motor=0
```

`motor=0` is expected with CanBench. It does not mean the CAN receiver failed.
`tx_ok` proves acknowledgment by at least one other node, not specifically
application acceptance on Bluepill; its rising `decode` is the receiver evidence.

## Interpret failures

| Observation | Meaning / next check |
| --- | --- |
| ESP UART byte/frame counters stay zero | Blackpill-to-ESP UART problem, before CAN |
| UART frames rise, gateway rejects, `tx_ok=0` | Inspect validity, neutral/disarmed baseline, sequence and sample age |
| Bluepill `configured=0` | Flash CanBench, not Debug/Release |
| Bluepill `configured=1 ready=0` | Inspect its manual `status` report: block, panic, BTR/ESR and startup configuration |
| ESP `tx_fail` and `last_error=0x10` grow | No CAN acknowledgment; check Bluepill startup, transceiver power/enable, CANH/CANL, ground, bitrate and termination |
| ESP state=3 | Bus-off; transport is recreated without reusing an in-flight buffer |
| Bluepill `rx` rises but `decode` does not | Inspect CRC/range rejection and fault state using `status` |
| Both frame/decode counters rise but motors stay off | Link works; evaluate actuator profile and arming separately |

ESP error bits: 0x01 arbitration lost, 0x02 bit error, 0x04 form, 0x08 stuff,
0x10 ACK. `last_error` is historical, not cleared by a later success. State
values: 0 active, 1 warning, 2 passive, 3 bus-off. Error counters are cumulative
in software; `bus_err` is for the current recreated controller instance.

Bluepill latched faults are deliberately not cleared automatically. After
correcting the physical problem, send `recover` plus newline on its FTDI and
inspect `status`; fresh neutral/disarmed data and the existing arm edge are
still required. CAN acknowledgment does not require application arming.

For hardware checking, with the bus powered off, two 120-ohm end terminators
normally measure about 60 ohms between CANH and CANL. Check transceiver logic
levels against the actual board interface. Software cannot correct missing
termination, a disabled transceiver or disconnected CAN wires.

## Validation performed here

- Bluepill CanBench and disabled-CAN Debug builds and linked-image/profile checks.
- Bluepill 131/131 host tests, including decoder, control and fault handling.
- ESP UART/gateway host test and a new cross-repository test that compiles the
  actual Bluepill codec: UART chunk boundaries, limits, signed steering, sequence
  rollover, CRC corruption and invalid-stop frames pass (2/2 suites).
- ESP native ESP-IDF firmware build. Firmware is prepared locally, not flashed
  by this session. Physical acknowledgment, bus waveforms and Bluepill counters
  still require the board procedure above.
