# REST API & Endpoints

The ESP32-SwitchBot firmware exposes a zero-heap RESTful HTTP API. All endpoints respond with low latency and can be integrated into Home Assistant, Node-RED, Shortcuts, cron jobs, or custom scripts.

---

## Actuation Endpoints

### `POST /trigger` or `POST /`
Triggers the physical servo actuator to press and release the button according to calibrated angles.

* **Method:** `POST` (also accepts `GET`)
* **Headers:** `Content-Type: application/x-www-form-urlencoded`
* **Response (HTTP 200):**
```text
Actuation triggered.
```

---

## Live Telemetry & Status

### `GET /api/live`
Returns a compact JSON payload containing real-time system metrics, temperatures, Wi-Fi indices, and Tailscale connection states.

* **Method:** `GET`
* **Response (HTTP 200 - JSON):**
```json
{
  "u": "21m 2s",
  "uf": "17h 3m 41s",
  "t": 40,
  "ru": 137,
  "rt": 363,
  "c": 80,
  "p": 0.14,
  "ota": 0,
  "sl": 1789372685,
  "st": 1789374278,
  "sc": 34,
  "ts": 0,
  "ts_st": "Standby",
  "ts_cls": "standby",
  "ts_ip": "-",
  "ts_conn": "Subnet Active",
  "ts_cm": 0,
  "ts_cs": "",
  "cal": 1,
  "s_rest": 180,
  "s_press": 156,
  "s_dur": 400,
  "local": 0,
  "w_idx": 1,
  "w_tot": 1
}
```

#### JSON Field Schema:
| Field | Type | Description |
| :--- | :--- | :--- |
| `u` | `string` | Formatted uptime of current boot cycle (e.g. `"21m 2s"`). |
| `uf` | `string` | Cumulative fleet uptime across all power cycles. |
| `t` | `number` | CPU core temperature in degrees Celsius (°C). |
| `ru` / `rt` | `number` | Internal RAM used / total available (KB). |
| `c` | `number` | CPU clock frequency in MHz (80 MHz). |
| `p` | `number` | Estimated power draw in Watts (~0.14 W). |
| `ota` | `0` or `1` | Over-The-Air wireless update listener status (`1` = active). |
| `ts_st` | `string` | Tailscale status string (`"Standby"`, `"Connected"`, `"Starting"`). |
| `cal` | `0` or `1` | Calibration state (`1` = calibrated, `0` = factory center 90°). |
| `s_rest` | `number` | Current saved rest angle (0°–180°). |
| `s_press`| `number` | Current saved press angle (0°–180°). |
| `s_dur` | `number` | Actuation hold duration in milliseconds (50ms–3000ms). |
| `w_idx` | `number` | Current connected Wi-Fi profile index (1 to 6). |

---

## Calibration Endpoints

### `POST /api/calibrate/move?angle=<deg>`
Temporarily moves the servo to `<deg>` for live visual alignment. Does not write to flash.

### `POST /api/calibrate/test?rest=<r>&press=<p>&dur=<d>`
Runs a background test tap using temporary test parameters.

### `POST /api/calibrate/hold?state=<1|0>&press=<p>&rest=<r>`
Enables manual hold. When `state=1`, holds servo at `press` angle. When `state=0`, returns to `rest`.
> [!NOTE]
> Includes a 10,000ms firmware safety watchdog that automatically releases the motor if `state=0` is not received.

### `POST /api/calibrate/save?rest=<r>&press=<p>&dur=<d>`
Stores calibrated values permanently to NVS flash (`servo_cal` namespace).

### `POST /api/calibrate/reset`
Clears NVS calibration values, reverting device to uncalibrated center (90°).

---

## System Management Endpoints

### `POST /ota/enable`
Unlocks port 3232 for wireless firmware flashing for 10 minutes.
* **Payload:** `key=<OTA_PASSWORD>` (if configured)

### `POST /ota/disable`
Immediately closes port 3232 and terminates OTA listener.

### `POST /reboot`
Performs a clean FreeRTOS software restart of the ESP32.

### `POST /clear-logs`
Erases activity and connection history ring buffers from NVS flash.
