---
layout: home

hero:
  name: "ESP32-SwitchBot"
  text: "Encrypted Physical Switch Actuator"
  tagline: "Industrial-grade DIY switch actuator & fingerbot with cross-platform PWA, interactive terminal cURL TUI, and embedded Tailscale WireGuard VPN for turning on PCs and appliances from anywhere in the world."
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
  - icon: ⚡
    title: Standalone PWA & Offline Precaching
    details: Install directly to your iPhone, Android, or Desktop home screen. Powered by a service worker with instant cached shell delivery and native safe-area notch padding.
  - icon: 💻
    title: Interactive Terminal cURL TUI
    details: Stream a full-screen interactive Bash dashboard directly inside your terminal with a single <code>curl</code> command. Zero client software required.
  - icon: 🌐
    title: Embedded Tailscale & HA Failover
    details: Access from anywhere on Earth via WireGuard mesh VPN without port forwarding. Automatic cold standby saves power when a primary subnet router is alive.
  - icon: 🛡️
    title: Dual-Core FreeRTOS Segregation
    details: Network I/O runs on Core 0 while physical servo actuation runs on Core 1 with task priority elevation (priority 5) for sub-5ms jitter-free motor timing.
  - icon: ❄️
    title: Ice-Cold Thermals (~40°C)
    details: Downclocked to 80 MHz CPU frequency with FreeRTOS tickless idle sleep. Operates at a whisper-quiet ~0.14 W without generating heat inside switch housings.
  - icon: ⚙️
    title: Interactive SVG Calibration
    details: Touch-friendly rotary dials with 40° bottom deadzones, live motor preview, test taps, and a 20-second safety watchdog on manual holds.
---

<div class="home-release-banner">
  <span class="release-item">Current Release: <strong>v1.2</strong></span>
  <span class="release-dot">&bull;</span>
  <span class="release-item">Apache 2.0 Open Source</span>
  <span class="release-dot">&bull;</span>
  <span class="release-item">Tested on ESP32-S3 (N16R8)</span>
</div>
