# ESP32-C3 Cisco Console Server

A remote COM port for console access to network equipment (Cisco and compatible) over Wi-Fi. An ESP32-C3 + MAX3232 turn a device's local console port into a TCP socket reachable over the network — no need to physically plug a laptop into every switch or router.

> 🤖 The firmware and web portal were built together with **Claude** (Anthropic) — from the first version of the bridge to the final security setup, dark theme, WPA2-Enterprise, and portal password protection.

## Features

- **TCP ⇄ UART bridge** on port `8888` (default), with buffered block transfer and minimal Telnet negotiation (`IAC WILL ECHO`) so the client doesn't duplicate typed commands.
- **Single active session** — a second connection attempt gets `BUSY` until the first one is closed (or kicked from the web portal).
- **Web portal** on port `80`, dark theme, with an **RU/EN** language switch.
- **Portal password** — optionally protects the web interface itself (HTTP Basic Auth, username `admin`) once the device is on a real network. Can reuse the console session password or be set separately. Does not apply to the initial access-point setup portal.
- **Configurable UART speed** (1200–115200 baud) — changes on the fly, no reboot needed.
- **Fallback access point**: if the target Wi-Fi network is unreachable or not configured, the device starts its own AP at `192.168.4.1`.
- **Wi-Fi setup via the portal**: scan for networks, pick an SSID from the list or enter one manually (for hidden networks).
- **WPA2-Enterprise (802.1X)** — connect to office/corporate networks via PEAP/MSCHAPv2 (Identity/Username/Password), no CA certificate required.
- **Session password** — optional protection for the TCP port, requested from the client on connect.
- **Network White List** — restrict connections by IP/CIDR (e.g. `192.168.1.0/24`).
- **Static IP** — a fixed address on the target Wi-Fi network (optional, DHCP by default).
- **MAC address** shown on the portal — in both AP and STA mode (useful for router-side MAC filtering).
- **BOOT button factory reset** — holding the board's physical BOOT button for 5 seconds wipes the Wi-Fi settings and portal password and reboots into the fallback access point, for when you're locked out.
- **Automatic radio retry** — if the first Wi-Fi connection attempt fails, the firmware powers the radio off and on before retrying (works around the radio sometimes not fully reinitializing after a soft restart).
- **Reduced TX power** (`WIFI_POWER_8_5dBm`) — lowers peak current during transmit bursts, noticeably improving stability on boards with marginal power delivery (common on cheap ESP32-C3 SuperMini clones).
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
| GPIO9 (onboard BOOT button) | used for the factory-reset gesture (see below) |

⚠️ Cheap ESP32-C3 SuperMini boards often suffer from power sag during Wi-Fi transmission (especially in access-point mode). If you see flaky Wi-Fi or the device occasionally resets on its own, add a 470–1000 µF capacitor between 3V3 and GND as close to the board as possible.

⚠️ GPIO9 is a strapping pin. If your specific board/clone has anything unusual wired to it, the BOOT-button factory-reset feature could misfire. The firmware logs `[DEBUG] BOOT button pressed` / `held Ns...` to the serial monitor whenever it detects the pin going low — if these lines appear without you touching the button, that pin isn't behaving as a clean input on your board.

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

- If the target network is unreachable or not configured, the device brings up its own network, **`ESP32-Console`** (password `console1234`), at **`192.168.4.1`**.
- Connect to that network and open `http://192.168.4.1` in a browser — no login required at this stage.
- Go to the **Wi-Fi** page, hit "Scan networks", pick the right one, enter the password (or enable WPA2-Enterprise for a corporate network), and save — the device will reboot and connect.

## Web portal

| Page | Purpose |
|---|---|
| `/` | status: network mode, security type, MAC address, session state, traffic stats, uptime |
| `/settings` | session password, portal password, White List, Static IP, UART speed |
| `/wifi` | Wi-Fi network scan and selection, regular or WPA2-Enterprise, UART speed, network reset |

The main actions are all available from the home page: disconnect a stuck client, restart the device, or reset the Wi-Fi settings (drops the device back into access-point mode at `192.168.4.1`). The interface language is switched with the button in the top-right corner.

Once the device is connected to a real network, you can set a portal password on `/settings` — either your own, or reuse the console session password. If you ever forget it, hold the board's **BOOT button for 5 seconds**: this wipes the Wi-Fi settings and portal password and reboots into the passwordless setup AP.

## Connecting to the console

```bash
telnet <device-ip> 8888
# or
nc <device-ip> 8888
```

If the session password is enabled, the device sends `AUTH_REQUIRED` first — reply with the password on a single line.

For PuTTY, use the **Telnet** connection type rather than Raw — that way the client correctly understands the device's echo negotiation and won't duplicate typed commands.

## WPA2-Enterprise

On the `/wifi` page, switch to "WPA2-Enterprise (802.1X)" and fill in:
- **Identity** — usually the same as the username; format depends on the organization (`user@domain` or `DOMAIN\user`);
- **Username**;
- **Password**.

Supports PEAP/MSCHAPv2 without CA certificate validation — the most common minimal setup for corporate RADIUS servers. If the network requires EAP-TLS or strict server certificate validation, this setup won't work without further changes.

## Compile-time options

Near the top of the file:

```cpp
// #define USE_BAN_LIST
```

Uncomment to enable a one-hour IP lockout after 3 wrong password attempts (only relevant when the session password is enabled).

## Security notes

- The session password, portal password, and Wi-Fi/Enterprise passwords are transmitted/stored without encryption — this is meant for a trusted local network, not for direct exposure to the internet.
- The web portal has no password by default — set one on `/settings` once you've moved off the initial setup AP.
- Anyone with physical access to the board can factory-reset Wi-Fi and the portal password via the BOOT button — this is intentional (recovery path), but means physical security of the device still matters.
- Scanning for Wi-Fi networks blocks the TCP bridge and web portal for a few seconds — keep that in mind if you change networks during an active console session.

## License

Use, modify, and share freely — built for a personal home/office infrastructure project.
