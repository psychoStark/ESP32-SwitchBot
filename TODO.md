**fix**
    - [x] connectivity through local and subnet is not consistent across devices
        - resolved single-network auto-reconnect bug in checkWifiReconnectIfNeeded
        - broadened subnet mask to 255.255.0.0 (/16) for Windows hotspot / ICS (192.168.137.x) compatibility
        - accelerated gratuitous ARP keepalive from 45s to 15s with immediate startup pulse
        - restored local interface packet remapping in wireguardif.c (tcpip_input) for 192.168.1.50 over WireGuard
    - [x] sometimes the boot time shows as 74.03s and wifi connect as 0ms, find the issue and fix
        - identified root cause: boot Tailscale check ran synchronously even when Wi-Fi was still connecting in background; 3 failed HTTPS calls to api.tailscale.com stalled on lwIP DNS resolution timeouts (~26s + ~26s + ~20s = ~74.03s)
        - guarded boot Tailscale check with WiFi.status() == WL_CONNECTED and reduced attempts to 2 with 500ms delay; if Wi-Fi is connecting in background, immediately enter cold Standby mode without blocking
        - added background wifi_connect_ms latch in loop() and reconnect handler so Wi-Fi association time is properly recorded and never displays 0ms
        - accelerated initial ts_watchdog verification delay to 15s to quickly confirm subnet router status if Wi-Fi connected right after boot

**optimize**
    - [x] check doc website for inconsistensicies and code optimizations
        - synchronized calibration manual hold timeout from 10s to 20s (MAX_HOLD_DURATION_MS = 20000) across all guides, diagrams, and hero features
        - corrected NVS namespace documentation in nvs-wear-leveling.md to reflect real firmware namespaces (esp_log, servo_log, ts_log, servo_cal)
        - added FreeRTOS task priority 5 elevation documentation during triggerPress() in concurrency-power.md
        - added v1.2 release notes covering 4s cooldown, 4.5s live polling, 10s ARP keepalives, version footer, and PWA capabilities in about.md
        - updated configuration table in configuration.md with exact main.cpp line numbers and 255.255.0.0 (/16) subnet mask
    - [x] check for inconsistencies among the doc and the actual code
        - corrected Gratuitous ARP and Gateway ARP probe interval in documentation.md from 15s to 10s
        - updated live telemetry polling interval in documentation.md from 3.5s to 4.5s
        - updated PRESS_COOLDOWN_MS default in documentation.md from 2s to 4s (main.cpp:210)
        - synchronized all Section 10 configuration table file line numbers to match current main.cpp
        - documented zero-jitter task priority 5 elevation during servo actuation in FreeRTOS concurrency section
    - [x] optimize webserver for pwa accross platforms
        - added /manifest.webmanifest with standalone display mode, portrait orientation, and cyber-dark theme colors
        - implemented self-destructing cleanup worker and network-direct execution with no-cache headers to prevent stale offline caching
        - served native ⚡ emoji SVG icon (/icon.svg), apple-touch-icon.png, and favicon.ico with 7-day browser caching
        - injected mobile PWA meta tags, apple-mobile-web-app-capable, and apple-mobile-web-app-status-bar-style in wrapPage and sendWrappedPageStream
        - added mobile safe-area insets padding and overscroll-behavior-y: none to prevent standalone window rubber-banding
    - [x] improve seo for the readme and the docs
        - added dedicated problem-solving guide contrasting ESP32-SwitchBot with Wake-on-LAN limitations, motherboard header relays, and smart plugs
        - highlighted advantages over ESPHome, Tasmota, Home Assistant Zigbee, Blynk, Sinric Pro, and cloud tunnels
        - added structured Search & Discovery Index covering all target keyword queries (diy switchbot, remote pc turn on, fingerbot, SG90 servo presser, tailscale on esp32)
        - added meta keywords tag to VitePress config.mts and updated documentation hero tagline

**feature**
    - [x] add firmare ver in debug page
        - exposed `FIRMWARE_VERSION` ("1.2") at the very last lines of `main/main.cpp` for quick and convenient version bumping
        - displayed small, styled version footer at the bottom of the debug page (`v1.2`)
        - added firmware version entry to cURL debug endpoint and `/api/live` telemetry payload
    - [x] the servo tab is not dynamically updated now in debug page
        - guaranteed Servo card (`#servo-card`) is permanently present on `/debug` even prior to the first actuation
        - assigned element IDs (`servo-ago`, `servo-src`, `servo-count`) for real-time DOM mutation
        - expanded `/api/live` JSON schema to output latest trigger source `ss` ("cURL" vs "Web")
        - updated `APP_JS` (`/app.js?v=4`) to dynamically recalculate and poll actuation age, trigger source, and total trigger count
    