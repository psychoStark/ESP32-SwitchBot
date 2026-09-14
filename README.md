# ESP32-S3 SwitchBot Dashboard with Embedded Tailscale VPN

Transform an **ESP32** into an encrypted smart switch actuator that physically pushes buttons (power switches, lights, appliances, or PC power buttons) on command. Control it locally over your home Wi-Fi or remotely from anywhere in the world via **Tailscale VPN**.

The device features two clean, zero-software interfaces:
* **Web Dashboard:** Open in any web browser on your phone, tablet, PC, or any device with a web browser (oh yes, even a refridgerator lol!)
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
  * **Minimum Requirement:** Any ESP32 development board with at least 4 MB Flash (8 MB or 16 MB recommended). *(For board-specific feature differences, see `documentation.md`).*
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
1. **Wi-Fi Networks (1 Primary + Up to 5 Fallback Networks):** Your primary Wi-Fi credentials, with the option to configure up to 5 additional fallback networks (e.g. backup router) that the ESP32 automatically cycles between if a connection drops.
2. **Operation Mode (Fully Local vs. Tailscale):** Choose between running fully local (completely bypassing Microlink and Tailscale to minimize CPU and RAM usage) or enabling Tailscale mesh VPN for global remote access.
3. **Tailscale Configuration (if remote mode selected):** Auth key, hostname, and optional subnet watchdog credentials.
4. **OTA Security Key:** A password or PIN to authorize future wireless updates. *(Leave empty to allow one-click updates without a password).*

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

## Initial Servo Setup & Horn Attachment

> [!IMPORTANT]
> **Do not attach or screw down the servo horn before powering on the ESP32!**
> Standard micro-servos (like the SG90 or MG90S) physically only sweep within a ~180° arc. Attaching the horn after zeroing ensures full mechanical travel in the desired direction.

### Step 1: Center the Servo Motor (90°)
1. Power on the ESP32 and open the calibration tool (via Web Browser at `http://192.168.1.50/` or terminal via `curl -s http://192.168.1.50/calibrate | bash`).
2. The initial default resting position is automatically set to **`90°`** (exact mechanical center).
3. When the calibration interface opens, the servo motor shaft rotates to its baseline **`90°`** center position.

### Step 2: Press Horn onto Splines (at 90°)
1. While the motor is held at its **`90°`** center position, take the servo horn (arm) and gently press it onto the splined gear shaft with your fingers pointing straight out towards your switch/button (roughly perpendicular to the servo casing).
2. The teeth (splines) will lock the horn at that exact angle, providing full travel flexibility in either direction across the 0° – 180° sweep.

### Step 3: Power Off & Tighten Screw
1. **Unplug or power off the ESP32** before tightening the screw.
2. **Hold the plastic horn firmly with your thumb and fingers** while tightening the center screw with your screwdriver. Holding the horn directly absorbs all screwdriver torque and prevents stripping or forcing the delicate internal gears.
3. Power the ESP32 back on.

---

## Servo Calibration

Once the horn is securely fastened at 90°, fine-tune your angles using either the Web Browser or the Terminal:

### Option A: Web Browser Calibration

1. Open your browser and go to `http://192.168.1.50/` or `http://esp32.local/` (or click **Servo Calibration** from the home screen).
2. **Rest Angle:** Adjust the dial (or tap the center number to type directly) so the horn hovers 1–2 mm just above the button without pressing it (idle state).
3. **Press Angle:** Adjust the second dial (or tap the center number to type) so the horn pushes down firmly on the switch/button. Make sure that the horn isn’t pushing down too hard causing the servo to buzz.
4. **Press Duration:** Set how many milliseconds (e.g. 200–400ms) the arm holds the button down before releasing.
5. **Test Tap:** Tap **Test Tap** to verify physical button actuation.
6. Tap **Save Calibration** to store your settings permanently to NVS flash.

### Option B: Terminal Calibration (`curl`)

You can also calibrate directly from any terminal:

```bash
bash <(curl -s http://192.168.1.50/calibrate)
```

Follow the on-screen prompts to adjust angles live, test actuation, and save.

---

## How to Use

### Browser Access

Navigate to any of these addresses in your browser:
* **Local Network:** `http://192.168.1.50/main` or `http://esp32.local/main`
* **Tailscale (Worldwide):** `http://esp32/main`

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
   python3 components/arduino/tools/espota.py -i 192.168.1.50 -p 3232 -f build/ESP32-SwitchBot.bin
   ```
4. When finished, or after 10 minutes, the OTA port automatically locks itself again.

---

## Detailed Documentation

For detailed technical documentation on how the system works under the hood, see [documentation.md](documentation.md).

---

## License

This project is licensed under the [Apache 2.0 License](LICENSE).

---

**Current Version:** `v1.2`