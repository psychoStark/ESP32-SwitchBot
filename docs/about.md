# About ESP32-SwitchBot

**ESP32-SwitchBot** is an open-source, industrial-grade firmware designed to turn an Espressif ESP32 microcontroller into an encrypted, remotely-accessible switch actuator. It mechanically actuates physical buttons (power switches, lights, appliances, or PC power buttons) on command.

---

## 🚀 Version 1.2 Release Notes (Current)

Version **v1.2** builds directly upon the v1.0 production baseline, focusing on zero-truncation streaming reliability, multi-network Wi-Fi failover, dynamic hardware auto-detection, and motor safety guards.

### 🌟 What's New in v1.2

#### ⚡ Actuation & Reliability
* **FreeRTOS Priority Elevation:** Elevates servo actuation task priority (Priority 5) during presses to guarantee zero-jitter hardware timing over network tasks.
* **Extended Hold Safety Watchdog (20s):** Increased manual calibration hold limit (`MAX_HOLD_DURATION_MS`) from 10s to 20s with automatic spring-back to protect the servo motor.
* **4-Second Press Cooldown & Click Debounce:** Prevents rapid re-triggers, rejects speculative browser retries, and disables UI buttons immediately upon tap.
* **Eliminated Boot Time Hangs:** Removed synchronous Tailscale checks during boot when Wi-Fi is still associating, eliminating 74-second DNS timeout stalls.

#### 🌐 Networking & WireGuard
* **Multi-Network Wi-Fi Cycling:** Supports up to 6 configured Wi-Fi profiles with 8-second disconnect monitoring, auto-reconnect, and dynamic DHCP fallback for secondary networks.
* **Broadened Subnet Compatibility (`/16`):** Updated default subnet mask to `255.255.0.0` for seamless communication with Windows Hotspots and Internet Connection Sharing (ICS).
* **WireGuard Inbound Route Delivery:** Fixed local interface packet remapping in `wireguardif.c` (`tcpip_input`) so incoming WireGuard traffic cleanly reaches the local web server.
* **10-Second L2 Keepalives:** Accelerated Gratuitous ARP and Gateway probes from 45s to 10s (with startup pulses) to prevent router connection drops during Wi-Fi power-save sleep.

#### 📱 Web Dashboard & Progressive Web App (PWA)
* **Cross-Platform PWA Support:** Installable on iOS, Android, and Desktop with `/manifest.webmanifest`, standalone window mode, safe-area notch padding, and custom `⚡` app icons.
* **Strict Live Network Actuation:** Eliminated offline caching via clean self-destructing service workers and strict `no-cache` headers, ensuring every command directly targets the hardware.
* **Dynamic Servo Telemetry:** Debug dashboard permanently displays the servo card with real-time DOM updates for actuation count, elapsed time, and trigger source (`Web` vs `cURL`).
* **Firmware Version Display:** Monospace `v1.2` version badge added to the debug dashboard footer, cURL terminal banner, and `/api/live` telemetry.
* **Adaptive Duration Formatting:** Automatically formats connection and uptime durations into human-readable units (`ms` → `s` → `m s` → `h m`).
* **Socket Starvation Prevention:** Added dedicated `/favicon.ico` route with 7-day caching to stop mobile browser 404 polling storms.


---

## 🎖️ Credits & Third-Party Code


This project builds upon exceptional open-source components and libraries:

* **[microlink](https://github.com/CamM2325/microlink)** by [CamM2325](https://github.com/CamM2325): Lightweight embedded Tailscale client and WireGuard coordination engine for ESP32 microcontrollers.
* **[ESP32Servo](https://github.com/madhephaestus/ESP32Servo)** by [Kevin Harrington](https://github.com/madhephaestus): High-resolution hardware PWM servo driver for Espressif chips.
* **[arduino-esp32](https://github.com/espressif/arduino-esp32)** by [Espressif Systems](https://github.com/espressif): Arduino framework core components integrated into the native ESP-IDF build system.

---

## 📜 License & Maintainer

* **Author / Maintainer:** [psychoStark](https://github.com/psychoStark)
* **License:** [Apache License 2.0](https://github.com/psychoStark/ESP32-SwitchBot/blob/update/LICENSE)

---

<div style="margin-top: 40px; padding: 20px; border-top: 1px solid var(--vp-c-divider); text-align: center;">
  <p style="font-size: 15px; color: var(--vp-c-text-2);">
    <strong>ESP32-SwitchBot Documentation</strong> &bull; Version <strong>v1.2</strong>
  </p>
</div>
