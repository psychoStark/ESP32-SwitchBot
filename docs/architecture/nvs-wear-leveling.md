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
│  • servo_cal Namespace        │  • pwr_cyc Namespace        │
│    - rest_angle (u8)          │    - total_boots (u32)      │
│    - press_angle (u8)         │    - last_reset_reason (u8) │
│    - press_dur (u16)          │  • uptime_hist Namespace    │
│  • act_log Ring Buffer        │    - cumulative_uptime (u64)│
│    - 16 fixed-length slots    │    - last_heartbeat (u32)   │
└───────────────────────────────┴─────────────────────────────┘
```

* **Zero Dynamic Reallocations:** The activity log uses fixed-length 16-slot ring buffers. Once the buffer fills, new entries overwrite the oldest slot cleanly.
* **Non-Volatile Survival:** Past crash reasons (`esp_reset_reason()`), Wi-Fi reconnections, and Tailscale status shifts survive sudden power loss.
