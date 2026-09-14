---
layout: home

hero:
  name: "ESP32-SwitchBot"
  text: "Encrypted Physical Switch Actuator"
  tagline: "Industrial-grade actuator firmware with zero-app Web UI, interactive terminal cURL TUI, and embedded Tailscale VPN mesh failover."
  actions:
    - theme: brand
      text: Get Started
      link: /guide/getting-started
    - theme: alt
      text: Explore Architecture
      link: /architecture/concurrency-power
    - theme: alt
      text: REST API
      link: /interfaces/rest-api

features:
  - icon: 📱
    title: Zero-App Web & Terminal UI
    details: Instant access from any browser (phone, PC, tablet) or any terminal via a single <code>curl</code> command. No proprietary mobile apps or third-party cloud bridges required.
  - icon: ❄️
    title: Ice-Cold Thermals (~40°C)
    details: Downclocked to 80 MHz CPU frequency with FreeRTOS tickless idle sleep. Operates at a whisper-quiet ~0.14 W without generating heat.
  - icon: 🛡️
    title: Dual-Core FreeRTOS Segregation
    details: Network I/O and web serving run on Core 0, while physical servo actuation and the responsive loop run on Core 1 for sub-5ms instant triggering.
  - icon: 🌐
    title: Embedded Tailscale & HA Failover
    details: Access from anywhere on Earth via WireGuard mesh VPN. Features automatic cold standby when a primary subnet router is alive to save power.
  - icon: 🔄
    title: Multi-Network Wi-Fi Cycling
    details: Configurable for up to 6 Wi-Fi networks with exponential backoff and ARP keepalives to prevent disconnects during modem sleep.
  - icon: ⚙️
    title: Interactive SVG Calibration
    details: Touch-friendly rotary dials with 40° bottom deadzones, live motor preview, test taps, and a 10-second safety watchdog on manual holds.
---

<div class="home-release-banner">
  <span class="release-item">Current Release: <strong>v1.2</strong></span>
  <span class="release-dot">&bull;</span>
  <span class="release-item">Apache 2.0 Open Source</span>
  <span class="release-dot">&bull;</span>
  <span class="release-item">Tested on ESP32-S3 (N16R8)</span>
</div>
