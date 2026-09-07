# ESP32-C3 Cisco Console Server

A remote COM port for console access to network equipment (Cisco and compatible) over Wi-Fi. An ESP32-C3 + MAX3232 turn a device's local console port into a TCP socket reachable over the network — no need to physically plug a laptop into every switch or router.

## Features

- **TCP ⇄ UART bridge** on port `8888` (default) — connect with `telnet`, `nc`, PuTTY, or SecureCRT just like a regular COM port.
- **Single active session** — a second connection attempt gets `BUSY` until the first one is closed (or kicked from the web portal).
- **Web portal** on port `80`: device status, traffic stats, session controls — dark theme.
- **RU/EN language switch** built right into the portal.
- **Configurable UART speed** (1200–115200 baud) — changes on the fly, no reboot needed.
- **Fallback access point**: if the target Wi-Fi network is unreachable or not configured, the device starts its own AP `ESP32-Console` at `192.168.4.1`.
- **Wi-Fi setup via the portal**: scan for networks, pick an SSID from the list or enter one manually (for hidden networks), save and reconnect — no reflashing required.
- **Session password** — optional protection for the TCP port, requested from the client on connect.
- **Network White List** — restrict connections by IP/CIDR (e.g. `192.168.1.0/24`).
- **Static IP** — a fixed address on the target Wi-Fi network (optional, DHCP by default).
- **Optional IP lockout** after 3 wrong password attempts (enabled via a compile-time flag, see below).
- All settings are stored in **NVS** (ESP32's non-volatile storage) and survive a reboot.

## Hardware

| Component | Purpose |
|---|---|
| ESP32-C3 Super Mini | main board |
| MAX3232 (RS-232 ⇄ TTL) | level shifting for the Cisco console port |
| Cisco console cable (RJ45 ⇄ DB9) | connection to the device |

### Pinout

| ESP32-C3 | MAX3232 |
|---|---|
| GPIO20 (RX1) | TXD |
| GPIO21 (TX1) | RXD |
| 3V3 / GND | power for the MAX3232 board |

## Installation

1. Open `esp32-tcp-com.ino` in Arduino IDE (or PlatformIO) with the **ESP32 by Espressif** board package installed (`WiFi.h`, `WebServer.h`, `Preferences.h` are all part of the standard core — no extra libraries needed).
2. Select the **ESP32C3 Dev Module** board.
3. Optionally change the factory defaults near the top of the file:
   ```cpp
   const char* DEFAULT_SSID     = "your_ssid";
   const char* DEFAULT_PASSWORD = "your_password";
   const char* apSsid     = "ESP32-Console";
   const char* apPassword = "console1234";
   ```
   These are just seed values — the actual working settings, once saved through the portal, live in NVS and survive reflashing (unless you fully erase the flash).
4. Upload the firmware.

## First boot

- If `DEFAULT_SSID` is unreachable or wrong, the device brings up its own network, **`ESP32-Console`** (password `console1234`), at **`192.168.4.1`**.
- Connect to that network and open `http://192.168.4.1` in a browser.
- Go to the **Wi-Fi** page, hit "Scan networks", pick the right one, enter the password, and save — the device will reboot and connect.

## Web portal

| Page | Purpose |
|---|---|
| `/` | status: network mode, session state, traffic stats, uptime |
| `/settings` | session password, White List, Static IP, UART speed |
| `/wifi` | Wi-Fi network scan and selection, UART speed, network reset |

The main actions are all available from the home page: disconnect a stuck client, restart the device, or reset the Wi-Fi settings (drops the device back into access-point mode at `192.168.4.1`).

## Connecting to the console

```bash
telnet <device-ip> 8888
# or
nc <device-ip> 8888
```

If the session password is enabled, the device sends `AUTH_REQUIRED` first — reply with the password on a single line.

## Compile-time options

Near the top of the file:

```cpp
// #define USE_BAN_LIST
```

Uncomment to enable a one-hour IP lockout after 3 wrong password attempts (only relevant when the session password is enabled).

## Security notes

- The session password is sent over TCP in plain text — this is meant for a trusted local network, not for direct exposure to the internet.
- The web portal itself has no login — restrict access to it at the network level (VLAN, ACL, White List).
- Scanning for Wi-Fi networks blocks the TCP bridge and web portal for a few seconds — keep that in mind if you change networks during an active console session.

> 🤖 The firmware and web portal were built together with **Claude** (Anthropic) 

## License

Use, modify, and share freely — built for a personal home/office infrastructure project.
