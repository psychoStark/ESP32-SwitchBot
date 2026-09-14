# Terminal cURL Dashboard

For terminal enthusiasts, remote SSH sessions, and automation scripts, the ESP32 serves a full-screen interactive Terminal User Interface (TUI) directly through a standard `curl` command. No client-side utilities or Python packages are required.

---

## 1. Interactive Terminal Dashboard

Connect from any terminal (macOS Terminal, iTerm2, Linux bash, Windows WSL, or Android Termux):

```bash
bash <(curl -s http://192.168.1.50/main)
```

<div class="snapshot-terminal">
  <div class="snapshot-header">
    <span class="snapshot-dot red"></span>
    <span class="snapshot-dot yellow"></span>
    <span class="snapshot-dot green"></span>
    <span class="snapshot-title">Terminal — bash &lt;(curl -s http://192.168.1.50/main)</span>
  </div>

```text
=== SwitchBot Dashboard ===
 [1] Trigger Servo
 [2] Device Info
 [3] Logs & Debug
 [C] Clear Logs
 [O] Enable OTA
 [R] Reboot ESP32
 [S] Recalibrate Servo
 [X] Exit

Select an option: _
```

</div>

### Single-Keystroke Controls
- **`1`**: Trigger the servo button press (or launches calibration if uncalibrated).
- **`2`**: Live auto-refreshing hardware status monitor (`/info`).
- **`3`**: View boot history, connection logs, and past reboot causes.
- **`C`**: Clear saved system logs.
- **`O`**: Unlock / lock the Over-The-Air (OTA) wireless update window.
- **`R`**: Soft reboot the ESP32.
- **`S`**: Launch guided interactive calibration wizard (when calibrated).
- **`X`**: Exit back to the shell cleanly.

---

## 2. Interactive Terminal Calibration (`/calibrate`)

To calibrate servo angles directly from your command line:

```bash
bash <(curl -s http://192.168.1.50/calibrate)
```

<div class="snapshot-terminal">
  <div class="snapshot-header">
    <span class="snapshot-dot red"></span>
    <span class="snapshot-dot yellow"></span>
    <span class="snapshot-dot green"></span>
    <span class="snapshot-title">Terminal — bash &lt;(curl -s http://192.168.1.50/calibrate)</span>
  </div>

```text
==========================================
      SERVO CALIBRATION TOOL (ESP32)      
==========================================
 Status: CALIBRATED
 Current Config: Rest=25° | Press=45° | Duration=250ms
==========================================
 [1] Start Guided Calibration (Steps 1-3)
 [T] Test Current Tap
 [R] Reset Calibration Data
 [X] Exit
==========================================

Select an option: 1

[ Step 1/3: Rest Angle ] (Hovering just above button)
Enter rest angle (0 to 180, 'r' to reset, 'q' to cancel) [25]: 25
[+] Servo driven to 25°.
Confirm rest angle (25°)? [y: next / n: retry / q: cancel]: y

[ Step 2/3: Press Angle ] (Pushing button fully, not buzzing)
Enter press angle (0 to 180, 'q' to cancel) [45]: 45
[*] Previewing press stroke to 45°...
[+] Servo stroke completed.
Confirm press angle (45°)? [y: next / n: retry / q: cancel]: y

[ Step 3/3: Press Duration ] (Hold duration in ms)
Enter press duration (50-3000 ms, 'q' to cancel) [250]: 250
[*] Executing tap for 250 ms (Rest: 25°, Press: 45°)...
[+] Tap test complete.
Confirm press duration (250 ms)? [y: next / n: retry / q: cancel]: y

==========================================
       CALIBRATION REVIEW & ACTIONS       
==========================================
 Rest Angle     : 25°
 Press Angle    : 45°
 Press Duration : 250 ms
------------------------------------------
 [S] Save Calibration
 [T] Test Again
 [V] Type in Another Value
 [C] Restart Whole Calibration
 [R] Reset Calibration Data
 [X] Exit without Saving
==========================================

Select an option: S
[*] Saving calibration data to ESP32...

[+] Calibration saved successfully!
```

</div>

### Engineering Detail: `/dev/tty` Redirection
When bash scripts are executed through a pipe (`curl ... | bash`), standard input (`stdin`) is bound to the incoming HTTP network stream. 

In [`main/curl_scripts.h`](file:///Users/psychostark/Documents/PlatformIO/Projects/ESP32-SwitchBot/main/curl_scripts.h), all keystroke reads redirect from `/dev/tty`:

```bash
read -n 1 -s -p 'Select an option: ' opt </dev/tty
```

This guarantees seamless single-character keystroke detection without requiring the user to press `Enter` or suffering unexpected EOF pipeline crashes.
