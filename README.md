# ESP32-Sensing-Node

CUERT ESP32 firmware, using C and the native ESP-IDF CMake build system. The
application reads revision-10 command frames on UART1 and forwards validated
commands as standard CAN frames through the ESP32 TWAI controller.

## Hardware baseline

The reported board is an ESP32-S with 38 pins. This project assumes a classic
ESP32 (usually an ESP32-WROOM-32 or ESP-32S module), not an ESP32-S2/S3/C3.
Pin count alone does not identify the chip or flash capacity: check the metal
module's marking. The initial flash configuration uses a conservative 2 MB,
DIO, 40 MHz baseline. Change flash size in `idf.py menuconfig` after confirming
the module's actual capacity; 38-pin boards often have 4 MB.

UART1 uses GPIO17 (TX), GPIO16 (RX), and 115200 baud. TWAI uses GPIO19 (TX),
GPIO18 (RX), and 500 kbit/s. The ESP32 has no built-in CAN transceiver; connect
an external transceiver between the ESP32 and the CAN bus. Confirm pin wiring
and bus termination before connecting hardware.

## One-time Windows setup

1. Open this directory in VS Code.
2. Install the recommended Espressif ESP-IDF extension if missing.
3. Press Ctrl+Shift+P and run **ESP-IDF: Open ESP-IDF Installation Manager**.
4. Install a stable ESP-IDF version and its tools through the manager.
   Record the chosen version for the team so everyone uses the same release.
5. Run **ESP-IDF: Select Current ESP-IDF Version** and select that installation.
6. Run **ESP-IDF: Doctor Command** to verify the setup.
7. Open an ESP-IDF terminal using the extension's command palette.

ESP-IDF supplies the ESP32 cross-compiler, Python environment, CMake, Ninja,
and esptool. Having desktop CMake alone is insufficient. Use the activated
ESP-IDF environment for all commands below.

## Build

On this PC, the EIM offline installer selected MSYS2 Python and failed to finish.
The tools were retained and a native Windows Python 3.12 environment was repaired
offline at `C:\Espressif\tools\python\v6.1\venv-win`. To activate it in a normal
PowerShell terminal, run the following from this directory (the leading dot matters):

```powershell
. .\Activate-ESP-IDF.ps1
idf.py --version
idf.py build
```

The activation script accepts installation paths as parameters for other PCs.
The `.vscode/settings.json` ESP-IDF paths refer to this PC and must be adjusted
for other installations. The manual setup works independently of EIM's completion
screen or installation registry.

In an ESP-IDF terminal, from this project's root:

```powershell
idf.py set-target esp32
idf.py build
```

Set the target once at initialization. Running `set-target` again regenerates
configuration and clears the build directory. For later builds use only `build`.
`sdkconfig.defaults` seeds a new configuration; subsequent changes belong in
`idf.py menuconfig` and should be exported to defaults for the team when needed.

`idf.py` drives CMake and Ninja. To use CMake directly in the same environment:

```powershell
cmake -S . -B build -G Ninja -DIDF_TARGET=esp32
cmake --build build
```

Outputs:

- `build/esp32_sensing_node.elf`: linked program and debug symbols.
- `build/esp32_sensing_node.bin`: application firmware.
- `build/bootloader/bootloader.bin`: second-stage bootloader.
- `build/partition_table/partition-table.bin`: flash partition layout.
- `build/flasher_args.json`: generated flash file locations and offsets.

ESP32 normally flashes binary `.bin` images rather than Intel HEX `.hex`.
Use the generated flash information instead of guessing offsets.

## Flash and monitor

Connect the board over USB with a data cable. Find its COM port in Windows
Device Manager under Ports (COM & LPT), then replace COM5 below:

```powershell
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

Or build, flash, and monitor in one command:

```powershell
idf.py -p COM5 flash monitor
```

`flash` also builds any pending changes. It uses esptool to communicate with the
ESP32's built-in ROM download bootloader over USB-to-UART, and writes the
bootloader, partition table, and application at their generated offsets.
An ST-Link is not required for normal USB flashing.

Expected output includes `CUERT ESP32 sensing node started`. Exit the monitor
with Ctrl+]. VS Code also provides ESP-IDF commands
for building, selecting a port, flashing using UART, and monitoring.

If no COM port appears, check the cable and the driver for the board's actual
USB-to-UART chip (commonly CP210x or CH340). If flashing waits at `Connecting`,
hold BOOT, tap EN/RESET, and release BOOT once flashing begins. Close other
serial monitors before flashing. For an unreliable connection try
`idf.py -p COM5 -b 115200 flash`.

## GUI alternative

Espressif Flash Download Tools is the Windows GUI alternative to STM32 Utility.
Choose ESP32 and load the `.bin` files at the offsets produced by your build.
During development, the VS Code ESP-IDF extension or `idf.py flash` avoids
manually entering the file paths and offsets.

## References

- [Windows ESP-IDF installation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/windows-setup.html)
- [VS Code setup](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/installation.html)
- [CMake build system](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-guides/build-system.html)
- [Build and flash](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/start-project.html)
