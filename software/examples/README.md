# Project Overview
This project provides example firmware for interacting with FM24W256 FRAM (or compatible I2C EEPROM/FRAM) memory modules. It includes two main examples:

*   **Arduino (C++)**: For Arduino-compatible boards.
*   **MicroPython**: For boards running MicroPython (e.g., Raspberry Pi Pico, ESP32).

Both examples offer a serial/command-line interface to scan for I2C devices, detect memory capacity, read/write/erase memory, and check write protection.

## Arduino Example (`fm24w256/fm24w256.ino`)
### Features
*   **I2C Pin Configuration**: Set custom SDA/SCL pins.
*   **Device Scanning**: Finds I2C devices and selects likely EEPROM/FRAM chips.
*   **Capacity Detection**: Automatically detects memory size and page size.
*   **Read/Write/Erase**: Read, write, and erase memory with page handling.
*   **Write Protection Check**: Tests if the memory is write-protected.
*   **Command Parser**: Serial interface for commands like `scan`, `read`, `write`, `erase`, etc.

### Key Functions
*   `eepromWaitReady()`: Waits for the device to be ready after a write.
*   `eepromReadSeq()`, `eepromWritePaged()`: Read/write memory, handling page boundaries.
*   `eepromDetectSize()`: Detects memory size and addressing mode.
*   `cmdScan()`, `cmdRead()`, `cmdWrite()`, `cmdErase()`, `cmdWPCheck()`: Command handlers.
*   `handleCommand()`: Parses and executes user commands from the serial monitor.

## MicroPython Example (`mp/main.py`)
### Features
*   **I2C Pin Configuration**: Set SDA/SCL pins for your board.
*   **Device Scanning**: Scans the I2C bus for devices, selects FRAM.
*   **Read/Write/Erase**: Read, write, and erase memory.
*   **Write Protection Check**: Verifies if the memory is write-protected.
*   **Command Parser**: Command-line interface for similar commands as the Arduino example.

### Key Functions
*   `fram_write()`, `fram_read()`: Write/read memory.
*   `cmd_scan()`, `cmd_read()`, `cmd_write()`, `cmd_erase()`, `cmd_wpcheck()`: Command handlers.
*   `handle_command()`: Parses and executes user commands.
*   `main_loop()`: Runs the interactive command-line interface.

## Usage
1.  Connect your FM24W256 (or compatible) chip to your board’s I2C pins.
2.  Upload the appropriate firmware (Arduino or MicroPython).
3.  Open the serial monitor or terminal.
4.  Use commands like `scan`, `capacity`, `read <addr> <len>`, `write <addr> <text>`, `erase`, and `wpcheck` to interact with the memory.

## Summary
The firmware provides a robust, interactive way to test, use, and debug I2C FRAM/EEPROM chips on both Arduino and MicroPython platforms. It handles device detection, memory operations, and user interaction via simple commands, making it ideal for learning, prototyping, or diagnostics.