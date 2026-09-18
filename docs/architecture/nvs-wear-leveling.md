# NVS Wear-Leveling Forensics

Microcontroller flash memory cells degrade over time with repeated erase/write cycles (typically rated for ~100,000 writes per sector). The ESP32-SwitchBot incorporates wear-leveling techniques and circular ring buffers to guarantee decades of continuous operation without flash failure.

---

## 1. Throttled Heartbeat Persistence

To track system uptime and calculate total power outage downtime across reboots, the firmware periodically commits a Unix timestamp to Non-Volatile Storage (NVS).

* **Interval:** Every **60 seconds** (`HEARTBEAT_INTERVAL_MS = 60000`).
* **Flash Wear Calculations:**
  - 1 write per minute = 60 writes per hour = 1,440 writes per day.
  - Across a year: ~525,600 writes.
  - ESP-IDF NVS utilizes multi-sector log-structured wear leveling across the entire partition. With wear leveling distribution, the physical endurance exceeds **12 to 15+ years** of uninterrupted continuous 24/7 logging.

---

## 2. Cyclic Activity Ring Buffers

System logs, reboot events, and connection state transitions are stored in structured cyclic ring buffers:

```
┌─────────────────────────────────────────────────────────────┐
│                    NVS FLASH PARTITION                      │
├───────────────────────────────┬─────────────────────────────┤
│  • esp_log Namespace          │  • servo_cal Namespace      │
│    - boot_count (u32)         │    - rest_angle (i32)       │
│    - last_alive (u32)         │    - press_angle (i32)      │
│    - first_boot (u32)         │    - press_dur (i32)        │
│    - b0..b49 (50 Boot Slots)  │                             │
├───────────────────────────────┼─────────────────────────────┤
│  • servo_log Namespace        │  • ts_log Namespace         │
│    - sc (Actuation Count)     │    - tc (Session Count)     │
│    - s0..s19 (20 Slots)       │    - t0..t19 (20 Slots)     │
└───────────────────────────────┴─────────────────────────────┘
```

* **Zero Dynamic Reallocations:** System boot history uses a rolling 50-slot ring buffer (`b0`–`b49` with network slot index attribution `wifiIdx`), while servo triggers (`s0`–`s19`) and Tailscale sessions (`t0`–`t19`) use 20-slot ring buffers. Once full, new entries overwrite the oldest slot cleanly.
* **Non-Volatile Survival:** Past crash reasons (`esp_reset_reason()`), Wi-Fi reconnections, and Tailscale session transitions survive sudden power loss.
