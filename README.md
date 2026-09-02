# ESP32-S3 SwitchBot Dashboard

This firmware transforms an ESP32-S3 into a dual-interface smart switch actuator. It operates a physical servo to press buttons mechanically while hosting a highly optimized, asynchronous web server. The system features a modern web dashboard and a native MacOS/Linux terminal interface, comprehensive crash forensics, and aggressive thermal management.

### Core Features

* **Dual-UX Architecture:** Access the device via a standard web browser (HTML/JS/SVG) or through the terminal using `curl` to generate a live, interactive Bash dashboard.
* **Thermal & Power Optimization:** Hardware is strictly underclocked to 80 MHz and utilizes deep FreeRTOS yielding (20 ms idle loops) to run ice-cold while maintaining sub-second responsiveness.
* **Persistent Crash Forensics:** The NVS (Non-Volatile Storage) `Preferences` library records up to 50 rolling boot events, translating raw hardware watchdogs and exception panics into human-readable logs.
* **Live Telemetry:** Monitors real-time CPU clock, RAM/Flash/PSRAM usage, internal temperature, estimated power draw, and exact firmware initialization times.
* **OTA Updates:** Supports seamless Over-The-Air updates directly through the Arduino IDE.

### Hardware Requirements

* **Microcontroller:** ESP32-S3 (Tested on ESP32-S3-WROOM-N16R8 DOIT Dev Board).
* **Actuator:** Standard 5V/3.3V Servo Motor.
* **Wiring:** Connect the Servo Signal wire to **GPIO 1** (or your configured `servoPin`), with standard VCC and GND.

### Installation via Arduino IDE

1. **Board Manager Setup:**
* Go to **File > Preferences** and add the Espressif boards URL:
`[https://dl.espressif.com/dl/package_esp32_index.json](https://dl.espressif.com/dl/package_esp32_index.json)`
* Go to **Tools > Board > Boards Manager**, search for `esp32` by Espressif Systems, and install the latest version.


2. **Select the Board:**
* Navigate to **Tools > Board** and select **ESP32S3 Dev Module**.
* Set **Flash Mode** to `QIO 80MHz`.
* Set **Partition Scheme** to `Default 4MB with spiffs` (or match your specific board's flash size).


3. **Install Dependencies:**
* Go to **Sketch > Include Library > Manage Libraries**.
* Search for and install **ESP32Servo** by Kevin Harrington. *(Note: All other libraries like `WiFi`, `WebServer`, `Preferences`, and `ArduinoOTA` are built into the ESP32 core).*


4. **Compile and Flash:** Connect your board via USB, select the correct COM/Serial port, and upload the sketch.

### Configuration

Before uploading, modify the User Configuration block at the top of the `.ino` file to match your network and hardware mechanics:

```cpp
const char* ssid     = "WIFI_SSID";
const char* password = "WIFI_PASSWORD";
const char* deviceHostname = "esp32"; // Access via http://esp32.local

IPAddress local_IP(192, 168, 1, 50);  // Static IP Assignment
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);

const int servoPin   = 1;   
const int restAngle  = 180; // Idle clearance angle
const int pressAngle = 156; // Physical actuation angle

```

### Interfaces & Usage

**1. Web Browser Interface**
Navigate to `[http://esp32.local](http://esp32.local)` (or your static IP `[http://192.168.1.50](http://192.168.1.50)`) from any device on the network. The UI will stream live sensor updates asynchronously via hidden JSON endpoints.

**2. Terminal / Command Line Interface**
The ESP32 detects `curl` requests and serves a terminal-optimized Bash engine. To enable permanent single-keyword access on your Mac/Linux machine, add this alias to your `~/.zshrc` or `~/.bashrc` file:

```bash
alias esp-menu="curl -s http://esp32.local/main | bash"

```

Once saved and sourced (`source ~/.zshrc`), simply type **`esp-menu`** in your terminal to launch the interactive, live-updating command center. You can navigate the UI using single keystrokes (1-5, B, R, X) without pressing Enter.

### API Endpoints

| Route | Method | Description |
| --- | --- | --- |
| `/` | GET | Triggers the servo sequence. Includes a 2-second cooldown safeguard. |
| `/main` | GET | Delivers the Web UI hub or generates the interactive Terminal script. |
| `/info` | GET | Streams live hardware diagnostics (Temp, RAM, Power, Clock, Config). |
| `/debug` | GET | Displays the rolling array of the last 50 system resets and active uptimes. |
| `/clear-logs` | POST | Securely wipes the NVS flash memory, zeroes RAM arrays, and resets boot counts. |
| `/reboot` | POST | Executes a clean `ESP.restart()` after a 500 ms delay. |
| `/api/live` | GET | Internal JSON stream `{"u":"...","t":41.2,"ru":108...}` used by the web UI for async updates. |