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
> **Custom Modified `microlink` Component:**
> This project uses a tailored, modified version of the `microlink` Tailscale client located in `components/microlink/`:
> * **High Availability Subnet Routing:** Custom route advertising logic (`ml_conf.advertise_routes = "192.168.1.0/24"`) integrated directly into the WireGuard manager.
> * **Linker-Wrapped Outbound Routing:** GNU linker intercept (`__wrap_ip4_route_src_hook` in `ml_wg_mgr.c`) directing CGNAT `100.64.0.0/10` return traffic back through the WireGuard interface (`s_wg_netif`).
> * **Inbound Packet Remapping:** Interface mapping in `wireguardif.c` ensuring packets addressed to the local IP (`192.168.1.50`) pass directly to lwIP rather than getting forwarded or dropped.
> * **Power Clamping:** Clamps Wi-Fi RF output power to 13 dBm (`ml_conf.wifi_tx_power_dbm = 13`) and reduces maximum peers to 8 to minimize memory and thermal footprint.

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                              ESP32-S3 SOC (80 MHz)                            │
├───────────────────────────────────────┬───────────────────────────────────────┤
│                CORE 0                 │                CORE 1                 │
├───────────────────────────────────────┼───────────────────────────────────────┤
│  • http_srv Task (Priority 4, 6 KB)   │  • arduino_loop Task (Priority 1, 8KB)│
│    - WebServer request dispatching    │    - Event-driven ulTaskNotifyTake()  │
│    - Chunked HTTP streaming engine    │    - Instant async servo actuation    │
│    - Zero-heap /api/live endpoint     │    - 60s NVS heartbeat persistence    │
│  • ml_net_io Task (Priority 5, 6 KB)  │  • ml_wg_mgr Task (Priority 6, 8 KB)  │
│    - DERP TLS socket transport        │    - WireGuard ChaCha20-Poly1305      │
│    - Control plane HTTPS sessions     │    - Handshake timers & peer sessions │
└───────────────────────────────────────┴───────────────────────────────────────┘
```

### Core Design Principles:
1. **Zero Client Software:** Direct browser rendering or interactive Bash TUI streaming directly to `curl`. No mobile apps, proprietary cloud bridges, or daemon background services on client devices.
2. **Deterministic Response Time (< 5ms):** Asynchronous decoupling ensures that HTTP requests receive an instant response before the physical mechanical motion completes, preventing timeouts over high-latency cellular connections.
3. **Thermal & Energy Efficiency:** Operates at 80 MHz CPU clock with 13 dBm Wi-Fi output power and modem sleep, keeping chip temperatures low (~38–41°C).
4. **Flash Wear Minimization:** Ring buffers and throttled heartbeats guarantee decades of continuous 24/7 operation without flash exhaustion.

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

The firmware embeds a full WireGuard and Tailscale coordination client via `microlink`.

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
3. **Automatic Failover:** If the primary router misses 2 consecutive checks (~90 seconds), the ESP32 automatically starts `microlink` and assumes active routing.
4. **Self-Healing Recovery (How Reconnection is Detected):** The watchdog queries the official Tailscale REST API (`api.tailscale.com`) every 45 seconds over HTTPS and inspects the `connectedToControl` boolean field for the primary subnet router device. As soon as the primary router reconnects to the Tailscale control plane (`connectedToControl: true`), the ESP32 detects this, automatically calls `stopTailscale()`, relinquishes active subnet routing, and returns to Cold Standby (~38–41°C).

---

### D. 100% Local / Zero-Key Operation
If you do not use Tailscale:
* Set `TAILSCALE_KEY` and `TAILSCALE_API_KEY` to `""` in `secrets.h`.
* **System Effect:** Tailscale initialization is completely skipped.
* **Benefits:** Reclaims ~180 KB of internal RAM, reduces operating temperature to ~38°C, and runs completely offline with zero external network dependencies.

---

## 4. FreeRTOS Concurrency & Low-Power Architecture

### FreeRTOS Task Distribution

| Task Name | Core | Priority | Stack Size | Function & Responsibility |
|---|---|---|---|---|
| `http_srv` | **Core 0** | 4 | 6,144 bytes | Accepts incoming TCP connections, parses HTTP, serves chunked responses. |
| `ml_net_io` | **Core 0** | 5 | 6,144 bytes | Socket I/O and TLS transport for DERP and control plane. |
| `ml_wg_mgr` | **Core 1** | 6 | 8,192 bytes | WireGuard peer management, handshake timers, and cryptography. |
| `arduino_loop`| **Core 1** | 1 | 8,192 bytes | Event-driven servo actuation, NVS heartbeats, OTA updates, watchdog checks. |

### Event-Driven Task Scheduling (`ulTaskNotifyTake`)
Traditional firmware models poll the Arduino `loop()` continuously with `vTaskDelay(20)`. In this firmware:
* `loop()` blocks on `ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(100))`.
* When an HTTP request arrives, `handleRoot()` fires `xTaskNotifyGive(loopTaskHandle)`.
* Core 1 wakes up **instantly (< 1 ms)** to actuate the servo.
* When idle, the task sleeps for 100 ms instead of 20 ms, **saving 80% of Core 1 idle wakeups**.
* Combined with `CONFIG_FREERTOS_USE_TICKLESS_IDLE=y` and `CONFIG_ESP_WIFI_SLP_IRAM_OPT=y`, the chip achieves maximum power and thermal efficiency.

---

## 5. Memory Pipeline & Chunked HTTP Streaming Engine

### 1. Zero-Heap Live Telemetry (`/api/live`)
* Web clients poll `/api/live` every 3.5 seconds.
* To prevent heap fragmentation, the handler uses stack-allocated buffers (`char json[700]`) formatted directly with `snprintf()`.
* **Zero heap allocations** occur during continuous telemetry polling.

### 2. Chunked HTTP Web Streaming (`sendWrappedPageStream`)
* In traditional ESP32 implementations, full HTML pages are concatenated into monolithic `String` buffers (`String.reserve(35000)`), causing 35–52 KB heap allocation spikes.
* **Stream Implementation:** Defined in [`main/web_pages.h`](main/web_pages.h). Web pages (`/info`, `/debug`, `/calibrate`, `/`, `/main`) are streamed in compact chunks via HTTP/1.1 chunked transfer encoding (`server.sendContent()`).
* **Result:** Peak heap spikes during page loads drop from **52 KB to ZERO**. The browser begins parsing CSS and HTML immediately as chunks arrive.

---

## 6. NVS Wear-Leveling Forensics & Activity Ring Buffers

To prevent flash memory wear while maintaining complete system observability, the firmware uses rolling ring buffers across individual NVS keys:

```
NVS "esp_log" Namespace:
┌─────┬─────┬─────┬─────┬─────┬┄┄┄┄┄┬──────┐
│ b0  │ b1  │ b2  │ b3  │ b4  │     │ b49  │  (50 Boot History Slots)
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
        -180° / +180°
              │
      ┌───────┴───────┐
      │               │
-90° ─┤   REST DIAL   ├─ +90°
      │  (Default 0°) │
      └───────┬───────┘
              │
          [GAP 40°]   ◄── Anti-Flip Barrier Prevents 360° Wrap
              │
      ┌───────┴───────┐
      │               │
-90° ─┤  PRESS DIAL   ├─ +90°
      │ (Default 10°) │
      └───────┬───────┘
              │
         HOLD DURATION
           (50-3000ms)
```

### Safety Features:
* **Bottom-Gap Barrier:** Rotary dials enforce a 40° deadzone at the bottom of the circle, preventing rotational phase wraps between -180° and +180°.
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

---

## 10. In-Code Variables, Customization & Network Gotchas

The following table documents all user-configurable parameters in [`main/main.cpp`](main/main.cpp) and [`main/secrets.h`](main/secrets.h):

| Variable | File & Line | Default Value | Description, Considerations & Gotchas |
|---|---|---|---|
| `local_IP` | `main.cpp:61` | `192.168.1.50` | Static IP of the ESP32. Must be outside your router's DHCP pool or assigned as a static DHCP reservation to avoid IP conflicts. |
| `gateway` | `main.cpp:62` | `192.168.1.1` | Local network router gateway. Must match your router's IP for NTP and internet API access. |
| `subnet` | `main.cpp:63` | `255.255.255.0` | Subnet mask (`/24`). Must match your local network configuration. |
| `SERVO_PIN` | `main.cpp:68` | `1` | PWM signal pin. Must use a PWM-capable GPIO that is not a strapping pin. |
| `PRESS_COOLDOWN_MS`| `main.cpp:71` | `2000` (2s) | Cooldown period between successive button actuations to protect the motor. |
| `HEARTBEAT_INTERVAL_MS`| `main.cpp:89`| `60000` (60s) | NVS timestamp write interval. 60s cuts flash writes by 50% vs 30s while maintaining precise downtime estimation. |
| `OTA_AUTO_TIMEOUT_MS` | `main.cpp:120`| `600000` (10m)| Inactivity timeout for the OTA listener before automatically locking port 3232. |
| `advertise_routes` | `main.cpp:1888`| `"192.168.1.0/24"`| Tailscale advertised subnet CIDR. Must match your local network subnet for HA failover. |
| `TAILSCALE_KEY` | `secrets.h:8` | `""` | Tailscale auth key. Leave empty for 100% local/offline Wi-Fi operation. |
| `TAILSCALE_API_KEY` | `secrets.h:14`| `""` | Tailscale read-only API key for the subnet failover watchdog. |
| `OTA_KEY` | `secrets.h:20` | `""` | Passphrase to unlock OTA flashing. Leave empty to allow single-click unlock without a password. |
