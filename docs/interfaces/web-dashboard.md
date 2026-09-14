# Web Dashboard & Calibration

The Web Dashboard delivers a responsive, cyber-dark control interface styled with glassmorphic cards, glowing accents, and live metric updates. It requires zero mobile applications or browser extensions and works across smartphones, tablets, and desktop browsers.

---

## 1. Main Dashboard (`/main`)

The home screen provides a focused, zero-distraction view for actuating the switch or jumping directly to device tools.

<BrowserWindow title="http://192.168.1.50/main — SwitchBot Dashboard">
  <div class="esp-scope">
    <div class="wrap centered">
      <h1>SwitchBot</h1>
      <div class="nav">
        <a class="primary" href="javascript:void(0)">⚡ Trigger Servo</a>
        <a href="javascript:void(0)">💻 Device Info</a>
        <a href="javascript:void(0)">🔧 Logs &amp; Debug</a>
      </div>
    </div>
  </div>
</BrowserWindow>

> [!TIP]
> If the device is on its first boot and uncalibrated, the primary action button automatically becomes:
> `🎯 Calibrate Servo` (`/calibrate`).

---

## 2. Device Info (`/info`)

The info screen displays live hardware metrics, dynamic board detection (`ESP32-S3-N16R8`), memory utilization, and connectivity details.

<BrowserWindow title="http://192.168.1.50/info — Device Info">
  <div class="esp-scope">
    <div class="wrap">
      <h1>💻 Device Info</h1>
      <h3>Live</h3>
      <div class="card">
        <div class="row"><span class="k">Uptime</span><span class="v mono">21m 2s</span></div>
        <div class="row"><span class="k">CPU Clock</span><span class="v mono">80 MHz</span></div>
        <div class="row"><span class="k">CPU Temp.</span><span class="v mono">40 °C</span></div>
        <div class="row"><span class="k">Est. Power</span><span class="v mono">~0.14 W</span></div>
      </div>
      <h3>Hardware &amp; Storage</h3>
      <div class="card">
        <div class="row"><span class="k">Model</span><span class="v mono">ESP32-S3-N16R8</span></div>
        <div class="row"><span class="k">RAM</span><span class="v mono">137/363 KB</span></div>
        <div class="row"><span class="k">Flash</span><span class="v mono">1410/4096 KB</span></div>
        <div class="row"><span class="k">PSRAM</span><span class="v mono">24/8192 KB</span></div>
      </div>
      <h3>Network &amp; Connectivity</h3>
      <div class="card">
        <div class="row"><span class="k">Wi-Fi SSID</span><span class="v mono">Home_Network_2.4G</span></div>
        <div class="row"><span class="k">IP Address</span><span class="v mono">192.168.1.50</span></div>
        <div class="row"><span class="k">Hostname</span><span class="v mono">esp32.local</span></div>
      </div>
      <h3>Tailscale</h3>
      <div class="card">
        <div class="row"><span class="k">Status</span><span class="v"><span class="pill standby">Standby</span></span></div>
        <div class="row"><span class="k">Hostname</span><span class="v mono">esp32</span></div>
        <div class="row"><span class="k">Connection</span><span class="v mono">Subnet Router Active</span></div>
      </div>
      <a class="back" href="javascript:void(0)">← Back to Dashboard</a>
    </div>
  </div>
</BrowserWindow>

---

## 3. Interactive SVG Calibration (`/calibrate`)

Fine-tuning servo positions is performed through dual rotary dials with real-time feedback, direct numeric touch typing, and safety timeout guards.

<BrowserWindow title="http://192.168.1.50/calibrate — Servo Calibration">
  <CalibrationSimulator />
</BrowserWindow>

### Key Safety Protections:
* **Live Rest Preview:** Dragging the Rest dial debounces live `POST /api/calibrate/move?angle=...` updates so you can visually confirm clearance before saving.
* **Bottom-Gap Barrier:** The rotary gesture engine enforces a 40° bottom deadzone, preventing accidental 0° ↔ 180° flip wraps.
* **10-Second Auto-Release Guard:** Pressing the **Hold** button commands the servo to the press depth. If held longer than 10,000ms (or if the browser tab disconnects), the firmware safety watchdog automatically springs the servo back to rest to protect the motor from burnout.

---

## 4. Logs &amp; Debug (`/debug`)

The debug screen gives real-time visibility into past reboots, servo actuation history, and Wi-Fi failover metrics.

<BrowserWindow title="http://192.168.1.50/debug — Logs &amp; Debug">
  <div class="esp-scope">
    <div class="wrap">
      <h1>🔧 Logs &amp; Debug</h1>
      <h3>General</h3>
      <div class="card">
        <div class="row"><span class="k">Uptime Since Boot</span><span class="v mono">21m 2s</span></div>
        <div class="row"><span class="k">Uptime Since Flash</span><span class="v mono">17h 3m 41s</span></div>
        <div class="row"><span class="k">Last Approx. Downtime</span><span class="v mono">8s</span></div>
      </div>
      <h3>Boot Stats</h3>
      <div class="card">
        <div class="row"><span class="k">Boot Time</span><span class="v mono">01:33:04 PM 14-Sep-2026</span></div>
        <div class="row"><span class="k">Wi-Fi Connect</span><span class="v mono">1.82s</span></div>
        <div class="row"><span class="k">Tailscale Connect</span><span class="v mono">2.14s</span></div>
        <div class="row"><span class="k">Total Boots</span><span class="v mono">34</span></div>
        <div class="row"><span class="k">Last Reset Cause</span><span class="v mono">Software Reset</span></div>
        <div class="row"><span class="k">Last Reset Time</span><span class="v mono">01:32:56 PM 14-Sep-2026</span></div>
        <div class="row"><span class="k">Connected Wi-Fi</span><span class="v mono">Home_Network_2.4G</span></div>
      </div>
      <h3>Servo</h3>
      <div class="card">
        <div class="row"><span class="k">Last Trigger</span><span class="v mono">2m 14s ago</span></div>
        <div class="row"><span class="k">Trigger Source</span><span class="v mono">Web</span></div>
        <div class="row"><span class="k">Total Triggers</span><span class="v mono">42</span></div>
      </div>
      <h3>Servo Trigger History</h3>
      <div class="card">
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title">Servo Actuation</span>
            <span class="log-sub">01:52:10 PM 14-Sep-2026</span>
          </div>
          <span class="log-badge">CURL</span>
        </div>
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title">Servo Actuation</span>
            <span class="log-sub">11:18:42 AM 14-Sep-2026</span>
          </div>
          <span class="log-badge">WEB</span>
        </div>
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title">Servo Actuation</span>
            <span class="log-sub">08:04:19 AM 14-Sep-2026</span>
          </div>
          <span class="log-badge">WEB</span>
        </div>
      </div>
      <h3>Tailscale Connection History</h3>
      <div class="card">
        <div class="log-item latest-entry">
          <div class="log-meta">
            <span class="log-title">Active Session</span>
            <span class="log-sub">Started: 01:33:06 PM 14-Sep-2026</span>
            <span class="log-sub">Duration: 21m 2s &bull; Downtime: 8s</span>
          </div>
          <span class="log-badge on">ACTIVE</span>
        </div>
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title">Tailscale Session</span>
            <span class="log-sub">Started: 08:00:15 AM 14-Sep-2026</span>
            <span class="log-sub">Ended: 01:32:56 PM 14-Sep-2026</span>
            <span class="log-sub">Duration: <b>5h 32m 41s</b></span>
          </div>
          <span class="log-badge">ENDED</span>
        </div>
      </div>
      <h3>Previous Boot History</h3>
      <div class="card">
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title warn">Software Reset</span>
            <span class="log-sub">08:00:12 AM 14-Sep-2026 &bull; Approx. Downtime: 12s &bull; Wi-Fi: Home_Network_2.4G</span>
          </div>
        </div>
        <div class="log-item">
          <div class="log-meta">
            <span class="log-title ok">Power-on Reset</span>
            <span class="log-sub">08:29:43 PM 13-Sep-2026 &bull; Approx. Downtime: 1m 4s &bull; Wi-Fi: Home_Network_2.4G</span>
          </div>
        </div>
      </div>
      <h3>OTA Updates</h3>
      <div class="card">
        <div class="row">
          <span class="k">Status</span>
          <span class="v"><span class="pill off">Disabled</span></span>
        </div>
      </div>
      <div class="actions">
        <form style="display:flex;gap:10px;align-items:center;">
          <input type="password" placeholder="OTA Key" style="flex:1;min-width:0;">
          <button class="warn" type="button" style="flex:1;">Enable OTA</button>
        </form>
      </div>
      <hr class="divider">
      <div class="actions" style="margin-bottom:12px;">
        <button type="button">🎯 Recalibrate Servo</button>
      </div>
      <div class="actions">
        <button class="danger" type="button" style="flex:1;">🔄 Reboot</button>
        <button class="warn" type="button" style="flex:1;">🗑 Clear Logs</button>
      </div>
      <a class="back" href="javascript:void(0)">← Back to Dashboard</a>
    </div>
  </div>
</BrowserWindow>

---

## 5. Background Visibility Optimization

To preserve battery life on both client devices and the ESP32:
* Live polling against `/api/live` listens to the HTML5 Page Visibility API (`document.hidden`).
* When the user locks their smartphone or switches browser tabs, polling **instantly halts**.
* Polling resumes automatically the millisecond the tab returns to the foreground.
