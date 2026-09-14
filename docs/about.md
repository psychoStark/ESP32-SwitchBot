# About ESP32-SwitchBot

**ESP32-SwitchBot** is an open-source, industrial-grade firmware designed to turn an Espressif ESP32 microcontroller into an encrypted, remotely-accessible switch actuator. It mechanically actuates physical buttons (power switches, lights, appliances, or PC power buttons) on command.

---

## 🚀 Version 1.2 Release Notes (Current)

Version **v1.2** builds directly upon the v1.0 production baseline, focusing on zero-truncation streaming reliability, multi-network Wi-Fi failover, dynamic hardware auto-detection, and motor safety guards.

### 🌟 What's New in v1.2

* **Dynamic Hardware Model Detection:** Replaced static chip model strings with native ESP-IDF heap capabilities (`heap_caps_get_total_size(MALLOC_CAP_SPIRAM)`), automatically detecting and formatting exact board hardware (e.g. `ESP32-S3-N16R8`, `ESP32-N4`).
* **In-Code Board Override (`BOARD_NAME`):** Added `#define BOARD_NAME ""` in `main/main.cpp` for instant manual overrides without touching sensitive credentials or rerunning setup tools.
* **Multi-Network Wi-Fi Failover (Up to 6 Networks):** Expanded Wi-Fi subsystem to cycle through up to 6 configured Wi-Fi network profiles (`WIFI_SSID_1..6`) with exponential reconnect backoff and L2 Gratuitous ARP keepalives.
* **Full-Stream Calibration JS Delivery:** Eliminated static buffer clipping in `sendWrappedPageStream`, directly streaming the complete ~8 KB JavaScript payload for responsive touch dials, linear sliders, live tests, and save actions.
* **Servo Motor Burnout Watchdog:** Implemented an active 10,000ms safety watchdog on manual calibration holds (`/api/calibrate/hold`). If a client disconnects or holds the motor down, the firmware automatically springs the arm back to rest.
* **Dedicated Watchdog Task (`ts_watchdog`):** Offloaded periodic HTTPS calls to `api.tailscale.com` into a dedicated Core 0 FreeRTOS background task. Blocking TLS handshakes never delay `loop()` or physical servo triggering on Core 1.
* **Multi-Network Host OTA Routing (`-I <lan_ip>`):** Resolved reverse connection broken pipes on host machines running Tailscale, Docker, or VPN tunnels by documenting explicit `-I <lan_ip>` binding for `espota.py`.
* **Battery-Saving Page Visibility:** Live telemetry polling against `/api/live` now listens to `document.hidden`, immediately halting background network requests when phone screens lock or tabs switch.
* **Long-Term Browser Caching:** Configured HTTP headers (`Cache-Control: public, max-age=604800, immutable`) for `/style.css` and `/app.js`, reducing repeat visits to 0 KB.

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
