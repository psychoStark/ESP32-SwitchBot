# About ESP32-SwitchBot

**ESP32-SwitchBot** is an open-source, industrial-grade firmware designed to turn an Espressif ESP32 microcontroller into an encrypted, remotely-accessible switch actuator. It mechanically actuates physical buttons (power switches, lights, appliances, or PC power buttons) on command.

---

## 🚀 Version 1.2 Release Notes (Current)

Version **v1.2** builds directly upon the v1.0 production baseline, focusing on zero-truncation streaming reliability, multi-network Wi-Fi failover, dynamic hardware auto-detection, and motor safety guards.

### 🌟 What's New in v1.2

* **Dynamic Hardware Model Detection:** Replaced static chip model strings with native ESP-IDF heap capabilities (`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`), automatically detecting and formatting exact board hardware (e.g. `ESP32-S3-N16R8`, `ESP32-N4`).
* **In-Code Board Override (`BOARD_NAME`):** Added `#define BOARD_NAME ""` in `main/main.cpp` for instant manual overrides without touching sensitive credentials or rerunning setup tools.
* **Multi-Network Wi-Fi Failover (Up to 6 Networks):** Expanded Wi-Fi subsystem to cycle through up to 6 configured Wi-Fi network profiles (`WIFI_SSID_1..6`) with dynamic DHCP fallback for secondary networks.
* **Broadened Subnet Mask (`255.255.0.0` /16):** Broadened local subnet mask to `/16` to enable direct, seamless bidirectional communication with Windows Hotspot and Internet Connection Sharing (ICS) clients (`192.168.137.x`).
* **WireGuard Inbound Local Packet Remapping:** Enhanced `wireguardif.c` (`tcpip_input`) to directly map incoming decrypted Tailscale packets destined for `192.168.1.50` into the local lwIP stack, eliminating dropped packets.
* **Zero-Lag Actuation Priority Elevation:** Added FreeRTOS task priority elevation (priority 5) during `triggerPress()` to guarantee zero-jitter hardware PWM timing regardless of heavy network traffic.
* **Servo Motor Burnout Watchdog (20s):** Implemented an active 20,000ms safety watchdog on manual calibration holds (`/api/calibrate/hold`). If a client disconnects or holds the motor down, the firmware automatically springs the arm back to rest.
* **4-Second Press Cooldown & Click Debounce:** Increased `PRESS_COOLDOWN_MS` to 4s and added client-side pointer-event guards to reject accidental multi-clicks and browser speculative retries.
* **Non-Blocking Boot Standby:** Eliminated ~74s boot stalls by guarding startup Tailscale checks with Wi-Fi association state and backgrounding association timing (`wifi_connect_ms`).
* **Accelerated L2 ARP Keepalive (10s):** Broadcasts Gratuitous ARP and Gateway ARP probes every 10s (plus startup/reconnect pulses) to prevent router connection drops during modem sleep.
* **Full-Stream Web Delivery & Battery-Saving Polling:** Streamed JavaScript directly with `sendWrappedPageStream`, tuned live polling to 4.5s, and halted polling when the tab is hidden (`document.hidden`).
* **Dynamic Servo Card Telemetry:** Rendered the debug Servo telemetry card permanently and added live DOM updates for trigger age, source ("cURL" vs "Web"), and total counts.
* **Firmware Version Display:** Monospace `v1.2` footer on `/debug`, cURL terminal view, and `/api/live` telemetry.
* **Cross-Platform Progressive Web App (PWA):** Standalone installable PWA for iOS, Android, and Desktop with `/manifest.webmanifest`, precaching Service Worker (`/sw.js`), native `⚡` SVG icon, and safe-area notch padding.
* **Long-Term Browser Caching:** Configured HTTP headers (`Cache-Control: public, max-age=604800, immutable`) for `/style.css`, `/app.js`, `/manifest.webmanifest`, and `/icon.svg`.

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
