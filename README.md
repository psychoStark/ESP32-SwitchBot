# ESP32-S3 SwitchBot Dashboard with Embedded Tailscale VPN

Transform an **ESP32** into an encrypted smart switch actuator that physically pushes buttons (power switches, lights, appliances, or PC power buttons) on command. Control it locally over your home Wi-Fi or remotely from anywhere in the world via **Tailscale VPN**.

The device features two clean, zero-software interfaces:
* **Web Dashboard:** Open in any web browser on your phone, tablet, or PC.
* **Terminal Dashboard:** Open directly in any terminal using a standard `curl` command.

---

## Features

* **Local or Remote Access:** Control your SwitchBot locally over your home Wi-Fi network without any cloud services, or connect securely from anywhere via Tailscale mesh VPN.
* **Cloud Watchdog & Automatic Standby:** Keeps the ESP32 cool and power-efficient when your main home network router is online, and automatically takes over if your primary router goes offline.
* **Interactive Servo Calibration:** Easily adjust button pressing angles and durations using live dials in your browser or a guided terminal wizard.
* **Instant Button Response:** Sends an immediate confirmation when clicked, so you never have to wait for the physical servo movement to complete.
* **Activity & Reset Logs:** Keeps track of past button presses, boot causes, power outage downtime, and connection history.
* **OTA for Future Updates:** Wirelessly update the ESP32 with new firmware versions or updated network credentials over Wi-Fi without needing a USB cable.

---

## Hardware Requirements & Wiring

### Hardware Checklist
* **Microcontroller:**
  * **Tested Board:** ESP32-S3 (DOIT N16R8 with 16 MB Flash, 8 MB Octal PSRAM).
  * **Compatible Boards:** ESP32-S3 (all variants), ESP32 (Classic), ESP32-C3, and ESP32-S2.
  * **Minimum Requirement:** Any ESP32 development board with at least 4 MB Flash (8 MB or 16 MB recommended for dual-slot wireless OTA updates). *(For board-specific feature differences, see `documentation.md`).*
* **Actuator:** Standard 3.3V–5V micro servo (e.g., TowerPro SG90, MG90S).
* **Power Supply:** Standard 5V USB-C power supply or phone charger.
* **USB Cable:** A data-capable USB cable for the initial flash.

### Wiring Diagram

```
ESP32-S3 Pin                     Servo Motor (SG90 / MG90S)
────────────────────────────────────────────────────────────
GPIO 1 (Signal)    ───────────►   Signal Wire (Orange / Yellow)
5V / VIN           ───────────►   VCC Wire    (Red)
GND                ───────────►   GND Wire    (Brown / Black)
```

---

## Installation Guide

### 1. Prerequisites

Make sure you have the following installed on your computer:
* **Python 3.6+** (Standard Python; no extra packages needed).
* **ESP-IDF v5.1.x** (v5.1.4 recommended). Follow the [Official ESP-IDF Installation Guide](https://docs.espressif.com/projects/esp-idf/en/v5.1.4/esp32s3/get-started/).
* **Git**.

### 2. Download the Project & Components

Open your terminal and run:

```bash
# Clone this repository
git clone https://github.com/psychoStark/ESP32-SwitchBot.git
cd ESP32-SwitchBot

# Download required Arduino and Servo components
git clone -b release/v2.x https://github.com/espressif/arduino-esp32.git components/arduino
git clone https://github.com/madhephaestus/ESP32Servo.git components/ESP32Servo
```

---

### 3. Setup Your Credentials (`setup_secrets.py`)

This project includes an interactive terminal setup tool, `setup_secrets.py`, to easily configure your Wi-Fi and network credentials in `main/secrets.h`.

Run the setup wizard:

```bash
# macOS / Linux:
python3 setup_secrets.py

# Windows:
python setup_secrets.py
```

The tool will prompt you for:
1. **Wi-Fi SSID & Password:** Your home 2.4 GHz Wi-Fi credentials.
2. **Tailscale Auth Key (Optional):** Pre-authenticated key from your Tailscale Admin Console. *(Leave empty if you only want to use local Wi-Fi).*
3. **Tailscale Device Name:** Name for your device on your Tailnet (default: `esp32`).
4. **Tailscale API Token & Primary Subnet Router (Optional):** Used for automated failover monitoring. *(Leave empty if you don't use this).*
5. **OTA Security Key:** A password or PIN to authorize future wireless updates. *(Leave empty to allow one-click updates without a password).*

---

### 4. Build and Flash the Firmware

Connect your ESP32 board to your computer using a USB cable.

#### Linux Setup
Ensure your user account has permission to access the serial port:
```bash
sudo usermod -a -G dialout $USER
# (Log out and log back in for this to take effect)
```

#### Compile and Flash

```bash
# 1. Activate the ESP-IDF environment
source ~/esp/esp-idf-v5.1.4/export.sh       # On Linux & macOS
# or on Windows: %userprofile%\esp\esp-idf-v5.1.4\export.bat

# 2. Build the firmware
idf.py build

# 3. Flash to your board and open the serial monitor:
# Linux example (replace with your port, e.g. /dev/ttyUSB0 or /dev/ttyACM0):
idf.py -p /dev/ttyUSB0 flash monitor

# macOS example:
idf.py -p /dev/cu.usbserial-0001 flash monitor

# Windows example:
idf.py -p COM3 flash monitor
```

*(Press `Ctrl + ]` to exit the serial monitor).*

---

## Initial Servo Calibration

On its very first boot, the ESP32 starts in a safe uncalibrated state so the servo arm will not move unexpectedly.

### Option A: Web Browser Calibration

1. Open your browser and go to `http://192.168.1.50/` or `http://esp32.local/`.
2. **Rest Angle:** Rotate the top dial to set where the arm rests when idle (hovering just above the button). The arm moves live as you adjust the dial.
3. **Press Angle:** Rotate the second dial to set how far the arm pushes down on the button.
4. **Press Duration:** Set how many milliseconds the arm holds the button down before releasing.
5. **Test Button:** Tap the test button to run a test press and confirm proper physical button actuation.
6. Tap **Save Calibration**. Your settings are saved and the device is ready to use.

### Option B: Terminal Calibration (`curl`)

You can also calibrate directly from your terminal:

```bash
bash <(curl -s http://192.168.1.50/calibrate)
```

Follow the on-screen steps to test angles live and save them.

---

## How to Use

### Browser Access

Navigate to any of these addresses in your browser:
* **Local Network:** `http://192.168.1.50/` or `http://esp32.local/`
* **Tailscale (Worldwide):** `http://esp32/`

### Terminal Access (`curl`)

Run this command in any terminal:

```bash
curl -s http://192.168.1.50/main | bash
```

Or over Tailscale:

```bash
curl -s http://esp32/main | bash
```

#### Terminal Menu Shortcuts:
* Press **`1`**: Trigger physical button press (or calibrate if not yet calibrated).
* Press **`2`**: Live device stats (temperature, uptime, memory, Wi-Fi, Tailscale).
* Press **`3`**: View crash logs, past reboots, downtime, and connection history.
* Press **`S`**: Recalibrate servo angles.
* Press **`C`**: Clear saved logs.
* Press **`O`**: Unlock Over-The-Air update window.
* Press **`R`**: Reboot the ESP32.
* Press **`X`**: Exit.

---

## Over-The-Air (OTA) Updates

Wirelessly update the ESP32 with new firmware versions or updated Wi-Fi/network credentials without plugging into a computer:

1. Unlock the OTA window from the Web UI (`/debug` page) or Terminal CLI (`[O]`). Enter your OTA key if configured.
2. The update window opens for **10 minutes**.
3. Upload new firmware wirelessly:
   ```bash
   python3 ~/.platformio/packages/framework-arduinoespressif32/tools/espota.py -i 192.168.1.50 -p 3232 -f build/ESP32-SwitchBot.bin
   ```
4. When finished, or after 10 minutes, the OTA port automatically locks itself again.

---

## Detailed Documentation

For detailed technical documentation on how the system works under the hood, see [documentation.md](documentation.md).

---

## License

This project is licensed under the [Apache 2.0 License](LICENSE).