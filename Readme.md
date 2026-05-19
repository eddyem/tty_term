# tty_term – Terminal Serial/Socket Communication Tool

`tty_term` is a terminal-based application for communicating with serial ports, TCP sockets, and
UNIX domain sockets. It provides a split-screen interface with an ncurses display area for received
data and a readline-powered command line for sending data.

## Features

- Connect to:
  - Serial TTY devices (with configurable baud rate, parity, data bits, stop bits)
  - Network sockets (TCP client)
  - UNIX domain sockets
- Multiple display modes:
  - **Text** – printable characters shown as-is, non‑printable as `\xXX`
  - **Raw** – each byte as a hexadecimal value (`XX`)
  - **Hexdump** – classic hexdump format (address + hex bytes + ASCII)
- Multiple input modes:
  - **Text** – free text, supports C‑style escape sequences
  - **Raw** – numbers in decimal, hex (`0x…`), binary (`0b…`), octal (`0…`)
  - **Hex** – hexadecimal bytes (with or without spaces)
  - **Modbus RTU (raw/hex)** – automatically appends CRC‑16 (Modbus)
- Scrollable output buffer, mouse wheel support
- Configurable end‑of‑line sequence (LF, CR, CR+LF, LF+CR)
- Dump all traffic to a file
- Exclusive mode for serial devices

## Requirements

- Linux (or POSIX system with termios)
- C23 compiler (GCC or Clang)
- Libraries:
  - `ncurses`
  - `readline`
  - `usefull_macros` (≥0.3.5) – [snippets_library](https://github.com/eddyem/snippets_library)

## Installation

```bash
git clone --depth=1 https://github.com/eddyem/tty_term.git
cd tty_term
mkdir build && cd build
cmake ..
make
sudo make install
```

By default the program is installed into `/usr/local/bin`.
Use `-DDEBUG=ON` with CMake to build a debug version with extra logging.

## Usage

```
ttyterm [options]
```

You **must** specify the device/socket path with `-n` (or `--name`).
All other options are optional.

### Command‑line options

| Option            | Argument               | Default        | Description                                                             |
| ----------------- | ---------------------- | -------------- | ----------------------------------------------------------------------- |
| `-h`, `--help`    | –                      | –              | Show help                                                               |
| `-s`, `--speed`   | integer                | 9600           | Baud rate (serial only)                                                 |
| `-n`, `--name`    | string                 | –              | Serial device (e.g. `/dev/ttyUSB0`), server `host:port`, or socket path |
| `-e`, `--eol`     | `n`, `r`, `rn`, `nr`   | `n`            | End‑of‑line sequence when sending in text mode                          |
| `-t`, `--timeout` | milliseconds (float)   | 100            | Timeout for `select()` on read                                          |
| `-S`, `--socket`  | –                      | –              | Treat `-n` as a network socket (TCP client)                             |
| `-d`, `--dumpfile`| filename               | –              | Append all received and sent data to this file                          |
| `-f`, `--format`  | string (e.g. `8N1`)    | `8N1`          | Serial line format (data bits, parity, stop bits)                       |
| `-U`, `--unix`    | –                      | –              | Treat `-n` as a UNIX domain socket path                                 |
| `-x`, `--exclusive`| –                     | –              | Open serial device in exclusive mode                                    |

**Examples:**

```bash
# Serial port at 115200, 8N1, CR+LF as EOL
ttyterm -n /dev/ttyUSB0 -s 115200 -e rn

# TCP connection to a Modbus server
ttyterm -n 192.168.1.100:502 -S

# UNIX socket
ttyterm -n /tmp/mysocket -U

# Log all communication to a file
ttyterm -n /dev/ttyACM0 -d session.log
```

## User Interface

The screen is divided into three areas:

1. **Message window** (top, scrolling) – displays received data in the selected display mode.
2. **Status bar** (middle) – shows connection details, current display mode, and whether you are in
**Insert** (editing) or **Scroll** mode.
3. **Command line** (bottom) – where you type data to send.

### Keyboard Controls

#### Global (always active)

| Key            | Action                                       |
| -------------- | -------------------------------------------- |
| `F1`           | Show poput help window                       |
| `F2`           | Switch input/output mode to **Text**         |
| `F3`           | Switch input/output mode to **Raw**          |
| `F4`           | Switch input/output mode to **Hexdump**      |
| `F5`           | Switch **input** mode to Modbus RTU (raw)    |
| `F6`           | Switch **input** mode to Modbus RTU (hex)    |
| `Tab`          | Toggle between **Insert** and **Scroll** mode|
| `Ctrl`+`C` / `Q` / `Ctrl`+`D` | Quit program                  |
| Mouse wheel    | Scroll output up/down                        |

The action of `F2`-`F6` keys depends on current mode: **Insert** — change input mode,
**Scroll** — change output mode.

#### Insert mode (for typing commands)

- Regular typing works as in `readline` (supports line editing, history, arrow keys).
- `Enter` sends the current line.
- `Ctrl`+`D` on an empty line quits.
- Escape sequences are supported when input mode is **Text** (see below).

#### Scroll mode (navigation)

| Key                | Action                           |
| ------------------ | -------------------------------- |
| `↑` / `Ctrl`+`P`   | Scroll up one line               |
| `↓` / `Ctrl`+`N`   | Scroll down one line             |
| `PageUp` / `Ctrl`+`B` | Scroll up one page            |
| `PageDown` / `Ctrl`+`F` | Scroll down one page         |
| `Home`             | Jump to the very top of the buffer |
| `End`              | Jump to the very bottom (follow new data) |
| `←` / `Ctrl`+`L`   | Scroll left (if lines exceed screen width) |
| `→` / `Ctrl`+`R`   | Scroll right                        |
| `Q`                | Quit (same as `Ctrl`+`C`)           |

### Data Display Modes

- **Text** (`F2`):
  Printable ASCII characters (32–126) are shown as‑is.
  Line feed (`\n`) ends the current line.
  Non‑printable bytes and control characters are displayed as `\xHH`.

- **Raw** (`F3`):
  Every byte is shown as two hexadecimal digits, separated by a space.
  Example: `41 42 0D 0A`

- **Hexdump** (`F4`):
  Classic hexdump format:
  `address   hex bytes (8‑byte groups)   |ASCII representation|`
  Lines are automatically adjusted to the terminal width.

### Input Modes and Data Formatting

The **input mode** determines how the text you type is converted into raw bytes before
transmission.

#### Text mode (default after `F2`)

- Most characters are sent as their ASCII/UTF‑8 bytes.
- **Escape sequences** (C‑style) are recognised after a backslash `\`:

| Sequence | Meaning           | Byte value |
| -------- | ----------------- | ---------- |
| `\a`     | Bell (alert)      | 0x07       |
| `\b`     | Backspace         | 0x08       |
| `\e`     | Escape            | 0x1B       |
| `\f`     | Form feed         | 0x0C       |
| `\n`     | Line feed (LF)    | 0x0A       |
| `\r`     | Carriage return   | 0x0D       |
| `\t`     | Horizontal tab    | 0x09       |
| `\v`     | Vertical tab      | 0x0B       |
| `\xHH`   | Hexadecimal byte  | 0xHH       |
| `\0`–`\377` | Octal byte    | up to 0xFF  |

- After processing escapes, the configured EOL sequence (e.g. `\r\n`) is **automatically appended**
to every line you send.

#### Raw mode (`F3` for display, but also used as input)

Input is a sequence of numbers, each representing one byte.
Numbers can be:
- Decimal: `65` (space‑separated)
- Hexadecimal: `0x41` or `0X41`
- Binary: `0b01000001`
- Octal: `0101` (leading zero)

Spaces are ignored, text and other characters treating "as is".
Example: `hel 0x6c 0x6f 0x0A`

The EOL is **not** added automatically in raw mode.

#### Hex mode (`F4` for display, also used as input)

Input must consist of hexadecimal digits (pairs are merged into bytes).
Spaces are ignored.
Example: `41420D0A` → sends four bytes: `A`, `B`, CR, LF.

#### Modbus RTU modes (`F5` – raw, `F6` – hex)

- Same input syntax as **Raw** or **Hex**, respectively.
- After converting the user‑typed numbers/hex, the program **calculates a 16‑bit Modbus CRC‑16**
(polynomial 0xA001) and appends it (low byte first).

Example (raw): `1 3 0 0 0 10`
→ sends: `01 03 00 00 00 0A C5 CD`

## Logging

If `-d <filename>` is given, all data is appended to that file.
Received lines are prefixed with `< `, sent lines with `> `.

## Notes

- The program uses the library `usefull_macros` for low‑level terminal/socket operations and
logging.
- Debug builds (`cmake -DDEBUG=ON`) produce a `debug.log` file with verbose diagnostic output.
- The terminal must support at least 80 columns for comfortable hexdump viewing.
- When resizing the terminal window, the output is automatically reformatted.

## License

GNU General Public License version 3 (or later).
See the `LICENSE` file or [https://www.gnu.org/licenses/gpl-3.0.html](https://www.gnu.org/licenses/gpl-3.0.html).

## Author

Edward V. Emelianov <edward.emelianoff@gmail.com>
Project page: [https://github.com/eddyem/tty_term](https://github.com/eddyem/tty_term)
