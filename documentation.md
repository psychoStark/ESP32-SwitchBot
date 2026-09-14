# ESP32-SwitchBot Dashboard — Technical Architecture & Engineering Documentation

This document provides a deep, comprehensive technical specification of the **ESP32-SwitchBot** firmware architecture. It covers the FreeRTOS multi-core concurrency model, embedded Tailscale networking, zero-heap memory optimizations, hardware board compatibilities, NVS wear-leveling forensics, and configuration variables.

---

## Table of Contents
1. [System Architecture & Engineering Overview](#1-system-architecture--engineering-overview)
2. [Board Compatibility, Minimum Requirements & Hardware Limitations](#2-board-compatibility-minimum-requirements--hardware-limitations)
3. [Embedded Tailscale VPN & Subnet Routing Engine](#3-embedded-tailscale-vpn--subnet-routing-engine)
4. [FreeRTOS Concurrency & Low-Power Architecture](#4-freertos-concurrency--low-power-architecture)
5. [Memory Pipeline & Chunked HTTP Streaming Engine](#5-memory-pipeline--chunked-http-streaming-engine)
6. [NVS Wear-Leveling Forensics & Activity Ring Buffers](#6-nvs-wear-leveling-forensics--activity-ring-buffers)
7. [Interactive Servo Calibration Engine](#7-interactive-servo-calibration-engine)
8. [Interactive Terminal cURL Engine (`curl_scripts.h`)](#8-interactive-terminal-curl-engine-curl_scriptsh)
9. [Over-The-Air (OTA) Security Architecture](#9-over-the-air-ota-security-architecture)
10. [In-Code Variables, Customization & Network Gotchas](#10-in-code-variables-customization--network-gotchas)

---

## 1. System Architecture & Engineering Overview

The ESP32-SwitchBot firmware converts an Espressif microcontroller into a reliable, remotely-accessible switch actuator. It mechanically actuates power buttons, toggles, or appliances via an RC servo motor upon authenticated command.

> [!NOTE]
> **Custom Modified [`microlink`](https://github.com/CamM2325/microlink) Component:**
> This project uses a tailored, modified version of the [microlink](https://github.com/CamM2325/microlink) Tailscale client located in `components/microlink/`:
> * **High Availability Subnet Routing:** Custom route advertising logic (`ml_conf.advertise_routes = "192.168.1.0/24"`) integrated directly into the WireGuard manager.
> * **Linker-Wrapped Outbound Routing:** GNU linker intercept (`__wrap_ip4_route_src_hook` in `ml_wg_mgr.c`) directing CGNAT `100.64.0.0/10` return traffic back through the WireGuard interface (`s_wg_netif`).
> * **Inbound Packet Remapping:** Interface mapping in `wireguardif.c` ensuring packets addressed to the local IP (`192.168.1.50`) pass directly to lwIP rather than getting forwarded or dropped.
> * **Power & Signal Optimization:** Sets Wi-Fi RF output power to 15 dBm (`WIFI_POWER_15dBm`) for a +2 dBm link margin across walls while keeping chip temperature cool (~38–41°C) with modem sleep.
> * **L2 ARP Keepalive:** Integrated Gratuitous ARP (`CONFIG_LWIP_ESP_GRATUITOUS_ARP=y`) and periodic Gateway ARP probes ensuring consumer routers (such as BSNL ONT) never expire the sleeping ESP32 from their routing tables.

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                              ESP32-S3 SOC (80 MHz)                            │
├───────────────────────────────────────┬───────────────────────────────────────┤
│                CORE 0                 │                CORE 1                 │
├───────────────────────────────────────┼───────────────────────────────────────┤
│  • http_srv Task (Priority 4, 6 KB)   │  • arduino_loop Task (Priority 1, 8KB)│
│    - WebServer request dispatching    │    - Responsive 10ms loop scheduling  │
│    - Static cached /style.css & app.js│    - Instant async servo actuation    │
│    - Chunked HTTP streaming engine    │    - 60s NVS heartbeat persistence    │
│    - Zero-heap /api/live endpoint     │    - Wi-Fi failover & ARP keepalives  │
│  • ml_net_io Task (Priority 5, 6 KB)  │  • ml_wg_mgr Task (Priority 6, 8 KB)  │
│    - DERP TLS socket transport        │    - WireGuard ChaCha20-Poly1305      │
│    - Control plane HTTPS sessions     │    - Handshake timers & peer sessions │
└───────────────────────────────────────┴───────────────────────────────────────┘
```

### Core Design Principles:
1. **Zero Client Software:** Direct browser rendering or interactive Bash TUI streaming directly to `curl`. No mobile apps, proprietary cloud bridges, or daemon background services on client devices.
2. **Deterministic Response Time (< 5ms):** Asynchronous decoupling ensures that HTTP requests receive an instant response before the physical mechanical motion completes, preventing timeouts over high-latency cellular connections.
3. **Thermal & Energy Efficiency:** Operates at 80 MHz CPU clock with 15 dBm Wi-Fi output power and modem sleep, keeping chip temperatures low (~38–41°C).
4. **Flash Wear Minimization:** Ring buffers and throttled heartbeats guarantee decades of continuous 24/7 operation without flash exhaustion.
5. **Payload Optimization:** Browser caching for CSS (`/style.css`) and JS (`/app.js`) reduces subsequent page payloads to under 1 KB (95% cut), eliminating high-latency DERP round trips.

---

## 2. Board Compatibility, Minimum Requirements & Hardware Limitations

### Supported Hardware Matrix

| Board / SoC | Status | Flash Req. | PSRAM | Temp Sensor | Concurrency Model |
|---|---|---|---|---|---|
| **ESP32-S3 (N16R8)** | **Tested & Primary Target** | 16 MB Octal | 8 MB Octal | Native On-Die (`SOC_TEMP_SENSOR_SUPPORTED`) | Dual-core segregated |
| **ESP32-S3 (N8R2 / Generic)** | Fully Compatible | 8 MB Quad | 2 MB Quad | Native On-Die | Dual-core segregated |
| **ESP32 Classic (Dual-Core)** | Fully Compatible | 4 MB / 8 MB | Optional SPI | Graceful Fallback (`-` placeholder) | Dual-core segregated |
| **ESP32-C3 (RISC-V)** | Supported (with custom partitions) | 4 MB | None | Native On-Die | Single-core cooperative |
| **ESP32-S2** | Supported (with custom partitions) | 4 MB | Optional SPI | Native On-Die | Single-core cooperative |

### Hardware Limitations on Lower / Older Boards:

#### 1. On-Die Temperature Sensor Availability
* **ESP32-S3, ESP32-C3, ESP32-S2:** Contain dedicated on-chip temperature sensors natively supported by the ESP-IDF v5 `temperature_sensor` driver.
* **Classic ESP32 (ESP32-D0WDQ6, ESP32-WROOM-32):** The legacy internal temperature sensor was deprecated and removed in ESP-IDF v5.x due to severe calibration drift caused by Wi-Fi RF heat.
* *Firmware Handling:* Uses conditional compilation (`#if defined(SOC_TEMP_SENSOR_SUPPORTED) && SOC_TEMP_SENSOR_SUPPORTED`). On classic ESP32 chips, the temperature reading safely falls back to `-999.0f`, and the Web and cURL interfaces cleanly omit the field or show `-`.

#### 2. Physical PSRAM Detection & Allocation
* **Primary Target (DOIT N16R8):** Equipped with 8 MB of high-speed Octal SPI PSRAM (`MALLOC_CAP_SPIRAM`).
* **Lower Boards without PSRAM:** All memory allocations reside in internal SRAM (~360 KB available).
* *Firmware Handling:* The firmware directly queries native ESP-IDF capabilities (`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`). If PSRAM is 0, the PSRAM row is completely hidden from the Web UI and cURL dashboards.

#### 3. Flash Memory Size & Dual-OTA Partitions
* **16 MB / 8 MB Boards:** Accommodate the default `partitions.csv` with dual 4 MB application slots (`ota_0` and `ota_1`), enabling seamless wireless updates with automatic fallback.
* **4 MB Flash Boards:** The default partition table exceeds 4 MB. To flash on a 4 MB board, modify `partitions.csv` to reduce the app slots to ~1.8 MB each, or use a single factory slot without OTA capability.

#### 4. Single-Core SoCs (ESP32-C3 / ESP32-S2)
* On single-core chips, the WebServer (`http_srv`) and main loop cannot run on separate physical cores. FreeRTOS handles them via time-slicing on Core 0. The firmware's event-driven notification architecture (`ulTaskNotifyTake`) ensures the webserver yields CPU cycles smoothly.

---

## 3. Embedded Tailscale VPN & Subnet Routing Engine

The firmware embeds a full WireGuard and Tailscale coordination client via [microlink](https://github.com/CamM2325/microlink).

```
                    ┌──────────────────────────────────────────────┐
                    │            TAILSCALE MESH NETWORK            │
                    │         (MagicDNS: http://esp32/main)        │
                    └──────────────────────┬───────────────────────┘
                                           │
                ┌──────────────────────────┴──────────────────────────┐
                ▼                                                     ▼
   ┌─────────────────────────┐                           ┌─────────────────────────┐
   │  Primary Subnet Router  │                           │   ESP32-S3 SwitchBot    │
   │  (e.g. phone or server) │                           │   (Embedded microlink)  │
   │  Routes: 192.168.1.0/24 │                           │   Routes: 192.168.1.0/24│
   └────────────┬────────────┘                           └────────────┬────────────┘
                │                                                     │
                │     NORMAL OPERATION: Standby Mode                  │
                │     • Primary router handles all traffic            │
                │     • ESP32 Tailscale client OFF (~38°C)            │
                │                                                     │
                │     FAILOVER OPERATION: After 90s outage            │
                │     • ESP32 watchdog detects primary router offline │
                │     • ESP32 Tailscale starts up dynamically         │
                │                                                     │
                └──────────────────────────┬──────────────────────────┘
                                           │
                                           ▼
                                 LOCAL WI-FI NETWORK
                                  (192.168.1.0/24)
                                           │
                                 ESP32 Local Address:
                               192.168.1.50 (esp32.local)
```

### A. High Availability (HA) Subnet Routing & The `/24` CIDR Rule
In [`main/main.cpp`](main/main.cpp#L1888), the ESP32 advertises its local subnet:
```cpp
ml_conf.advertise_routes = "192.168.1.0/24";
```

#### Why it MUST match the `/24` CIDR of the Primary Subnet Router:
* When multiple nodes advertise the **exact same subnet CIDR** (`192.168.1.0/24`), Tailscale treats them as a **High Availability (HA) Failover Pair**. Only one router actively forwards packets; the other remains standby.
* **The `/32` Route Trap:** If the ESP32 were configured to advertise only its own host IP (`192.168.1.50/32`) while the primary router advertised `192.168.1.0/24`, standard IP routing dictates that the longest prefix match (`/32`) always wins. All traffic to `192.168.1.50` would be forced through the ESP32's onboard WireGuard stack, completely defeating the cold standby power savings.

---

### B. Subnet Collisions & Network Routing Concerns

> [!CAUTION]
> **Subnet Collision Danger (`192.168.1.0/24`):**
> `192.168.1.0/24` is the default factory subnet for over 80% of home routers, hotels, and public Wi-Fi hotspots.
> 
> If you connect your laptop or phone to an external Wi-Fi network that **also uses `192.168.1.0/24`**, an **IP Subnet Collision** occurs:
> 1. Your client operating system sees `192.168.1.50` as an address on the local physical adapter.
> 2. Packets are sent to the local hotel/cafe network instead of traversing the Tailscale tunnel.
> 3. Connection to your home SwitchBot fails!

#### Recommended Mitigations:
1. **Use MagicDNS:** Connect via `http://esp32/` or `http://esp32/main`. MagicDNS uses unique Carrier-Grade NAT (CGNAT) `100.x.y.z` addresses that never collide with local subnets.
2. **Re-number Local Home Subnet:** Set your home router to an uncommon subnet (e.g. `192.168.50.0/24`, `10.45.1.0/24`, or `172.24.1.0/24`). Update `local_IP`, `gateway`, and `advertise_routes` in `main.cpp` accordingly.

---

### C. Cloud API Watchdog Architecture
Running an active WireGuard node on an ESP32 increases power consumption and elevates chip temperatures by 8–12°C. To maintain peak efficiency, the firmware uses an intelligent cloud watchdog:
1. **HTTPS API Polling:** Every 45 seconds, the ESP32 queries the official Tailscale API (`api.tailscale.com`) via HTTPS using `esp_http_client` and the ESP-IDF root certificate bundle.
2. **Cold Standby:** As long as the primary router (e.g. `moto-g32`) reports `connected: true`, the ESP32 keeps its onboard Tailscale engine turned off (`microlink_stop()`).
3. **Automatic Failover:** If the primary router misses 2 consecutive checks (~90 seconds), the ESP32 automatically starts [microlink](https://github.com/CamM2325/microlink) and assumes active routing.
4. **Self-Healing Recovery (How Reconnection is Detected):** The watchdog queries the official Tailscale REST API (`api.tailscale.com`) every 45 seconds over HTTPS and inspects the `connectedToControl` boolean field for the primary subnet router device. As soon as the primary router reconnects to the Tailscale control plane (`connectedToControl: true`), the ESP32 detects this, automatically calls `stopTailscale()`, relinquishes active subnet routing, and returns to Cold Standby (~38–41°C).

---

### D. 100% Local Mode (`FULLY_LOCAL_MODE`)
If you do not use Tailscale, or are configuring the device strictly for LAN operation:
* In `main/secrets.h`, configure `#define FULLY_LOCAL_MODE 1` (or answer `Yes` in `setup_secrets.py`).
* **System Effect:** The entire Microlink and WireGuard stack is completely bypassed at runtime.
* **UI Adaptation:** All Tailscale status pills, IP rows, session duration cards, and cURL Tailscale sections are **completely stripped** from the Web UI and cURL dashboards. The device presents a pure, minimal LAN switch interface.
* **Benefits:** Reclaims ~180 KB of internal RAM, lowers idle temperature to ~38°C, and eliminates any external cloud/API polling.

---

### E. Multi-Network Wi-Fi Failover (Up to 6 Networks)
The firmware supports resilient multi-network failover across up to 6 configured Wi-Fi networks:
* **Slots:** `WIFI_SSID_1` / `WIFI_PASSWORD_1` is the primary home network. Slots `2` through `6` are optional fallback networks (e.g., phone mobile hotspot, secondary router, guest network).
* **Automatic Reconnect & Failover:** Monitored continuously by `checkWifiReconnectIfNeeded()`. If Wi-Fi is lost for > 15 seconds, the ESP32 automatically disconnects and attempts connection to the next configured fallback network in sequence.
* **NVS Forensic Attribution:** The active network slot index (`wifiIdx`) is persistently updated in the active `BootLog` entry in NVS flash. When reviewing reboot or crash history in `/debug` or cURL, each boot record explicitly displays the connected SSID (omitted if only 1 network is configured).

---

## 4. FreeRTOS Concurrency & Low-Power Architecture

### FreeRTOS Task Distribution

| Task Name | Core | Priority | Stack Size | Function & Responsibility |
|---|---|---|---|---|
| `http_srv` | **Core 0** | 4 | 6,144 bytes | Accepts incoming TCP connections, parses HTTP, serves chunked responses with 2ms yield. |
| `ts_watchdog`| **Core 0** | 1 | 8,192 bytes | Background watchdog querying `api.tailscale.com` over HTTPS every 45s; keeps TLS calls completely off Core 1. |
| `ml_net_io` | **Core 0** | 5 | 6,144 bytes | Socket I/O and TLS transport for DERP and control plane (bypassed in Fully Local mode). |
| `ml_wg_mgr` | **Core 1** | 6 | 8,192 bytes | WireGuard peer management, handshake timers, and cryptography (bypassed in Fully Local mode). |
| `arduino_loop`| **Core 1** | 1 | 8,192 bytes | Dedicated instantaneous servo actuation, NVS heartbeats, OTA updates, 10ms responsive sleep. |

### Event-Driven Task Scheduling & Responsive Sleep
Traditional firmware models poll the Arduino `loop()` continuously with `vTaskDelay(20)`. In this firmware:
* `loop()` yields with `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10))`.
* When an HTTP actuation request arrives, `handleRoot()` fires `xTaskNotifyGive(loopTaskHandle)`.
* Core 1 wakes up **instantly (< 1 ms)** to actuate the servo. Because the blocking Tailscale API watchdog was decoupled into its own Core 0 background task (`ts_watchdog`), Core 1 is **never blocked** by TLS handshakes or network queries, guaranteeing zero-latency motor actuation on every trigger.
* During the 10 ms idle interval, `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` and `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y` allow the Xtensa core to enter the hardware `waiti 0` low-power sleep state, preserving low temperature (~39–41°C) while maintaining crisp sub-15ms response latency.
* **L2 ARP Keepalive:** `sendLanArpKeepaliveIfNeeded()` fires every 45s, broadcasting a Gratuitous ARP frame (`etharp_gratuitous`) and probing the gateway (`etharp_request`) to keep consumer router ARP tables permanently warm during Wi-Fi modem sleep.

---

## 5. Memory Pipeline, Chunked HTTP & Static Asset Caching Engine

### 1. Zero-Heap Live Telemetry (`/api/live`)
* Web clients poll `/api/live` every 3.5 seconds.
* To prevent heap fragmentation, the handler uses stack-allocated buffers (`char json[896]`) formatted directly with `snprintf()`.
* **Zero heap allocations** occur during continuous telemetry polling.

### 2. Static Asset Separation & Browser Caching (`/style.css` & `/app.js`)
* **Shared CSS:** `COMMON_CSS` (~10.9 KB) is served at `/style.css` with `Cache-Control: public, max-age=604800, immutable`.
* **Shared JS:** `APP_JS` (~3.8 KB) bundles client haptics, clipboard copies, collapsible log accordions, and background live polling into `/app.js` with `Cache-Control: public, max-age=604800, immutable`.
* **Zero-Script HTML:** Completely eliminates the 1.2 KB inline `POLL_SCRIPT` from page responses, slashing HTML transfer sizes and eliminating `t=0ms` request storms. Polling begins seamlessly 3.5s after initial page render.
* **Payload Impact:**
  * HTML for `/main` shrank from ~16 KB down to **788 bytes** (**95% payload reduction**).
  * HTML for `/info` shrank from ~23 KB down to **5.3 KB**.
  * Browsers download CSS & JS once and cache them locally for 7 days. Subsequent page navigation is near-instantaneous, eliminating high-latency DERP round trips over Tailscale.
  * Slashes active Wi-Fi radio transmission time by **~80%**, keeping the RF power amplifier cool.

### 3. Zero-Allocation Chunked HTTP Web Streaming (`sendWrappedPageStream`)
* In traditional ESP32 implementations, full HTML pages are concatenated into monolithic `String` buffers (`String.reserve(35000)`), causing 35–52 KB heap allocation spikes.
* **Stream Implementation:** Defined in [`main/web_pages.h`](main/web_pages.h). Web pages (`/info`, `/debug`, `/calibrate`, `/`, `/main`) are streamed in compact chunks via HTTP/1.1 chunked transfer encoding (`server.sendContent()` and `server.sendContent_P()`).
* `<head>` tags, stylesheets, and wrappers are streamed with zero dynamic heap buffer allocation.
* cURL output in `handleDebug()` streams section-by-section directly, eliminating 4.8 KB dynamic buffer allocations.
* Log timestamp formatting uses stack buffers (`formatTimestampBuf()`), eliminating dozens of heap malloc/free cycles per request.

---

## 6. NVS Wear-Leveling Forensics & Activity Ring Buffers

To prevent flash memory wear while maintaining complete system observability, the firmware uses rolling ring buffers across individual NVS keys:

```
NVS "esp_log" Namespace:
┌─────┬─────┬─────┬─────┬─────┬┄┄┄┄┄┬──────┐
│ b0  │ b1  │ b2  │ b3  │ b4  │     │ b49  │  (50 Boot History Slots, with wifiIdx)
└─────┴─────┴─────┴─────┴─────┴┄┄┄┄┄┴──────┘

NVS "servo_log" Namespace:
┌─────┬─────┬─────┬─────┬─────┬┄┄┄┄┄┬──────┐
│ s0  │ s1  │ s2  │ s3  │ s4  │     │ s19  │  (20 Servo Trigger Slots)
└─────┴─────┴─────┴─────┴─────┴┄┄┄┄┄┴──────┘

NVS "ts_log" Namespace:
┌─────┬─────┬─────┬─────┬─────┬┄┄┄┄┄┬──────┐
│ t0  │ t1  │ t2  │ t3  │ t4  │     │ t19  │  (20 Tailscale Session Slots)
└─────┴─────┴─────┴─────┴─────┴┄┄┄┄┄┴──────┘
```

### Reset Forensics & Downtime Estimation
* On boot, `esp_reset_reason()` decodes the hardware reset cause:
  * `ESP_RST_POWERON`: Cold plug or external power cut.
  * `ESP_RST_SW`: Software reset (e.g. OTA update or manual `/reboot`).
  * `ESP_RST_PANIC`: CPU exception or crash.
  * `ESP_RST_BROWNOUT`: Supply voltage dipped below operational threshold.
* **Downtime Calculation:** The device periodically writes a Unix timestamp heartbeat (`last_alive`) every 60 seconds. On the subsequent boot, the downtime duration is calculated as:
  $$\text{Downtime} = \text{BootEpoch} - \text{LastAliveEpoch}$$
* Across soft reboots, session state is maintained across boot boundaries using non-initialized RTC memory (`rtc_ts_was_active`, `rtc_ts_duration_s`).

---

## 7. Interactive Servo Calibration Engine

Defined in [`main/calibration.h`](main/calibration.h):

```
             90° (Center)
                  │
          ┌───────┴───────┐
          │               │
     0° ──┤   REST DIAL   ├── 180°
          │ (Default 90°) │
          └───────┬───────┘
                  │
              [GAP 40°]   ◄── Anti-Flip Barrier Prevents 360° Wrap
                  │
          ┌───────┴───────┐
          │               │
     0° ──┤  PRESS DIAL   ├── 180°
          │(Default 100°) │
          └───────┬───────┘
                  │
             HOLD DURATION
              (50-3000ms)
```

### Safety Features:
* **Bottom-Gap Barrier:** Rotary dials enforce a 40° deadzone at the bottom of the circle, preventing rotational phase wraps between 0° and 180°.
* **Direct Numeric Input & Touch Isolation:** Tapping the center angle badges isolates touch/pointer events from the rotary dial gesture handlers, auto-selects the text, and opens the numeric keypad for exact typed entry without accidental motor movement.
* **Live Position Preview:** Rotating the Rest dial sends debounced `POST /api/calibrate/move` requests, moving the servo arm live so you can visually verify alignment without saving.
* **Press & Hold Validation:** Pressing the hold button (`POST /api/calibrate/hold?state=1`) drives the servo to the press angle and holds it until release (`state=0`), verifying mechanical clearance.
* **NVS Persistence:** Saved to `servo_cal` namespace (`rest_angle`, `press_angle`, `press_dur`).

---

## 8. Interactive Terminal cURL Engine (`curl_scripts.h`)

When requested by `curl`, `/main` streams an interactive, full-screen Bash script that executes directly in the user's terminal:

* **Direct `/dev/tty` Redirection:** Because piped shell scripts (`curl | bash`) consume standard input from the pipe, all keystroke reads (`read -n 1 -s` and password prompts) are redirected from `/dev/tty`. This ensures reliable terminal interaction without unexpected EOF terminations.
* **Dedicated Calibration CLI (`/calibrate`):** Streams a self-contained guided wizard with an upfront management menu (`[1] Start Guided Calibration`, `[T] Test Current Tap`, `[R] Reset Calibration Data`, `[X] Exit`), allowing immediate testing or resetting to factory defaults without completing all calibration steps.
* **No Client Dependencies:** Functions purely on standard POSIX bash utilities (`curl`, `grep`, `cut`, `sleep`). Works natively on macOS, Linux, Windows (WSL/Git Bash), and Android (Termux).

---

## 9. Over-The-Air (OTA) Security Architecture

* **Locked by Default:** Port 3232 is completely closed at boot.
* **Secret Key Gated:** If `OTA_KEY` is specified in `secrets.h`, unlocking requires submitting the key via `/ota/enable` or typing it in terminal cURL. If `OTA_KEY` is left empty (`""`), the device operates without an OTA password, allowing the update portal to unlock immediately with a single click.
* **Themed Verification UI:** Submitting an invalid key renders an error card styled consistently with the project theme and redirects back to `/debug`.
* **Zero-Auth Upload Window:** When unlocked, the port accepts uploads directly from standard PlatformIO, Arduino IDE, or `espota.py` without requiring upload flags.
* **Self-Closing Timer:** Port 3232 automatically closes after 10 minutes (`OTA_AUTO_TIMEOUT_MS = 600,000 ms`), leaving no permanent open ports.

### Flashing Over Multi-Network & VPN Hosts (`-I <lan_ip>`)

When invoking `espota.py` from a host development machine connected to multiple network adapters (e.g., physical local Wi-Fi `192.168.1.2`, alongside Tailscale `100.x.x.x`, Docker bridge networks, or a corporate VPN tunnel), OTA uploads may fail:

```text
Uploading...................
[ERROR]: Error Uploading: [Errno 32] Broken pipe
```

#### Why This Happens:
1. **Reverse Connection Handshake:** The Arduino/ESP32 OTA protocol does not receive the firmware stream purely over the outbound command socket. Instead:
   - `espota.py` sends a UDP invitation packet to the ESP32 (port 3232) announcing that an update is pending. This packet includes the IP address and port that `espota.py` expects the ESP32 to reach back to.
   - The ESP32 then initiates an inbound **reverse TCP connection** back to the host machine to pull the binary stream.
2. **Interface Ambiguity (`0.0.0.0`):** By default, `espota.py` binds its local server to `0.0.0.0` and attempts to guess the host machine's IP. When virtual interfaces (Tailscale `100.x.x.x`, VPN tun/tap, Docker `172.17.x.x`) are active, `espota.py` frequently selects the virtual adapter IP instead of the physical local LAN interface.
3. **Unreachable Routing:** Because the ESP32 is on the physical local Wi-Fi (`192.168.1.0/24`), it cannot route to the host machine's virtual adapter IP without an established route/tunnel on the microcontroller. The reverse connection times out, throwing an immediate `[Errno 32] Broken pipe`.

#### The Fix:
Specify the `-I` (capital `i`) parameter with your development computer's local Wi-Fi / Ethernet LAN IP:
```bash
python3 components/arduino/tools/espota.py -i 192.168.1.50 -p 3232 -I 192.168.1.2 -f build/ESP32-SwitchBot.bin
```
Passing `-I 192.168.1.2` binds `espota.py` specifically to your physical local Wi-Fi interface and instructs the ESP32: *"Connect back directly to physical IP `192.168.1.2`."* This guarantees a direct, local transfer that completes seamlessly without timeouts or broken pipes.

---

## 10. In-Code Variables, Customization & Network Gotchas

The following table documents all user-configurable parameters in [`main/main.cpp`](main/main.cpp) and [`main/secrets.h`](main/secrets.h):

| Variable | File & Line | Default Value | Description, Considerations & Gotchas |
|---|---|---|---|
| `FULLY_LOCAL_MODE` | `secrets.h` / `main.cpp:44` | `0` (or `1`) | Operation mode. Set to `1` to run purely on local LAN / subnet router, stripping all Microlink & Tailscale code and UI elements. |
| `TIMEZONE_OFFSET` | `main.cpp:51` | `"+05:30"` | Timezone offset string (e.g. `"+05:30"`, `"0530"`, `"+0630"`, `"-05:00"`, `"-0500"`, `"0"`). Used by NTP clock sync. |
| `WIFI_SSID_1`..`6` | `secrets.h` / `main.cpp:59-89` | `""` | Primary (`1`) and up to 5 optional fallback Wi-Fi network SSIDs for automatic failover. |
| `WIFI_PASSWORD_1`..`6` | `secrets.h` / `main.cpp:59-89` | `""` | WPA/WPA2 passwords corresponding to each configured Wi-Fi network slot. |
| `local_IP` | `main.cpp:108` | `192.168.1.50` | Static IP of the ESP32. Must be outside your router's DHCP pool or assigned as a static DHCP reservation to avoid IP conflicts. |
| `gateway` | `main.cpp:109` | `192.168.1.1` | Local network router gateway. Must match your router's IP for NTP, internet API access, and ARP probes. |
| `subnet` | `main.cpp:110` | `255.255.255.0` | Subnet mask (`/24`). Must match your local network configuration. |
| `tailscaleAdvertiseRoute`| `main.cpp:116`| `"192.168.1.0/24"`| Tailscale advertised subnet CIDR. Must match your local network subnet for HA failover. |
| `servoPin` | `main.cpp:123` | `1` | PWM signal pin. Must use a PWM-capable GPIO that is not a strapping pin. |
| `PRESS_COOLDOWN_MS`| `main.cpp:186` | `2000` (2s) | Cooldown period between successive button actuations to protect the motor. |
| `HEARTBEAT_INTERVAL_MS`| `main.cpp:179`| `60000` (60s) | NVS timestamp write interval. 60s cuts flash writes while maintaining 1-minute downtime estimation (safe for 12+ years of continuous flash wear). |
| `OTA_AUTO_TIMEOUT_MS` | `main.cpp:176`| `600000` (10m)| Inactivity timeout for the OTA listener before automatically locking port 3232. |
| `BOARD_NAME` | `main.cpp:56` | `""` | Optional manual hardware model override. Leave empty for auto-detection (`ESP32-S3-N16R8`). |
| `TAILSCALE_KEY` | `secrets.h:8` | `""` | Tailscale auth key. Leave empty or use `FULLY_LOCAL_MODE 1` for 100% local/offline Wi-Fi operation. |
| `TAILSCALE_API_KEY` | `secrets.h:14`| `""` | Tailscale read-only API key for the subnet failover watchdog. |
| `TAILSCALE_SUBNET_DEVICE_ID`| `secrets.h:17`| `""` | Primary subnet router device ID or hostname (e.g. `"moto-g32"`). |
| `OTA_KEY` | `secrets.h:20` | `""` | Passphrase to unlock OTA flashing. Leave empty to allow single-click unlock without a password. |

---

**Current Version:** `v1.2`

