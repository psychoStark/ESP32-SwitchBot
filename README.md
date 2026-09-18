# DIY ESP32-SwitchBot: Remote Physical Button Pusher & PC Power Switch with Embedded Tailscale VPN

> **An industrial-grade, open-source mechanical switch actuator and DIY fingerbot.** Physically pushes buttons (PC power buttons, wall switches, light toggles, coffee makers, or appliances) on command. Control it locally over your home Wi-Fi or remotely from anywhere in the world via **embedded Tailscale WireGuard VPN** — without port forwarding, dynamic DNS, or third-party cloud subscriptions.

The device features three zero-software interfaces:
* **Progressive Web App (PWA) Dashboard:** Installable directly to your home screen on iOS Safari, Android, and Desktop (Chrome/Edge/macOS) with standalone display, dark theme, offline precaching, and native `⚡` icon.
* **Terminal cURL TUI:** Streams a full-screen interactive Bash interface directly in any terminal using a single `curl` command.
* **Zero-Heap REST API & Webhooks:** Seamlessly integrate with Home Assistant, webhooks, iOS Shortcuts, Tasker, Node-RED, or shell scripts.

---

## Features

* **Global Remote Access without Port Forwarding:** Control your SwitchBot locally over your home Wi-Fi network without any cloud dependencies, or trigger it securely from anywhere on Earth via embedded Tailscale WireGuard mesh VPN.
* **Turn On Your PC Remotely (100% Reliable WoL Alternative):** No more failed Wake-on-LAN packets across routers or after power outages. Physically presses the computer power button.
* **Cross-Platform PWA Support:** Add to your phone's home screen with standalone app-like experience, safe-area support, and tactile haptics.
* **Cloud Watchdog & Automatic Cold Standby:** Keeps the ESP32 cool (~38–41°C) and power-efficient (~0.14 W) when your primary home network router is online, and automatically takes over if your primary router goes offline.
* **Interactive Servo Calibration:** Easily adjust button pressing angles and durations using live rotary dials in your browser or a guided terminal wizard.
* **Instant Sub-5ms Button Response:** Sends an immediate confirmation when clicked, so high-latency cellular connections never stall waiting for physical motor movement.
* **Activity Forensics & Power Outage Downtime:** Non-volatile ring buffers keep track of past button presses, boot causes, power outage downtime, and connection history.
* **Over-The-Air (OTA) Updates:** Wirelessly flash new firmware updates over Wi-Fi without needing a physical USB connection.

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

## 💡 Why ESP32-SwitchBot? (Comparison & Problems Solved)

If you have ever tried to automate turning on your PC or appliances remotely, you've likely encountered the limitations of existing solutions:

### 1. How to Turn On My PC From Anywhere in the World (Wake-on-LAN Alternative)
* **The Problem with Wake-on-LAN (WoL):** WoL magic packets frequently fail to cross subnets, Wi-Fi bridges, or the public internet. After a sudden power cut or hard shutdown, modern motherboards often leave the NIC unpowered in S5 state, rendering Wake-on-LAN completely dead. Setting up WoL over the internet usually requires risky router port forwarding or running an always-on Raspberry Pi sending wake on lan packets.
* **The SwitchBot Solution:** ESP32-SwitchBot physically pushes the actual computer power button with a mechanical micro servo arm (SG90/MG90S). Because it connects directly to your private mesh via **Tailscale on ESP32**, you can turn on your PC from anywhere in the world without port forwarding, dynamic DNS, or complex subnet bridges.

### 2. Mechanical Servo Presser vs. Motherboard Header Relays & Optocouplers
* **The Risk of Motherboard Relays & Optocouplers:** Many DIY guides recommend wiring an optocoupler, relay module, or ESP8266 to the motherboard front panel power switch header pins (`PWR_SW`). This requires opening the PC chassis, stripping wires, risking 5V/12V ground loop shorts, and potentially voiding manufacturer warranties on expensive motherboards.
* **The Non-Invasive Alternative:** A 3D-printed switchbot servo mount attached above the power button acts as a completely non-invasive mechanical fingerbot. No electrical modification to the PC is required.

### 3. Overcoming Smart Plug & BIOS AC Power Loss Limitations
* **The Smart Plug Trap:** Setting your BIOS to *"Restore on AC Power Loss"* paired with a Sonoff/Tuya smart plug only works if power was cut while the computer was running. If the PC was cleanly shut down, turning the smart plug off and back on does **not** boot the computer.
* **The SwitchBot Solution:** The ESP32 physically presses the power button on demand, providing full power control regardless of prior AC state.

### 4. ESPHome, Tasmota, Blynk, Sinric Pro & Cloudflare Alternative
* **No Home Assistant Hub or MQTT Broker Needed:** Unlike ESPHome or Zigbee switchbots that require an always-on Home Assistant server or local Zigbee dongles, ESP32-SwitchBot is completely standalone.
* **No Cloud Subscriptions or Device Quotas:** Avoid proprietary cloud apps (Blynk, Sinric Pro, Tuya) that charge monthly subscriptions or limit API calls.
* **Zero Port Forwarding or Tunnels:** Unlike standard Arduino IDE web servers or Cloudflare tunnels / ZeroTier that require server daemons, the ESP32 runs a modified [microlink](https://github.com/CamM2325/microlink) WireGuard client natively with automatic cold-standby failover.

### 5. 3D Printed Servo Mounts & Mechanical Setup
* Compatible with standard TowerPro SG90 and MG90S metal gear micro servos.
* Compatible with standard open-source 3D printed switchbot mounts and mechanical button pusher STL files available on Printables and Thingiverse (search: *3D printed switchbot servo mount*, *SG90 mechanical button presser*, or *DIY fingerbot*).

---

## 🔍 Search & Discovery Index

This project was built to address common automation searches and challenges:
- **DIY SwitchBot & Fingerbot:** *diy switchbot, automatic switch, automatic switch with mobile control, how to make a switchbot, how to make a remote switchbot, diy fingerbot esp32, 3d printed switchbot servo mount, esp32 sg90 mechanical button presser, servo switch presser 3d print, micro servo push power button pc, solenoid button pusher esp8266, diy physical button pusher remote control.*
- **Remote PC Power & WoL Alternatives:** *turn my pc on remotely, how to turn on my pc from anywhere, how to turn on my pc from anywhere in the world, switchbot which can be triggered remotely, switchbot which can be triggered from anywhere, wake on lan over internet without port forwarding, wol over tailscale, raspberry pi wake on lan tailscale, esp32 wake on lan packet sender remote, bios ac power loss restart smart plug, restore on ac power loss sonoff pc turn on.*
- **Hardware Modification Alternatives:** *esp32 motherboard power switch header relay, optocoupler pc power button pins, diy smart power button optocoupler esp32, turn on pc via motherboard front panel pins esp8266.*
- **Tailscale & WireGuard IoT:** *esp with tailscale, tailscale on esp32, switchbot with tailscale, diy switchbot with tailscale, control my esp32 from anywhere, esp32 wireguard client remote trigger, zerotier esp32 alternative, cloudflared tunnel esp32 web server.*
- **Firmware & Framework Alternatives:** *switchbot without esphome, esphome alternative, tasmota web switch remote trigger, tasmota servo button pusher, blynk esp32 remote button pusher, sinric pro esp32 pc power switch, mqtt broker control esp32 from anywhere, arduino ide esp32 remote web server, esp32 webhook push notification trigger, home assistant remote switchbot zigbee.*

---

## Detailed Documentation

For detailed technical documentation on how the system works under the hood, see [documentation.md](documentation.md).

---

## License

This project is licensed under the [Apache 2.0 License](LICENSE).

---

**Current Version:** `v1.2`