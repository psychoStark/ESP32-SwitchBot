# ESP32-S3 SwitchBot Dashboard with Embedded Tailscale VPN

Transform an **ESP32-S3** into an encrypted, remotely-accessible smart switch actuator. The device drives a mechanical servo motor to physically actuate a power button on command, accessible from anywhere in the world via an embedded **Tailscale VPN** client (`microlink`) as well as local Wi-Fi.

The system features dual user interfaces: a modern browser dashboard and an interactive, real-time terminal command center served directly to `curl`.

---

## Key Features

* **Embedded Tailscale VPN (`microlink`):** Full zero-config WireGuard mesh VPN client built directly into the firmware. Connects via DERP relays, STUN NAT traversal, and DISCO direct peer-to-peer discovery. Reachable from anywhere on your Tailnet via MagicDNS (`http://esp32/`) or subnet route (`192.168.1.50`).
* **Dual-UX Interface:**
  * **Web Dashboard:** Mobile-first, cyber-dark UI with live telemetry, sensor gauges, and VPN indicators.
  * **Interactive Terminal CLI:** Detects `curl` requests and streams a live, keystroke-driven Bash TUI menu without needing any client-side software.
* **Multi-Core FreeRTOS Architecture:**
  * **Core 0:** Dedicated `http_srv` webserver task (priority 4, 8 KB stack) and non-blocking DERP network I/O.
  * **Core 1:** High-performance WireGuard cryptographic engine (`wg_mgr`) and main application logic.
* **Non-Blocking Async Servo Actuation:** Immediate `< 5ms` HTTP 200 response with background queue execution to eliminate TCP timeouts over high-latency remote links.
* **Flash Wear-Leveling Forensics:** Ring-buffered NVS event storage (`b0`..`b49`) reducing flash write wear by ~98%, tracking reset causes, boot times, and power-off downtime.
* **Gated On-Demand OTA:** Secure Over-The-Air flash updates locked by default. Enabled via authenticated POST for a 10-minute auto-closing window.
* **Active Wi-Fi Performance:** Operates at 240 MHz with disabled Wi-Fi modem sleep (`WiFi.setSleep(false)`) for zero-jitter, sub-millisecond network responsiveness.

---

## Hardware Specifications & Wiring

* **MCU:** ESP32-S3-WROOM-1 / N16R8 (16 MB Flash, 8 MB Octal PSRAM)
* **Actuator:** Standard 3.3V / 5V Servo (SG90, MG90S, etc.)
* **Pin Connections:**
  * **Servo Signal (PWM):** GPIO 1
  * **Servo VCC:** 5V / 3.3V
  * **Servo GND:** Ground

---

## Project Structure

```
ESP32-SwitchBot/
├── CMakeLists.txt            # Root CMake project configuration
├── partitions.csv            # Custom 16MB partition table (dual 4MB OTA slots)
├── sdkconfig.defaults        # ESP-IDF configurations (Octal PSRAM, ChaCha20-Poly1305)
├── dependencies.lock         # Managed IDF component locks
├── documentation.md          # Comprehensive firmware & network documentation
├── main/
│   ├── CMakeLists.txt        # IDF component registration & linker wrappers
│   ├── main.cpp              # Primary firmware application & HTTP endpoints
│   ├── secrets.h             # Wi-Fi credentials & Tailscale auth key (gitignored)
│   └── calibrateservo/       # Standalone servo angle calibration sketch
└── components/
    ├── microlink/            # Embedded Tailscale client (WireGuard, DERP, DISCO)
    ├── wireguard_lwip/       # Symlink to microlink wireguard_lwip component
    ├── arduino/              # Arduino-ESP32 v2.x component (gitignored external)
    └── ESP32Servo/           # ESP32Servo PWM library (gitignored external)
```

---

## Getting Started

### 1. Prerequisites

* [ESP-IDF v5.1.x](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/get-started/) installed.
* USB-to-UART cable connected to the ESP32-S3.

### 2. Clone Dependencies

Clone the external vendored components into `components/`:

```bash
# Clone Arduino-ESP32 component
git clone -b release/v2.x https://github.com/espressif/arduino-esp32.git components/arduino

# Clone ESP32Servo library
git clone https://github.com/madhephaestus/ESP32Servo.git components/ESP32Servo
```

### 3. Configure Credentials (`secrets.h`)

Create `main/secrets.h` (this file is excluded from Git):

```cpp
#pragma once

#define WIFI_SSID       "Your_WiFi_SSID"
#define WIFI_PASSWORD   "Your_WiFi_Password"

#define TAILSCALE_KEY   "tskey-auth-kXXXXX-XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define TAILSCALE_HOST  "esp32"

#define OTA_KEY         "YourSecretOtaPassword"
```

### 4. Build and Flash

```bash
# Activate ESP-IDF environment
source ~/esp/esp-idf-v5.1.4/export.sh

# Build the project
idf.py build

# Flash to the board and open serial monitor
idf.py -p /dev/cu.usbserial-0001 flash monitor
```

---

## Network & Access Methods

The device can be accessed through multiple paths:

| Access Method | URL | Network Context |
|---|---|---|
| **MagicDNS (Tailscale)** | `http://esp32/main` | Any machine connected to your Tailnet (Worldwide) |
| **Tailscale Subnet Route** | `http://192.168.1.50/main` | Any machine connected to your Tailnet or local Wi-Fi |
| **Tailscale VPN IP** | `http://100.123.197.88/main` | Any machine connected to your Tailnet |
| **Local mDNS** | `http://esp32.local/main` | Devices on the same local Wi-Fi network |

> [!TIP]
> To access `http://esp32.local/main` while outside your home Wi-Fi over Tailscale, add this entry to `/etc/hosts` on your client machine:
> ```bash
> echo "192.168.1.50 esp32.local" | sudo tee -a /etc/hosts
> ```

---

## Terminal Command Center (`curl`)

Connect to the device using `curl` to launch the interactive live dashboard:

```bash
curl -s http://esp32/main | bash
```

### Add a Shell Alias

Add this alias to your `~/.zshrc` or `~/.bashrc`:

```bash
alias switchbot="curl -s http://esp32/main | bash"
```

Once reloaded (`source ~/.zshrc`), type **`switchbot`** in your terminal to open the single-keystroke control menu:
* `[1]` Trigger Power Button (fires servo)
* `[2]` Device Info & Hardware Telemetry (live updating)
* `[3]` Crash Logs & Debug (boot history & downtime forensics)
* `[4]` Reboot ESP32
* `[5]` Clear Logs & Timers
* `[6]` Enable OTA Update Window (10 min)
* `[X]` Exit

---

## API Reference

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Triggers the physical servo tap sequence (async queue with 2s cooldown). |
| `/main` | GET | Delivers the Web UI dashboard or the interactive terminal Bash script. |
| `/info` | GET | Returns hardware diagnostics (Uptime, Temperature, RAM, Flash, PSRAM, Clock). |
| `/debug` | GET | Displays rolling crash history, last reset cause, and power-off downtime. |
| `/api/live` | GET | High-frequency JSON telemetry endpoint (`{"u":"...","t":51.2,"p":0.41...}`). |
| `/ota/enable` | POST | Unlocks the ArduinoOTA port for 10 minutes (`-d "key=YOUR_KEY"`). |
| `/ota/disable` | POST | Manually closes the ArduinoOTA update port. |
| `/clear-logs` | POST | Securely wipes NVS crash records and flash timers. |
| `/reboot` | POST | Gracefully restarts the microcontroller. |

---

## License

This project is open source and available under the [Apache 2.0 License](LICENSE).