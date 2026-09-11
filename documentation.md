# ESP32-SwitchBot Dashboard — Firmware & Architecture Documentation

---

## Overview

This firmware transforms an **ESP32-S3-WROOM-N16R8** into a dual-interface, encrypted smart switch actuator. It mechanically actuates a button using a servo motor upon authenticated command. The device features two concurrent interfaces — a responsive cyber-dark web dashboard and an interactive terminal session via `curl` — and is accessible globally through an embedded **Tailscale VPN** client (`microlink`).

**Primary Capability:** Actuate a physical power button remotely from a web browser, a terminal, or any device on your Tailnet, complete with live hardware telemetry, crash forensics, downtime tracking, and gated OTA updates.

---

## Project Structure

```
ESP32-SwitchBot/
├── CMakeLists.txt            — Root CMake project file
├── partitions.csv            — Custom flash partition layout (dual 4MB OTA slots)
├── sdkconfig.defaults        — ESP-IDF build configuration (Octal PSRAM, ChaCha20-Poly1305)
├── dependencies.lock         — Locked managed component versions
├── documentation.md          — This architecture & firmware specification
├── README.md                 — Repository overview and quickstart guide
├── main/
│   ├── CMakeLists.txt        — IDF component registration & linker wrapping
│   ├── main.cpp              — Primary firmware (application logic, web routes, TUI)
│   ├── secrets.h             — Wi-Fi credentials & Tailscale auth key (gitignored)
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

Configuration values are defined in [`main/main.cpp`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/main.cpp) and [`main/secrets.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/secrets.h).

| Parameter | Configured Value | Description |
|---|---|---|
| **MCU** | ESP32-S3-WROOM-N16R8 | 16 MB Flash, 8 MB Octal PSRAM |
| **CPU Clock** | 240 MHz | Full dual-core speed for WireGuard ChaCha20-Poly1305 crypto |
| **Wi-Fi SSID** | Configured in `secrets.h` | 2.4 GHz 802.11 b/g/n station mode |
| **Static IP** | `192.168.1.50` | Fixed local IPv4 address |
| **Gateway / DNS** | `192.168.1.1` | Local network router address |
| **Subnet Mask** | `255.255.255.0` | Local `/24` subnet mask |
| **Local Hostname** | `esp32.local` | mDNS identifier for local network discovery |
| **Tailscale Device Name** | `esp32` | MagicDNS host name (`http://esp32/`) |
| **Subnet Route Advertised**| `192.168.1.50/32` | Advertised by ESP32 to Tailscale control plane |
| **Servo GPIO Pin** | GPIO 1 | PWM control line connected to servo signal wire |
| **Servo PWM Range** | 500 – 2400 µs | Pulse width limits for 50 Hz PWM period |
| **Servo Rest Angle** | 180° | Idle position (hovering above button) |
| **Servo Press Angle** | 156° | Actuation position (24° of physical travel) |
| **Wi-Fi Modem Sleep** | Disabled (`WiFi.setSleep(false)`) | Prevents radio latency; instant packet reception |
| **NTP Timezone Offset** | +5:30 IST (19800 sec) | India Standard Time (no daylight savings) |
| **HTTP Port** | 80 | Standard HTTP listening port |

---

## Flash Partition Layout

Defined in [`partitions.csv`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/partitions.csv):

| Partition | Type | SubType | Size | Purpose |
|---|---|---|---|---|
| `nvs` | data | nvs | 20 KB | Key-value store (crash logs, boot counters, heartbeats) |
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

## Core Components

### 1. Microlink — Embedded Tailscale VPN

`microlink` implements a full userspace Tailscale node on the ESP32:
* **WireGuard Cryptography:** ChaCha20-Poly1305 authenticated encryption and Curve25519 ECDH key exchange using hardware-accelerated mbedTLS.
* **DERP Relay Client:** Connects to Tailscale's global HTTPS DERP relays (TCP port 443) using HTTP/2, providing connectivity behind restrictive NATs and symmetric firewalls.
* **STUN & DISCO:** Periodic STUN endpoint probing and Tailscale DISCO message exchange to detect and switch to direct UDP paths when possible.
* **Subnet Router Advertisement:** Advertises `192.168.1.50/32` as a primary route so Tailscale peers route directly to the device.

### 2. Multi-Core FreeRTOS Concurrency

| Task | Core | Priority | Stack | Function |
|---|---|---|---|---|
| `http_srv` | **Core 0** | 4 | 8 KB | Runs `server.handleClient()` continuously without blocking Arduino `loop()` |
| `ml_net_io` | **Core 0** | 5 | 6 KB | Socket I/O and TLS transport for DERP and control plane |
| `ml_wg_mgr` | **Core 1** | 6 | 8 KB | WireGuard peer management, handshake timers, and cryptography |
| `arduino_loop`| **Core 1** | 1 | 8 KB | Servo actuation, NTP sync, heartbeat NVS writes, and OTA handling |

### 3. Asynchronous Servo Actuation

To prevent HTTP request timeouts over remote cellular networks, servo actuation is completely decoupled from the HTTP response:
1. `handleRoot()` receives the HTTP request.
2. Checks cooldown (`PRESS_COOLDOWN_MS = 2000`).
3. Sets atomic flag `pendingPress = true`.
4. Immediately sends `HTTP 200 OK` (with `Connection: close`) in `< 5ms`.
5. `loop()` on Core 1 detects `pendingPress`, attaches the servo, moves to `156°`, holds for 400 ms, returns to `180°`, settles for 300 ms, and detaches the PWM pin.

### 4. Ring-Buffered NVS Crash Forensics

Replaces monolithic flash writes with a rolling ring buffer across individual keys:
* **Slot Keys:** `b0` through `b49` (each storing a compact 5-byte `BootLog` struct: 4-byte epoch + 1-byte reset reason code).
* **Index Pointer:** `w_idx` points to the next write slot.
* **Boot Counter:** `boot_cnt` records cumulative boots.
* **Flash Wear Reduction:** Only 2 integers and 1 five-byte slot are written per boot, reducing flash write wear by ~98%.

### 5. Downtime & Outage Tracking

* Every 5 minutes, `heartbeatIfNeeded()` writes the current Unix timestamp to NVS key `last_alive`.
* Upon the next reboot and NTP synchronization, the firmware calculates:
  $$\text{lastOffDuration} = \text{thisBootEpoch} - \text{lastAliveEpoch}$$
* This allows accurate forensic identification of power outages, breaker trips, or offline duration.

### 6. Gated On-Demand OTA

* `ArduinoOTA` is **disabled by default** at boot.
* **Activation:** Triggered via `POST /ota/enable` with body `key=OTA_KEY` (or via option `[6]` in the terminal menu).
* **Auto-Disable:** The OTA listener automatically de-initializes and closes port 3232 after 10 minutes (`OTA_AUTO_TIMEOUT_MS = 600,000 ms`).
* **Manual Disable:** Triggered anytime via `POST /ota/disable`.

---

## API Endpoints Reference

All endpoints return explicit `Connection: close` headers to immediately release lwIP socket descriptors.

| Endpoint | Method | Request Format | Response Content | Description |
|---|---|---|---|---|
| `/` | GET | None | HTML / Text | Triggers the physical button press sequence. Enforces a 2-second cooldown. |
| `/main` | GET | `curl` or Browser | HTML / Bash Script | Serves the web dashboard or streams the self-executing terminal Bash TUI. |
| `/info` | GET | None | HTML / Text Table | Returns hardware specs (Uptime, Temperature, RAM, Flash, PSRAM, Clock, Wi-Fi). |
| `/debug` | GET | None | HTML / Text | Displays crash logs, reset cause, downtime, and VPN connection state. |
| `/api/live` | GET | None | JSON | Live stream: `{"u":"...","t":51.2,"ru":202,"rt":362,"c":240,"p":0.41}`. |
| `/ota/enable` | POST | Form / Body `key=...` | HTML / Text | Unlocks the OTA port (3232) for a 10-minute auto-closing window. |
| `/ota/disable`| POST | None | HTML / Text | Immediately shuts down the OTA listener. |
| `/clear-logs` | POST | None | HTML / Text | Clears NVS crash history and resets flash timers. |
| `/reboot` | POST | None | HTML / Text | Gracefully restarts the ESP32 after a 500 ms socket-flush delay. |

---

## Terminal Command Center (`curl`)

When queried with `curl`, the `/main` endpoint generates an interactive, full-screen TUI that listens for single keystrokes (`read -n 1 -s`):

```bash
curl -s http://esp32/main | bash
```

```
=== SwitchBot Dashboard ===
 [1] Trigger Power Button
 [2] Device Info (Live)
 [3] Crash Logs & Debug
 [4] Reboot ESP32
 [5] Clear Logs
 [6] Enable OTA (10 min)
 [X] Exit
```

Sub-menus auto-refresh every second and accept single-key commands without requiring Enter:
* `B`: Back to Menu
* `R`: Reboot ESP32
* `C`: Clear Logs (Debug menu)
* `O`: Enable OTA (Debug menu)
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
