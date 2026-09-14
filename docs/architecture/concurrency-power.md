# FreeRTOS Concurrency & Thermals

The ESP32-SwitchBot architecture maximizes responsiveness while keeping power consumption and operational temperatures exceptionally low (~40°C).

---

## Dual-Core Task Segregation

The ESP32-S3 contains two Xtensa LX7 processor cores. Network operations and actuator controls are physically partitioned across cores:

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
│  • ts_watchdog Task (Priority 1, 8KB) │  • ml_wg_mgr Task (Priority 6, 8 KB)  │
│    - Asynchronous Tailscale API checks│    - WireGuard ChaCha20-Poly1305      │
│  • ml_net_io Task (Priority 5, 6 KB)  │    - Handshake timers & peer sessions │
│    - DERP TLS socket transport        │                                       │
└───────────────────────────────────────┴───────────────────────────────────────┘
```

### 1. Core 0: Network & WebServer Operations
* **`http_srv` Task (Priority 4):** Dispatches incoming HTTP connections and streams chunked responses with a 2ms yield tick (`vTaskDelay(pdMS_TO_TICKS(2))`).
* **`ts_watchdog` Task (Priority 1):** Performs periodic HTTPS calls to `api.tailscale.com` in the background. Because it is completely decoupled from Core 1, blocking TLS handshakes **never** cause servo actuation lag.

### 2. Core 1: Control Loop & Servo Actuation
* **`arduino_loop` Task (Priority 1):** Manages physical actuator movements, NTP clock sync, Wi-Fi failover checks, and flash heartbeats.
* **Asynchronous Actuation:** When `/trigger` is called on Core 0, it signals Core 1 via FreeRTOS task notification (`xTaskNotifyGive(loopTaskHandle)`), responding to the client in < 5ms before the mechanical stroke completes.

---

## Low-Power 80 MHz Clock & Tickless Idle

Standard ESP32 firmware runs at 240 MHz, consuming up to 800mW and generating chip temperatures of 60–70°C. 

### Why 80 MHz is the Sweet Spot:
1. **Peripheral Stability:** At 80 MHz, the internal APB bus operates at its native clock speed without baud rate drift on UART, SPI, or I2C.
2. **Thermal Drop:** Operating temperature falls from ~65°C to **~40°C**, allowing safe enclosed mounting inside switch housings.
3. **Power Consumption:** Drops power draw to **~0.14 W** (~28mA average current).
4. **Tickless Idle:** FreeRTOS automatically halts the CPU core via the `waiti 0` instruction during idle periods between the 10ms scheduling cycles.
