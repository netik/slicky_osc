# Slicky OSC

OSC server and utilities for controlling a USB **Slicky Light** over the network. The main program listens for UDP OSC messages and drives the light via HID; optional SSDP discovery and status feedback are supported.

## Overview

| Binary | Purpose |
|--------|---------|
| **oscserver** | Main daemon: UDP OSC listener, HID driver, SSDP discovery, status feedback |
| **oscclient** | Example CLI client: sends one OSC command and exits |
| **rainbow** | Standalone demo: rainbow animation on the device (no OSC) |

The device is a Microchip USB HID product (Vendor `0x04D8`, Product `0xEC24`). Color is set with a 65-byte HID report: `0x00 0x0A 0x04 0x00 0x00 W B G R` (see `led.c`).

## Dependencies

- **hidapi** — HID access. On macOS with Homebrew:
  ```bash
  brew install hidapi
  ```
- **log.c** — vendored logging (submodule):
  ```bash
  git submodule update --init --recursive
  ```

## Building

```bash
git submodule update --init --recursive
make
```

Produces `oscserver`, `oscclient`, and `rainbow`.

**Note for developers:** The Makefile uses hardcoded include/lib paths for hidapi under `/opt/homebrew/Cellar/hidapi/`. If your hidapi version or prefix differs, adjust `INCLUDES` and `LIBS` in the Makefile (or use `pkg-config` if you add it).

## Running

- **Server** (default port 9000):
  ```bash
  ./oscserver
  ./oscserver -p 9000 -d    # custom port, debug logs
  ./oscserver -t            # test mode (no USB, for development)
  ```
- **Client** (sends to localhost:9000):
  ```bash
  ./oscclient setcolorhex ff0000
  ./oscclient setcolorint 16711680
  ./oscclient blink 1
  ```

## OSC API

### Incoming (client → server)

| Address | Format | Description |
|---------|--------|-------------|
| `/setcolorint` | `i` | Set color from 32-bit integer (e.g. `0xFF0000` = red). |
| `/setcolorhex` | `s` | Set color from hex string (e.g. `"ff0000"` or `"FF0000"`). |
| `/blink` | `i` | Enable (value &gt; 0) or disable (0) continuous blinking. |
| `/blink_on_change` | `i` | Enable (value &gt; 0) or disable (0) brief blink on color change. |

### Feedback (server → client)

Sent to the **source IP** of the last received packet, on **UDP port 9500** (see `config.h`). Sent after each processed packet and periodically (e.g. every 1 s) to the last sender.

| Address | Format | Description |
|---------|--------|-------------|
| `/status/color` | `i` | Current RGB color (32-bit). |
| `/status/blinking` | `i` | `1` = continuous blink on, `0` = off. |
| `/status/blink_on_change` | `i` | `1` = blink-on-change on, `0` = off. |

## Configuration

- **config.h** — Device IDs, SSDP/feedback ports, intervals, buffer sizes.
- **CLI** — `oscserver` supports `-p` (port), `-d` (debug), `-t` (test mode). Run with `-h` for full help.

## Project layout

| Path | Role |
|------|------|
| `oscserver.c` | Main loop: socket, select, OSC parse, state, SSDP. |
| `state.c` / `state.h` | OSC command handling and blink logic; builds feedback messages. |
| `led.c` / `led.h` | HID open/close, color and rainbow; used by oscserver and rainbow. |
| `tinyosc.c` / `tinyosc.h` | OSC parse/serialize. |
| `cli.c` / `cli.h` | Argument parsing and usage. |
| `ssdp.c` / `ssdp.h` | SSDP announcements for discovery. |
| `config.h` | Central constants (ports, IDs, intervals). |
| `log.c/` | Submodule; used for leveled logging. |

## Development

- Use **test mode** when no device is attached: `./oscserver -t`. HID is not initialised and no device is opened; OSC and feedback still run.
- Default OSC port is **9000**; feedback port is **9500** (both in `config.h` / CLI).
- `oscclient` sends to `INADDR_ANY` (effectively localhost) and hardcodes port 9000; change `PORT` in `oscclient.c` if needed.

## License

See `LICENSE.TXT` in the repository root.
