# Configuration & Secrets

The firmware separates sensitive credentials (Wi-Fi passwords, Tailscale keys, OTA secrets) from hardware board definitions and runtime logic.

---

## 1. Automated Setup Tool (`setup_secrets.py`)

Run the cross-platform interactive CLI tool to generate `main/secrets.h`:

```bash
python3 setup_secrets.py
```

<div class="snapshot-terminal">
  <div class="snapshot-header">
    <span class="snapshot-dot red"></span>
    <span class="snapshot-dot yellow"></span>
    <span class="snapshot-dot green"></span>
    <span class="snapshot-title">Terminal — python3 setup_secrets.py</span>
  </div>

```text
┌────────────────────────────────────────────────────────────────────────┐
│           ESP32-SwitchBot : Interactive secrets.h Generator            │
└────────────────────────────────────────────────────────────────────────┘
  ℹ Target destination : main/secrets.h
  ℹ Step-by-step setup  : Confirm with [Y] or retype with [n]

┌─ [Step 1] Wi-Fi Network Setup ─────────────────────────────────────────┐
│  Configuring Primary & Fallback Wi-Fi Networks                         │
│  Network #1 is mandatory. Up to 5 additional fallback networks optional.│
└────────────────────────────────────────────────────────────────────────┘

  ▸ Enter Wi-Fi #1 (Primary) SSID: Home_Network_2.4G
  ▸ Enter Wi-Fi #1 (Primary) Password: **************** (16 chars)
  ✔ Verified Wi-Fi Network #1.

  ▸ Do you want to add another Wi-Fi network (fallback)? [y/N]: n

┌─ [Step 2] Operation Mode ──────────────────────────────────────────────┐
│  Local-Only vs. Tailscale Remote Access                                │
│  Fully local mode disables Microlink & Tailscale, saving CPU and RAM.   │
│  The ESP32 will only be accessed over local Wi-Fi or subnet router.    │
└────────────────────────────────────────────────────────────────────────┘

  ▸ Are you trying to setup this ESP fully local (no Tailscale)? [y/N]: n
  ▸ Tailscale Auth Key: tskey-auth-****************
  ▸ Tailscale Device Hostname [esp32]: esp32

┌─ [Step 3] Device Security ─────────────────────────────────────────────┐
│  Over-The-Air (OTA) Flash Protection                                   │
│  PIN or passphrase required to authorize wireless firmware updates.    │
└────────────────────────────────────────────────────────────────────────┘

  ▸ Enter OTA Security Password (leave blank for one-click unlock): ******** (8 chars)

┌────────────────────────────────────────────────────────────────────────┐
│                      CONFIGURATION REVIEW SUMMARY                      │
├────┬────────────────────────────┬──────────────────────────────────────┤
│ #  │ Setting                    │ Configured Value                     │
├────┼────────────────────────────┼──────────────────────────────────────┤
│ 1  │ Operation Mode             │ Tailscale Enabled                    │
│ 2  │ Wi-Fi #1 (Primary)         │ Home_Network_2.4G (pass set)         │
│ 3  │ Tailscale Auth Key         │ tskey-auth-*********9876             │
│ 4  │ Tailscale Device Hostname  │ esp32                                │
│ 5  │ OTA Security Password      │ s******t (8 chars)                   │
└────┴────────────────────────────┴──────────────────────────────────────┘

  ✔ Successfully generated main/secrets.h!
```

</div>

The script features clean ANSI formatting and guides you through:
1. **Wi-Fi Network Configuration:** 1 primary network + up to 5 automatic fallback networks (with WPA/WPA2 passphrases).
2. **Operation Mode:** Choose between:
   - **Tailscale Mesh VPN Mode:** Global remote access with optional subnet router watchdog.
   - **Fully Local Mode:** Disables Microlink & WireGuard completely to minimize RAM and CPU overhead.
3. **Over-The-Air (OTA) Key:** Password/PIN to protect wireless firmware flashing (or blank for one-click unlock).

---

## 2. Hardware Model Override (`BOARD_NAME`)

Hardware model names are automatically detected at boot by querying the ESP32 chip model, flash size, and PSRAM capabilities via ESP-IDF native heap APIs (e.g. `ESP32-S3-N16R8`).

If you wish to customize or override the displayed name, you can do so directly in [`main/main.cpp`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/main.cpp#L53-L57):

```cpp
// Hardware Model Name (Optional Manual Override)
// Leave empty ("") to let firmware automatically detect your ESP32 chip model, flash, and PSRAM (e.g. "ESP32-S3-N16R8").
// If you want to change it or if detection is wrong, specify your custom board name here (e.g. "ESP32-S3 DOIT"):
#define BOARD_NAME ""
```

* **Default (`""`)**: Automatically generates `ESP32-S3-N16R8`, `ESP32-N4`, etc.
* **Custom String**: e.g., `#define BOARD_NAME "SwitchBot Pro"` renders that exact string on the web dashboard and cURL info screen.

---

## 3. Configuration Reference Table

The following parameters are located in [`main/main.cpp`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/main.cpp) and [`main/secrets.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/secrets.h):

| Parameter | Location | Default Value | Description |
| :--- | :--- | :--- | :--- |
| `BOARD_NAME` | `main.cpp:56` | `""` | Optional manual hardware model override. |
| `TIMEZONE_OFFSET` | `main.cpp:51` | `"+05:30"` | Timezone offset for NTP synchronization (supports `+05:30`, `-05:00`, `0530`, `0`). |
| `WIFI_SSID_1..6` | `secrets.h` | `""` | Up to 6 configured Wi-Fi network SSIDs for automatic failover. |
| `WIFI_PASSWORD_1..6`| `secrets.h` | `""` | Corresponding Wi-Fi WPA2 passwords. |
| `local_IP` | `main.cpp:108` | `192.168.1.50` | Static IP of the ESP32 on the local Wi-Fi subnet. |
| `gateway` | `main.cpp:109` | `192.168.1.1` | Default router gateway IP address. |
| `subnet` | `main.cpp:110` | `255.255.255.0`| Subnet mask (`/24`). |
| `tailscaleAdvertiseRoute` | `main.cpp:116` | `"192.168.1.0/24"` | CIDR advertised to Tailnet for high-availability subnet failover. |
| `servoPin` | `main.cpp:123` | `1` | Output GPIO connected to servo PWM line. |
| `PRESS_COOLDOWN_MS` | `main.cpp:186` | `2000` (2s) | Cooldown interval between successive button pushes to protect the motor. |
| `HEARTBEAT_INTERVAL_MS` | `main.cpp:179` | `60000` (60s) | NVS timestamp write interval for safe flash wear-leveling. |
| `OTA_AUTO_TIMEOUT_MS` | `main.cpp:176` | `600000` (10m) | Inactivity auto-close timer for port 3232 after being unlocked. |
| `OTA_KEY` | `secrets.h` | `""` | Passphrase to authenticate Over-The-Air updates. |
