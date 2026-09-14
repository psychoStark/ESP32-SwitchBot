# Getting Started

The **ESP32-SwitchBot** turns an Espressif ESP32 into an encrypted, remotely-accessible switch actuator. It mechanically actuates physical buttons (PC power buttons, wall switches, coffee machines, or appliances) upon authenticated command.

---

## Hardware Bill of Materials

| Component | Specification | Notes |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 (DOIT N16R8 Recommended) | 16 MB Flash, 8 MB Octal PSRAM. Compatible with classic ESP32, ESP32-C3, and S2. |
| **Actuator** | Micro Servo (TowerPro SG90 / MG90S) | Standard 3.3V – 5V analog servo (180° sweep). |
| **Power Source** | 5V USB-C Power Adapter | Clean 5V / 1A+ supply. |
| **Wiring** | 3 Dupont Jumper Wires | Signal, 5V (VCC), and Ground (GND). |

---

## Supported Boards Matrix

| Board / SoC Variant | Flash | PSRAM | Temp Sensor | Concurrency Architecture |
| :--- | :--- | :--- | :--- | :--- |
| **ESP32-S3 (N16R8)** *(Primary)* | 16 MB Octal | 8 MB Octal | Native On-Die (`SOC_TEMP_SENSOR_SUPPORTED`) | Dual-core segregated (Core 0: Net, Core 1: Servo) |
| **ESP32-S3 (N8R2 / Generic)** | 8 MB Quad | 2 MB Quad | Native On-Die | Dual-core segregated |
| **Classic ESP32 (WROOM-32)** | 4 MB / 8 MB | Optional SPI | Graceful Fallback (`-` placeholder) | Dual-core segregated |
| **ESP32-C3 (RISC-V)** | 4 MB | None | Native On-Die | Single-core cooperative time-sliced |
| **ESP32-S2** | 4 MB | Optional SPI | Native On-Die | Single-core cooperative time-sliced |

---

## Wiring & Pinout

Connect the 3 servo leads to the ESP32 board as follows:

```text
  ESP32-S3 Board          Micro Servo (SG90)
 ┌──────────────┐        ┌──────────────────┐
 │          5V  ├────────┤ Red (VCC / 5V)   │
 │         GND  ├────────┤ Brown (GND)      │
 │ GPIO 1 (PWM) ├────────┤ Orange (Signal)  │
 └──────────────┘        └──────────────────┘
```

> [!TIP]
> While GPIO 1 is configured by default in `main/main.cpp` (`const int servoPin = 1;`), you can change it to any general-purpose PWM output pin on your board (avoiding strapping pins like GPIO 0, 45, or 46).

---

## Initial Setup & Flashing

### Step 1: Install Toolchain Prerequisites
You can compile and flash the firmware using standard **ESP-IDF v5.1+**:

```bash
# Export ESP-IDF environment (v5.1.4 recommended)
. $HOME/esp/esp-idf-v5.1.4/export.sh
```

### Step 2: Configure Secrets
Run the interactive configuration CLI to set up Wi-Fi, Tailscale, and security keys:

```bash
python3 setup_secrets.py
```

### Step 3: Compile and Flash over USB
Connect your ESP32 to your computer via a data-capable USB-C cable:

```bash
# Set target chip (if first time)
idf.py set-target esp32s3

# Build, flash, and open serial monitor
idf.py -p /dev/tty.usbmodem* flash monitor
```

Once booted, the ESP32 will connect to your local Wi-Fi, report its IP address (e.g. `192.168.1.50`), and start the web dashboard!
