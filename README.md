# ESP32-S3 SwitchBot Dashboard with Embedded Tailscale VPN

Transform an **ESP32-S3** into an encrypted, remotely-accessible smart switch actuator. The device drives a mechanical servo motor to physically actuate a power button on command, accessible from anywhere in the world via an embedded **Tailscale VPN** client (`microlink`) as well as local Wi-Fi.

The system features dual user interfaces: a modern browser dashboard and an interactive, real-time terminal command center served directly to `curl`.

---

## Key Features

* **Tailscale Cloud Watchdog & Cold Standby/Failover:** Automatically monitors primary subnet router connectivity via Tailscale REST API (`api.tailscale.com`). When the primary subnet router is online, ESP32 Tailscale remains in cold **STANDBY** (~38-41°C at 80 MHz). If the router disconnects for 2 consecutive checks (~90s), ESP32 dynamically activates embedded Tailscale.
* **Embedded Tailscale VPN (`microlink`):** Full zero-config WireGuard mesh VPN client built directly into the firmware. Connects via DERP relays, STUN NAT traversal, and DISCO direct peer-to-peer discovery. Reachable from anywhere on your Tailnet via MagicDNS (`http://esp32/`) or subnet route (`192.168.1.50`).
* **Interactive Dual-UI Servo Calibration:** Touch-friendly circular SVG dials in Web UI (`/calibrate`) with bottom gap barrier protection & interactive step-by-step TUI in `curl`. Supports real-time angle adjustments, test tap execution, smooth revert animations, and NVS persistence (`servo_cal`).
* **Dual-UX Interface:**
  * **Web Dashboard:** Mobile-first, cyber-dark UI with live telemetry, sensor gauges, and VPN indicators.
  * **Interactive Terminal CLI:** Detects `curl` requests and streams a live, keystroke-driven Bash TUI menu without needing any client-side software.
* **Multi-Core FreeRTOS Architecture:**
  * **Core 0:** Dedicated `http_srv` webserver task (priority 4, 8 KB stack) and non-blocking DERP network I/O.
  * **Core 1:** High-performance WireGuard cryptographic engine (`wg_mgr`) and main application logic.
* **Non-Blocking Async Servo Actuation:** Immediate `< 5ms` HTTP 200 response with background queue execution to eliminate TCP timeouts over high-latency remote links.
* **Power & Heat Optimization:** Boot and operates at 80 MHz CPU frequency by default (`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_80`), Wi-Fi modem sleep IRAM optimization (`CONFIG_ESP_WIFI_SLP_IRAM_OPT`), keeping chip operating temperatures low (~38-41°C).
* **Flash Wear-Leveling Forensics & Activity Ring Buffers:** Ring-buffered NVS storage for boots (`b0`..`b49`), servo triggers (`s0`..`s19`), and Tailscale sessions (`t0`..`t19`), tracking reset causes, boot times, tap history, and downtime forensics.
* **Gated On-Demand OTA:** Secure Over-The-Air flash updates locked by default. Enabled via authenticated POST for a 10-minute auto-closing window.

---

## Hardware Specifications & Wiring

* **MCU:** ESP32-S3-WROOM-1 / N16R8 (16 MB Flash, 8 MB Octal PSRAM)
* **CPU Clock:** 80 MHz default (low power & heat, ~38-41°C operating temp)
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
├── sdkconfig.defaults        # ESP-IDF configurations (80MHz CPU, Octal PSRAM, ChaCha20-Poly1305)
├── dependencies.lock         # Managed IDF component locks
├── documentation.md          # Comprehensive firmware & network documentation
├── README.md                 # Repository overview and quickstart guide
├── main/
│   ├── CMakeLists.txt        # IDF component registration & linker wrappers
│   ├── main.cpp              # Primary firmware application, HTTP endpoints & watchdog
│   ├── calibration.h         # Servo calibration storage, web SVG dial UI & cURL script
│   ├── curl_scripts.h        # Interactive bash TUI dashboard generator for cURL clients
│   ├── web_pages.h           # Responsive cyber-dark HTML templates, CSS & poll scripts
│   ├── secrets.h             # Wi-Fi credentials, Tailscale keys & API watchdog (gitignored)
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

// Wi-Fi Credentials
#define WIFI_SSID       "Your_WiFi_SSID"
#define WIFI_PASSWORD   "Your_WiFi_Password"

// Tailscale Authentication & Machine Identity
#define TAILSCALE_KEY   "tskey-auth-kXXXXX-XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define TAILSCALE_HOST  "esp32"

// OTA Security Key
#define OTA_KEY         "YourSecretOtaPassword"

// Optional: Tailscale API Watchdog for Subnet Failover
#define TAILSCALE_API_KEY           "tskey-api-kXXXXX-XXXXXXXXXXXXXXXXXXXXXXXXXXXX"
#define TAILSCALE_SUBNET_DEVICE_ID  "123456789" // Numeric Device ID or machine hostname (e.g., "moto-g32")
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
* `[1]` Trigger Servo / Calibrate Servo
* `[2]` Device Info & Hardware Telemetry (live updating)
* `[3]` Crash Logs & Debug (boot history, downtime forensics & Tailscale activity)
* `[S]` Recalibrate Servo
* `[C]` Clear Logs & Timers
* `[O]` Enable / Disable OTA Update Window (10 min)
* `[R]` Reboot ESP32
* `[X]` Exit

---

## API Reference

| Endpoint | Method | Description |
|---|---|---|
| `/` | GET | Triggers the physical servo tap sequence (async queue with 2s cooldown). Redirects to `/calibrate` if uncalibrated. |
| `/main` | GET | Delivers the Web UI dashboard or the interactive terminal Bash script to `curl`. |
| `/info` | GET | Returns hardware diagnostics (Uptime, Temperature, RAM, Flash, PSRAM, Clock, Wi-Fi, Tailscale Status). |
| `/debug` | GET | Displays rolling crash history, last reset cause, downtime forensics, servo log & Tailscale sessions. |
| `/api/live` | GET | High-frequency JSON telemetry endpoint (`{"u":"...","t":40.5,"c":80,"cal":1...}`). |
| `/calibrate` | GET | Serves Web SVG dial calibration UI or interactive Bash script to `curl`. |
| `/api/calibrate/move` | POST | Live moves servo to specified angle (`?angle=X`). |
| `/api/calibrate/hold` | POST | Holds servo at press angle on button press (`?state=1`) or releases to rest (`?state=0`). |
| `/api/calibrate/test` | POST | Executes a test tap with given metrics (`?rest=X&press=Y&dur=Z`). |
| `/api/calibrate/save` | POST | Persists rest angle, press angle, and duration metrics to NVS (`servo_cal`). |
| `/api/calibrate/reset` | POST | Clears NVS calibration data and resets to uncalibrated initial defaults. |
| `/ota/enable` | POST | Unlocks the ArduinoOTA port for 10 minutes (`-d "key=YOUR_KEY"`). |
| `/ota/disable` | POST | Manually closes the ArduinoOTA update port. |
| `/clear-logs` | POST | Securely wipes NVS crash records, servo history, Tailscale session logs, and flash timers. |
| `/reboot` | POST | Gracefully restarts the microcontroller. |

---

## License

This project is open source and available under the [Apache 2.0 License](LICENSE).