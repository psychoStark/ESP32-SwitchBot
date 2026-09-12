# ESP32-SwitchBot Dashboard — Firmware & Architecture Documentation

---

## Overview

This firmware transforms an **ESP32-S3-WROOM-N16R8** into a dual-interface, encrypted smart switch actuator. It mechanically actuates a button using a servo motor upon authenticated command. The device features two concurrent interfaces — a responsive cyber-dark web dashboard and an interactive terminal session via `curl` — and is accessible globally through an embedded **Tailscale VPN** client (`microlink`) with an automated cloud API failover watchdog.

**Primary Capability:** Actuate a physical power button remotely from a web browser, a terminal, or any device on your Tailnet, complete with live hardware telemetry, interactive servo calibration, crash forensics, downtime tracking, Tailscale session logs, and gated OTA updates.

---

## Project Structure

```
ESP32-SwitchBot/
├── CMakeLists.txt            — Root CMake project file
├── partitions.csv            — Custom flash partition layout (dual 4MB OTA slots)
├── sdkconfig.defaults        — ESP-IDF build configuration (80MHz CPU, Octal PSRAM, ChaCha20-Poly1305)
├── dependencies.lock         — Locked managed component versions
├── documentation.md          — This architecture & firmware specification
├── README.md                 — Repository overview and quickstart guide
├── main/
│   ├── CMakeLists.txt        — IDF component registration & linker wrapping
│   ├── main.cpp              — Primary firmware (application logic, web routes, TUI, watchdog)
│   ├── calibration.h         — Servo calibration storage, web SVG dial UI & cURL script
│   ├── curl_scripts.h        — Interactive bash TUI dashboard generator for cURL clients
│   ├── web_pages.h           — Responsive cyber-dark HTML templates, CSS & poll scripts
│   ├── secrets.h             — Wi-Fi credentials, Tailscale keys & API watchdog (gitignored)
│   ├── calibrateservo/
│   │   └── calibrateservo.ino — Standalone servo angle calibration sketch
│   └── switchbot/
│       └── switchbot.ino     — Legacy reference Arduino sketch (not compiled)
└── components/
    ├── microlink/            — Tailscale VPN client (WireGuard, DERP, STUN, DISCO)
    │   ├── include/          — Public and internal headers
    │   ├── src/              — C source files (ml_wg_mgr, ml_coord, ml_derp, etc.)
    │   └── Kconfig           — Menuconfig options
    ├── wireguard_lwip/       — Symlink to microlink/components/wireguard_lwip
    ├── arduino/              — Arduino-ESP32 v2.x component (gitignored external)
    └── ESP32Servo/           — Servo PWM library (gitignored external)
```

---

## Hardware & Network Configuration

Configuration values are defined in [`main/main.cpp`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/main.cpp), [`main/calibration.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/calibration.h), and [`main/secrets.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/secrets.h).

| Parameter | Configured Value | Description |
|---|---|---|
| **MCU** | ESP32-S3-WROOM-N16R8 | 16 MB Flash, 8 MB Octal PSRAM |
| **CPU Clock** | 80 MHz | Low-power CPU frequency (~38-41°C operating temperature) |
| **Wi-Fi SSID** | Configured in `secrets.h` | 2.4 GHz 802.11 b/g/n station mode |
| **Static IP** | `192.168.1.50` | Fixed local IPv4 address |
| **Gateway / DNS** | `192.168.1.1` | Local network router address |
| **Subnet Mask** | `255.255.255.0` | Local `/24` subnet mask |
| **Wi-Fi Tx Power** | 15 dBm (`WIFI_POWER_15dBm`)| Optimized RF output power for thermal stability |
| **Local Hostname** | `esp32.local` | mDNS identifier for local network discovery |
| **Tailscale Device Name** | `esp32` | MagicDNS host name (`http://esp32/`) |
| **Subnet Route Advertised**| `nullptr` | Unset to prevent `/32` route hijacking when primary router is active |
| **Servo GPIO Pin** | GPIO 1 | PWM control line connected to servo signal wire |
| **Servo PWM Range** | 500 – 2400 µs | Pulse width limits for 50 Hz PWM period |
| **Servo Rest Angle** | 0° (default, configurable)| Idle position hovering above button (stored in NVS `servo_cal`) |
| **Servo Press Angle** | 10° (default, configurable)| Actuation position pushing button (stored in NVS `servo_cal`) |
| **Servo Press Duration** | 200 ms (default, configurable)| Hold duration during actuation (stored in NVS `servo_cal`) |
| **Wi-Fi Modem Sleep** | Disabled (`WiFi.setSleep(false)`)| Prevents radio latency; instant packet response |
| **NTP Timezone Offset** | +5:30 IST (19800 sec) | India Standard Time (no daylight savings) |
| **HTTP Port** | 80 | Standard HTTP listening port |

---

## Flash Partition Layout

Defined in [`partitions.csv`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/partitions.csv):

| Partition | Type | SubType | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data | nvs | 20 KB | Key-value store (crash logs, boot counters, servo cal, Tailscale logs) |
| `otadata` | data | ota | 8 KB | OTA active partition selection data |
| `phy_init` | data | phy | 4 KB | RF physical calibration parameters |
| `ota_0` | app | ota_0 | **4 MB** | Factory / Primary firmware slot |
| `ota_1` | app | ota_1 | **4 MB** | Secondary / OTA update slot |
| `spiffs` | data | spiffs | **4 MB** | Filesystem storage (reserved) |

---

## Build System & Linker Configuration

The firmware compiles via **ESP-IDF v5.1.x**. The Arduino framework runs as an IDF component alongside native IDF components.

### 1. Linker Wrap for Tailscale Routing

To resolve asymmetric routing when clients access `192.168.1.50` over Tailscale WireGuard, the GNU linker intercepts calls to `ip4_route_src_hook` via `-Wl,--wrap=ip4_route_src_hook` in [`main/CMakeLists.txt`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/CMakeLists.txt):

```c
// Implemented in components/microlink/src/ml_wg_mgr.c
extern struct netif *__real_ip4_route_src_hook(const ip4_addr_t *src, const ip4_addr_t *dest);

struct netif *__wrap_ip4_route_src_hook(const ip4_addr_t *src, const ip4_addr_t *dest)
{
    if (dest != NULL && !ip4_addr_isany(dest) && s_wg_netif != NULL && netif_is_up(s_wg_netif)) {
        // If destination is in Tailscale CGNAT subnet (100.64.0.0/10), route to WireGuard netif
        if ((ip4_addr_get_u32(dest) & PP_HTONL(0xFFC00000)) == PP_HTONL(0x64400000)) {
            return s_wg_netif;
        }
    }
    return __real_ip4_route_src_hook(src, dest);
}
```

### 2. Inbound RX Interface Selection

In [`components/microlink/components/wireguard_lwip/src/wireguardif.c`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/components/microlink/components/wireguard_lwip/src/wireguardif.c), incoming decrypted packets destined for `192.168.1.50` are dynamically mapped to the matching local network interface (`st0`) before invoking `ip_input()`. This prevents lwIP from forwarding or dropping the packet when `CONFIG_LWIP_IP_FORWARD=1` is active.

---

## Core Systems & Architecture

### 1. Tailscale Cloud API Watchdog & Subnet Failover

To keep the ESP32 chip running cool (~38-41°C) and minimize network contention, the firmware implements an intelligent **Cloud API Watchdog**:

* **Primary Subnet Router Monitoring:** The ESP32 queries the official Tailscale REST API (`https://api.tailscale.com/api/v2/device/DEVICE_ID` or `/tailnet/-/devices`) over HTTPS using `esp_http_client` with `esp_crt_bundle_attach`.
* **Cold Standby Mode:** When the primary subnet router (e.g. `moto-g32`) is verified online with the Tailscale control plane, the ESP32 keeps its local `microlink` VPN instance in **Standby**. All Tailscale traffic routes through the primary subnet router.
* **Automated Failover Trigger:** The watchdog queries the API every **45 seconds** (<0.8% radio duty cycle). If the primary subnet router fails 2 consecutive checks (~90 seconds), the ESP32 automatically invokes `startTailscale()` to bring up its embedded VPN node.
* **Automatic Recovery:** Once the primary subnet router reconnects to Tailscale, the watchdog automatically calls `stopTailscale()`, returning the ESP32 to cold Standby.

### 2. Interactive Servo Calibration System

Defined in [`main/calibration.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/calibration.h):

* **NVS Persistence (`servo_cal` namespace):** Stores `calibrated` (bool), `rest_angle` (-180° to 180°), `press_angle` (-180° to 180°), and `press_dur` (50–3000 ms). Default values: `rest_angle = 0°`, `press_angle = 10°`, `press_dur = 200 ms`.
* **Web UI Calibration (`/calibrate`):**
  * Features two interactive, circular SVG dials for Rest and Press angles.
  * **Gap Barrier Logic:** Prevents accidental 360-degree rotational flips across the bottom 40° gap.
  * **Live Servo Preview:** Dragging the Rest dial debounces live servo position updates (`POST /api/calibrate/move`).
  * **Press & Hold Control:** Real-time push button (`POST /api/calibrate/hold`) drives servo to Press angle on touch and returns to Rest angle on release.
  * **Revert Animation:** Smooth JavaScript requestAnimationFrame interpolates controls back to saved values.
* **Terminal CLI Calibration (`/calibrate` via `curl`):**
  * Line-by-line interactive Bash wizard prompting for Rest angle, Press angle, and Duration with step-by-step confirmation and test taps.
* **Uncalibrated State Gating:** Accessing `/` while uncalibrated redirects Web clients to `/calibrate` or outputs CLI calibration instructions for `curl` clients.

### 3. Multi-Core FreeRTOS Concurrency

| Task | Core | Priority | Stack | Function |
|---|---|---|---|---|
| `http_srv` | **Core 0** | 4 | 8 KB | Runs `server.handleClient()` continuously without blocking Arduino `loop()` |
| `ml_net_io` | **Core 0** | 5 | 6 KB | Socket I/O and TLS transport for DERP and control plane |
| `ml_wg_mgr` | **Core 1** | 6 | 8 KB | WireGuard peer management, handshake timers, and cryptography |
| `arduino_loop`| **Core 1** | 1 | 8 KB | Servo actuation, NTP sync, heartbeat NVS writes, Watchdog checks, and OTA |

### 4. Asynchronous Servo Actuation

To prevent HTTP request timeouts over remote cellular networks, servo actuation is completely decoupled from the HTTP response:
1. `handleRoot()` receives the HTTP request.
2. Checks cooldown (`PRESS_COOLDOWN_MS = 2000`).
3. Sets atomic flag `pendingPress = true`.
4. Immediately sends `HTTP 200 OK` (with `Connection: close`) in `< 5ms`.
5. `loop()` on Core 1 detects `pendingPress`, attaches the servo, moves to `pressAngle`, holds for `pressDurationMs`, returns to `restAngle`, settles for 300 ms, and detaches the PWM pin.

### 5. Flash Wear-Leveling Forensics & Activity Ring Buffers

Replaces monolithic flash writes with rolling ring buffers across individual keys to minimize flash write wear by ~98%:

* **Boot Forensics (`esp_log` namespace):** Keys `b0`..`b49` store compact 9-byte `BootLog` structs (timestamp, downtime seconds, reset reason code). Tracks `last_alive` heartbeat every 30s for power outage calculation.
* **Servo Trigger Logs (`servo_log` namespace):** Keys `s0`..`s19` store `ServoLog` structs (timestamp, `fromCurl` flag).
* **Tailscale Session Logs (`ts_log` namespace):** Keys `t0`..`t19` store `TailscaleLog` structs (start time, end time, downtime seconds). Preserves session durations across soft reboots using RTC memory (`rtc_ts_duration_s`, `rtc_ts_was_active`).

### 6. Gated On-Demand OTA

* `ArduinoOTA` is **disabled by default** at boot.
* **Activation:** Triggered via `POST /ota/enable` with body `key=OTA_KEY` (or via option `[O]` in the terminal menu).
* **Auto-Disable:** The OTA listener automatically de-initializes and closes port 3232 after 10 minutes (`OTA_AUTO_TIMEOUT_MS = 600,000 ms`).
* **Manual Disable:** Triggered anytime via `POST /ota/disable`.

---

## API Endpoints Reference

All endpoints return explicit `Connection: close` headers to immediately release lwIP socket descriptors.

| Endpoint | Method | Request Format | Response Content | Description |
|---|---|---|---|---|
| `/` | GET | None | HTML / Text | Triggers the physical button press sequence (2s cooldown). Redirects to `/calibrate` if uncalibrated. |
| `/main` | GET | `curl` or Browser | HTML / Bash Script | Serves the web dashboard or streams the self-executing terminal Bash TUI. |
| `/info` | GET | None | HTML / Text Table | Returns hardware specs (Uptime, Temperature, RAM, Flash, PSRAM, Clock, Wi-Fi, Tailscale status). |
| `/debug` | GET | None | HTML / Text | Displays crash logs, reset cause, downtime, servo history, and Tailscale session logs. |
| `/api/live` | GET | None | JSON | Live stream: `{"u":"...","t":40.5,"c":80,"cal":1,"s_rest":0,"s_press":10...}`. |
| `/calibrate` | GET | `curl` or Browser | HTML / Bash Script | Serves Web SVG dial calibration page or `curl` interactive Bash calibration wizard. |
| `/api/calibrate/move` | POST | `?angle=X` | Text | Drives servo to specified angle immediately (-180° to 180°). |
| `/api/calibrate/hold` | POST | `?state=1\|0&press=X&rest=Y` | Text | Holds servo at press angle (`state=1`) or releases to rest angle (`state=0`). |
| `/api/calibrate/test` | POST | `?rest=X&press=Y&dur=Z` | Text | Executes test tap sequence with provided parameters. |
| `/api/calibrate/save` | POST | `?rest=X&press=Y&dur=Z` | Text | Persists rest angle, press angle, and duration to NVS (`servo_cal`). |
| `/api/calibrate/reset` | POST | None | Text | Clears calibration NVS data and resets device to uncalibrated initial defaults. |
| `/ota/enable` | POST | Form / Body `key=...` | HTML / Text | Unlocks the OTA port (3232) for a 10-minute auto-closing window. |
| `/ota/disable`| POST | None | HTML / Text | Immediately shuts down the OTA listener. |
| `/clear-logs` | POST | None | HTML / Text | Clears NVS crash history, servo log, Tailscale log, and resets flash timers. |
| `/reboot` | POST | None | HTML / Text | Gracefully restarts the ESP32 after a 500 ms socket-flush delay. |

---

## Terminal Command Center (`curl`)

When queried with `curl`, the `/main` endpoint generates an interactive, full-screen TUI that listens for single keystrokes (`read -n 1 -s`):

```bash
curl -s http://esp32/main | bash
```

```
=== SwitchBot Dashboard ===
 [1] Trigger Servo / Calibrate Servo
 [2] Device Info
 [3] Logs & Debug
 [C] Clear Logs
 [O] Enable OTA
 [R] Reboot ESP32
 [S] Recalibrate Servo
 [X] Exit
```

Sub-menus auto-refresh and accept single-key commands without requiring Enter:
* `B`: Back to Menu
* `R`: Reboot ESP32
* `C`: Clear Logs (Debug menu)
* `O`: Enable / Disable OTA (Debug menu)
* `S`: Launch Servo Calibration Wizard
* `X`: Exit to shell

---

## Client Hostname Resolution

| Address | Resolver | Supported Networks |
|---|---|---|
| `http://esp32/main` | Tailscale MagicDNS | Any device connected to your Tailscale network (LAN & WAN) |
| `http://192.168.1.50/main` | Local Routing / Tailscale Subnet | Local Wi-Fi and Tailscale peers |
| `http://esp32.local/main` | Multicast DNS (mDNS) | Local Wi-Fi link only |

> [!NOTE]
> Because macOS queries `.local` exclusively via link-local multicast (which does not cross Layer 3 WireGuard tunnels), you can make `http://esp32.local/main` resolve over Tailscale when away from home by adding an entry to `/etc/hosts`:
> ```bash
> echo "192.168.1.50 esp32.local" | sudo tee -a /etc/hosts
> ```
