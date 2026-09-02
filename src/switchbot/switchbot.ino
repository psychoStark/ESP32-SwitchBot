#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_system.h>
#include <time.h>

// ============================================================================
// ===== USER CONFIGURATION: MODIFY THESE SETTINGS FOR YOUR SCENARIO =====
// ============================================================================

// 1. WiFi Network Credentials
// Replace these with your actual home/office WiFi network details
const char* ssid     = "WIFI_SSID";          // [CHANGE ME] Your WiFi name (SSID)
const char* password = "WIFI_PASSWORD";      // [CHANGE ME] Your WiFi password

// 2. Network Identity (mDNS and OTA)
// This is the name you'll use to access the device (e.g., http://esp32.local)
const char* deviceHostname = "esp32";        // [CHANGE ME] Hostname for the device

// 3. Static IP Configuration
// Using a static IP ensures the device address never changes.
// Adjust these values to fit your router's network range (usually 192.168.1.x or 192.168.0.x)
IPAddress local_IP(192, 168, 1, 50);         // [CHANGE ME] Desired fixed IP for ESP32
IPAddress gateway(192, 168, 1, 1);           // [CHANGE ME] Router's IP address (Gateway)
IPAddress subnet(255, 255, 255, 0);          // [CHANGE ME] Subnet mask (usually 255.255.255.0)
IPAddress primaryDNS(192, 168, 1, 1);        // [CHANGE ME] DNS server (usually same as gateway)

// 4. Servo Motor Hardware Settings
const int servoPin   = 1;                    // [CHANGE ME] GPIO pin connected to the servo signal wire
const int restAngle  = 0;                    // [CHANGE ME] Idle angle (rest position, e.g., 180 or 0)
const int pressAngle = 30;                   // [CHANGE ME] Action angle (physically presses the button)
const int holdTimeMs = 400;                  // [CHANGE ME] How long to hold the press (milliseconds)

// 5. Time Zone and NTP Settings
// GMT Offset in seconds: (Hours * 3600)
// Examples: GMT+5:30 = 19800, GMT-5 (EST) = -18000, GMT+1 (CET) = 3600
const long gmtOffset_sec = 19800;            // [CHANGE ME] Your timezone offset in seconds
const int daylightOffset_sec = 0;            // [CHANGE ME] Daylight savings offset (usually 0 or 3600)

// 6. Security and Safety
const uint32_t PRESS_COOLDOWN_MS = 2000;     // [CHANGE ME] Cooldown between presses to prevent spam/damage

// ============================================================================
// ===== END OF USER CONFIGURATION - DO NOT MODIFY BELOW THIS LINE =====
// ============================================================================

// Hardware objects
Servo myservo;                               // Servo motor controller object
WebServer server(80);                        // Web server listening on port 80 (HTTP)
Preferences prefs;                           // Persistent storage (flash memory) for logs using ESP32's NVS

// Timing and state variables
uint32_t last_press_time = 0;                // Timestamp of last button press (milliseconds since boot)

// Time tracking variables
uint32_t first_boot_epoch = 0;               // First boot timestamp (Unix time, set after NTP sync)
uint32_t boot_time_ms = 0;                   // Time spent in firmware initialization (milliseconds)
uint32_t wifi_connect_ms = 0;                // Time spent connecting to WiFi (milliseconds)
bool time_synced = false;                    // Flag: true when we've successfully synchronized with NTP
bool log_time_fixed = false;                 // Flag: true when we've corrected invalid boot timestamps

// Crash logging configuration
const int MAX_BOOT_LOGS = 50;                // Maximum number of boot events to store in circular buffer
struct BootLog {
  uint32_t timestamp;                        // Unix timestamp of boot event (seconds since Jan 1 1970)
  char reason[32];                           // Text description of reset reason (null-terminated string)
};
BootLog bootHistory[MAX_BOOT_LOGS];          // Circular buffer storing boot history (newest at index 0)
uint32_t totalBootCount = 0;                 // Total number of boots recorded since first flash

// --- Helper Functions ---

// Detects if request came from cURL (terminal) vs web browser
bool isCurl() {
  return server.header("User-Agent").indexOf("curl") >= 0;
}

// Initialize servo motor with proper PWM settings
void initServo() {
  ESP32PWM::allocateTimer(0);  // Allocate PWM channels for servo control
  ESP32PWM::allocateTimer(1);
  myservo.setPeriodHertz(50);  // Standard servo frequency (50Hz = 20ms period)
}

// Execute servo press sequence: move to press angle, hold, return to rest
void triggerPress() {
  myservo.attach(servoPin, 500, 2400);  // Attach servo with pulse width range (500-2400 microseconds)
  myservo.write(pressAngle);            // Move to button press position
  delay(holdTimeMs);                    // Hold press for set duration
  myservo.write(restAngle);             // Return to rest position
  delay(300);                           // Allow servo to settle before detaching
  myservo.detach();                     // Detach to prevent jitter and save power when idle
}

// Estimate power consumption based on CPU frequency and WiFi state
float getEstimatedPowerW() {
  float current_mA = 15.0;              // Base current draw of ESP32-S3
  if (ESP.getCpuFreqMHz() == 80) current_mA += 12.0;   // 80MHz adds ~12mA
  else if (ESP.getCpuFreqMHz() == 240) current_mA += 30.0; // 240MHz adds ~30mA
  if (WiFi.status() == WL_CONNECTED) current_mA += 80.0;   // WiFi adds ~80mA when connected
  return (current_mA * 3.3) / 1000.0;   // Convert mA*V to Watts (P = I * V)
}

// Convert ESP32 reset reason enum to human-readable string
const char* getResetReasonString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "Power-On / Power Cut";
    case ESP_RST_EXT:       return "External Pin Reset";
    case ESP_RST_SW:        return "Software Reset (OTA/Code)";
    case ESP_RST_PANIC:     return "CRASH / Exception Panic";
    case ESP_RST_INT_WDT:   return "Interrupt Watchdog (Hung)";
    case ESP_RST_TASK_WDT:  return "Task Watchdog (Deadlock)";
    case ESP_RST_WDT:       return "Other Watchdog Reset";
    case ESP_RST_DEEPSLEEP: return "Deep Sleep Wake";
    case ESP_RST_BROWNOUT:  return "BROWNOUT (Voltage Dip)";
    case ESP_RST_SDIO:      return "SDIO Reset";
    default:                return "Unknown Reset";
  }
}

// Record boot event in persistent storage
void recordBootEvent() {
  prefs.begin("esp_log", false);
  totalBootCount = prefs.getUInt("boot_cnt", 0) + 1;
  prefs.putUInt("boot_cnt", totalBootCount);
  prefs.getBytes("boot_log", bootHistory, sizeof(bootHistory));

  for (int i = MAX_BOOT_LOGS - 1; i > 0; i--) {
    bootHistory[i] = bootHistory[i - 1];
  }

  time_t now; time(&now);
  bootHistory[0].timestamp = (now > 1600000000UL) ? (uint32_t)now : 0;
  snprintf(bootHistory[0].reason, sizeof(bootHistory[0].reason), "%s", getResetReasonString(esp_reset_reason()));

  prefs.putBytes("boot_log", bootHistory, sizeof(bootHistory));
  first_boot_epoch = prefs.getUInt("first_boot", 0);
  prefs.end();
}

// Synchronize time with NTP servers when WiFi is available
void syncTimeIfNeeded() {
  if (!time_synced && WiFi.status() == WL_CONNECTED) {
    time_t now; time(&now);
    
    if (now > 1600000000UL) {
      time_synced = true;
      
      prefs.begin("esp_log", false);
      if (first_boot_epoch == 0) {
        first_boot_epoch = (uint32_t)now - (millis() / 1000);
        prefs.putUInt("first_boot", first_boot_epoch);
      }
      
      if (!log_time_fixed && bootHistory[0].timestamp < 1600000000UL) {
        bootHistory[0].timestamp = (uint32_t)now - (millis() / 1000);
        prefs.putBytes("boot_log", bootHistory, sizeof(bootHistory));
      }
      prefs.end();
      log_time_fixed = true;
    }
  }
}

// Format Unix timestamp as human-readable date/time string
String formatTimestamp(uint32_t epoch) {
  if (epoch < 1600000000UL) return "Awaiting NTP Sync...";
  time_t t_epoch = epoch; 
  struct tm timeinfo;
  localtime_r(&t_epoch, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%I:%M:%S %p %d-%b-%Y", &timeinfo);
  return String(buf);
}

// Format duration in seconds as human-readable string
void formatDuration(uint64_t totalSec, char* buffer, size_t maxLen) {
  if (totalSec == 0) { snprintf(buffer, maxLen, "0s"); return; }
  uint64_t d = totalSec / 86400ULL; totalSec %= 86400ULL;
  uint64_t h = totalSec / 3600ULL;  totalSec %= 3600ULL;
  uint64_t m = totalSec / 60ULL;    uint64_t s = totalSec % 60ULL;

  int offset = 0;
  if (d > 0) offset += snprintf(buffer + offset, maxLen - offset, "%lud ", (unsigned long)d);
  if (h > 0) offset += snprintf(buffer + offset, maxLen - offset, "%luh ", (unsigned long)h);
  if (m > 0) offset += snprintf(buffer + offset, maxLen - offset, "%lum ", (unsigned long)m);
  if (s > 0 || offset == 0) offset += snprintf(buffer + offset, maxLen - offset, "%lus", (unsigned long)s);
}

// --- Routes ---

void handleRoot() {
  uint32_t now = millis();
  if (now - last_press_time < PRESS_COOLDOWN_MS) {
    server.send(429, "text/plain; charset=utf-8", "Cooldown active."); return;
  }
  last_press_time = now;
  triggerPress();

  char upBuf[64]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));

  if (isCurl()) {
    char out[256];
    snprintf(out, sizeof(out), "\n[+] SUCCESS: Servo tap completed.\n[i] ESP Uptime: %s\n\n", upBuf);
    server.send(200, "text/plain; charset=utf-8", out);
  } else {
    String html;
    html.reserve(1024);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>SwitchBot Trigger</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E⚡%3C/text%3E%3C/svg%3E'>";
    html += "<script>setInterval(()=>fetch('/api/live').then(r=>r.json()).then(d=>{document.getElementById('up').innerText=d.u;}), 1000);</script></head>";
    html += "<body style='background:#11111b;color:#a6e3a1;text-align:center;font-family:sans-serif;padding-top:50px;'>";
    html += "<h2>⚡ SUCCESS: Servo tap completed</h2>";
    html += "<p style='color:#cdd6f4;'>ESP Uptime: <span id='up'>" + String(upBuf) + "</span></p>";
    html += "<a href='/main' style='color:#89b4fa;text-decoration:none;'>Go to Dashboard &rarr;</a></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  }
}

void handleMain() {
  if (isCurl()) {
    String host = server.header("Host");
    if (host.length() == 0) host = String(deviceHostname) + ".local";

    char bashScript[3000];
    snprintf(bashScript, sizeof(bashScript),
      "#!/bin/bash\n"
      "HOST=\"%s\"\n"
      "while true; do\n"
      "  clear\n"
      "  echo -e '\\n=== SwitchBot Dashboard ==='\n"
      "  echo ' [1] Trigger Power Button'\n"
      "  echo ' [2] Device Info (Live)'\n"
      "  echo ' [3] Crash Logs & Debug'\n"
      "  echo ' [4] Reboot ESP32'\n"
      "  echo ' [5] Clear Logs'\n"
      "  echo ' [X] Exit'\n"
      "  echo ''\n"
      "  opt=\"\"\n"
      "  read -n 1 -s -p 'Select an option: ' opt </dev/tty\n"
      "  echo ''\n"
      "  if [[ \"$opt\" == \"x\" || \"$opt\" == \"X\" ]]; then echo \"Exiting.\"; break; fi\n"
      "  if [[ \"$opt\" == \"1\" ]]; then\n"
      "    curl -s http://$HOST/\n"
      "    sleep 1.5\n"
      "  elif [[ \"$opt\" == \"2\" ]]; then\n"
      "    clear\n"
      "    while true; do\n"
      "      OUT=$(curl -s http://$HOST/info)\n"
      "      echo -ne \"\\033[H$OUT\\n [B] Back to Menu | [R] Reboot | [X] Exit\\033[K\\n\"\n"
      "      key=\"\"\n"
      "      read -t 1 -n 1 -s key </dev/tty\n"
      "      if [[ \"$key\" == \"b\" || \"$key\" == \"B\" ]]; then break; fi\n"
      "      if [[ \"$key\" == \"x\" || \"$key\" == \"X\" ]]; then echo \"\"; exit 0; fi\n"
      "      if [[ \"$key\" == \"r\" || \"$key\" == \"R\" ]]; then curl -X POST -s http://$HOST/reboot; exit 0; fi\n"
      "    done\n"
      "  elif [[ \"$opt\" == \"3\" ]]; then\n"
      "    clear\n"
      "    while true; do\n"
      "      OUT=$(curl -s http://$HOST/debug)\n"
      "      echo -ne \"\\033[H$OUT\\n [B] Back to Menu | [C] Clear Logs | [R] Reboot | [X] Exit\\033[K\\n\"\n"
      "      key=\"\"\n"
      "      read -t 1 -n 1 -s key </dev/tty\n"
      "      if [[ \"$key\" == \"b\" || \"$key\" == \"B\" ]]; then break; fi\n"
      "      if [[ \"$key\" == \"x\" || \"$key\" == \"X\" ]]; then echo \"\"; exit 0; fi\n"
      "      if [[ \"$key\" == \"r\" || \"$key\" == \"R\" ]]; then curl -X POST -s http://$HOST/reboot; exit 0; fi\n"
      "      if [[ \"$key\" == \"c\" || \"$key\" == \"C\" ]]; then curl -X POST -s http://$HOST/clear-logs; sleep 1.5; break; fi\n"
      "    done\n"
      "  elif [[ \"$opt\" == \"4\" ]]; then\n"
      "    curl -X POST -s http://$HOST/reboot\n"
      "    exit 0\n"
      "  elif [[ \"$opt\" == \"5\" ]]; then\n"
      "    curl -X POST -s http://$HOST/clear-logs\n"
      "    sleep 1.5\n"
      "  fi\n"
      "done\n",
      host.c_str()
    );
    server.send(200, "text/plain; charset=utf-8", bashScript);
  } else {
    String html;
    html.reserve(1024);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>SwitchBot Dashboard</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E🎛️%3C/text%3E%3C/svg%3E'>";
    html += "<style>body{background:#11111b;color:#cdd6f4;font-family:sans-serif;text-align:center;padding:20px;} ";
    html += "a{display:block;background:#1e1e2e;color:#89b4fa;padding:15px;margin:10px auto;max-width:300px;border-radius:8px;text-decoration:none; border:1px solid #313244;}</style></head><body>";
    html += "<h1>🎛️ SwitchBot Dashboard</h1>";
    html += "<a href='/' style='color:#a6e3a1;'>⚡ Trigger Power Button</a>";
    html += "<a href='/info'>💻 Device Info</a>";
    html += "<a href='/debug'>🛠️ Crash Logs & Debug</a>";
    html += "</body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  }
}

void handleInfo() {
  char upBuf[64]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));
  uint32_t ramTotal = ESP.getHeapSize() / 1024;
  uint32_t ramFree = ESP.getFreeHeap() / 1024;
  uint32_t flashTotal = ESP.getFlashChipSize() / 1024;
  uint32_t flashUsed = ESP.getSketchSize() / 1024;
  uint32_t psramTotal = ESP.getPsramSize() / 1024;
  uint32_t psramFree = ESP.getFreePsram() / 1024;

  if (isCurl()) {
    char out[1024];
    snprintf(out, sizeof(out),
      "==================================================\n"
      " [i] ESP32-S3 DEVICE INFO\n"
      "==================================================\n"
      " Uptime     : %-15s | Temp  : %.1f C\n"
      " CPU Clock  : %u MHz          | Power : ~%.2f W\n"
      " Device     : ESP32-S3-WROOM-N16R8 DOIT\n"
      "--------------------------------------------------\n"
      " RAM Used   : %u/%u KB\n"
      " Flash Used : %u/%u KB\n"
      " PSRAM Used : %u/%u KB\n"
      "--------------------------------------------------\n"
      " Wi-Fi SSID : %s\n"
      " IP Address : %s (%s.local)\n"
      " Firmware   : Core 1 | QIO 80MHz | SPIFFS 4MB\n"
      "==================================================",
      upBuf, temperatureRead(),
      ESP.getCpuFreqMHz(), getEstimatedPowerW(),
      (ramTotal - ramFree), ramTotal,
      flashUsed, flashTotal,
      (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), deviceHostname
    );
    server.send(200, "text/plain; charset=utf-8", out);
  } else {
    String html;
    html.reserve(2560);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Device Info</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E💻%3C/text%3E%3C/svg%3E'>";
    html += "<style>body{background:#11111b;color:#cdd6f4;font-family:monospace;padding:20px;line-height:1.6;} h3{color:#89b4fa;border-bottom:1px solid #313244;padding-bottom:5px;} span{color:#a6e3a1;}</style>";
    html += "<script>setInterval(()=>fetch('/api/live').then(r=>r.json()).then(d=>{document.getElementById('up').innerText=d.u; document.getElementById('tmp').innerHTML=d.t+' &deg;C'; document.getElementById('pwr').innerText='~'+d.p+' W'; document.getElementById('ram').innerText=d.ru+'/'+d.rt+' KB'; document.getElementById('clk').innerText=d.c+' MHz';}), 1000);</script></head><body>";
    
    html += "<h2>💻 Device Info</h2><h3>Live Sensors</h3>Uptime: <span id='up'>Loading...</span><br>CPU Temp: <span id='tmp'>Loading...</span><br>CPU Clock: <span id='clk'>Loading...</span><br>Est. Power Draw: <span id='pwr'>Loading...</span><br>";
    
    char buf[1024];
    snprintf(buf, sizeof(buf),
      "<h3>Hardware Info</h3>Device Name: <span>ESP32-S3-WROOM-N16R8 DOIT Dev Board</span><br>Manufacturer: <span>Espressif Systems</span><br>"
      "<h3>Memory Capacity</h3>RAM: <span id='ram'>Loading...</span><br>Flash: <span>%u/%u KB</span><br>PSRAM: <span>%u/%u KB</span><br>"
      "<h3>Network & IDE Settings</h3>Wi-Fi SSID: <span>%s</span><br>IP Address: <span>%s</span><br>Hostname: <span>%s.local</span><br>"
      "Partition Scheme: <span>Default 4MB with SPIFFS</span><br>Flash Mode: <span>QIO 80MHz</span><br>Arduino Runs On: <span>Core 1</span><br>USB CDC: <span>Disabled</span><br>",
      flashUsed, flashTotal, (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), deviceHostname
    );
    html += buf;
    html += "<br><a href='/main' style='color:#89b4fa;text-decoration:none;'>&larr; Back to Dashboard</a></body></html>";
    
    server.send(200, "text/html; charset=utf-8", html);
  }
}

void handleApiLive() {
  char upBuf[64]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));
  
  char flashBuf[64] = "Awaiting NTP Sync...";
  if (time_synced && first_boot_epoch > 0) {
    time_t now; time(&now);
    if (now > first_boot_epoch) formatDuration(now - first_boot_epoch, flashBuf, sizeof(flashBuf));
  }
  
  uint32_t ramTotal = ESP.getHeapSize() / 1024;
  uint32_t ramFree = ESP.getFreeHeap() / 1024;
  
  char json[300];
  snprintf(json, sizeof(json), "{\"u\":\"%s\",\"uf\":\"%s\",\"t\":%.1f,\"ru\":%u,\"rt\":%u,\"c\":%u,\"p\":%.2f}", 
    upBuf, flashBuf, temperatureRead(), (ramTotal - ramFree), ramTotal, ESP.getCpuFreqMHz(), getEstimatedPowerW());
  server.send(200, "application/json", json);
}

void handleDebug() {
  syncTimeIfNeeded();
  
  char flashDuration[64] = "Awaiting NTP Sync...";
  if (time_synced && first_boot_epoch > 0) {
    time_t now; time(&now);
    if (now > first_boot_epoch) {
      formatDuration(now - first_boot_epoch, flashDuration, sizeof(flashDuration));
    }
  }

  char bootDuration[64];
  formatDuration(esp_timer_get_time() / 1000000ULL, bootDuration, sizeof(bootDuration));

  String lastResetCause = "N/A";
  String lastResetTime = "N/A";

  if (totalBootCount > 0 && bootHistory[0].timestamp > 0) {
      lastResetCause = String(bootHistory[0].reason);
      lastResetTime = formatTimestamp(bootHistory[0].timestamp);
  }

  bool hasHistory = false;
  int limitLogs = isCurl() ? 6 : MAX_BOOT_LOGS; 
  
  if (isCurl()) {
    String finalOut;
    finalOut.reserve(2048);
    
    char out[1024];
    snprintf(out, sizeof(out),
      "==================================================\n"
      " [!] ESP32-S3 CRASH LOGS & DEBUG\n"
      "==================================================\n"
      " Uptime Since Boot    : %s\n"
      " Uptime Since Flash   : %s\n"
      " Boot Time            : %u ms\n"
      " Wi-Fi Connect Time   : %u ms\n"
      " Total Boot Count     : %u\n",
      bootDuration, flashDuration, boot_time_ms, wifi_connect_ms, totalBootCount
    );
    finalOut += out;

    if (totalBootCount > 0) {
      char resetInfo[512];
      snprintf(resetInfo, sizeof(resetInfo),
        " Last Reset Cause     : %s\n"
        " Last Reset Time      : %s\n\n",
        lastResetCause.c_str(), lastResetTime.c_str()
      );
      finalOut += resetInfo;
    } else {
      finalOut += " Log Status         : Clean (0 Resets)\n\n";
    }

    for (int i = 1; i < limitLogs; i++) {
      if (bootHistory[i].timestamp == 0) continue;
      if (!hasHistory) {
        finalOut += " [ PREVIOUS BOOT HISTORY ]\n";
        hasHistory = true;
      }
      char line[128];
      snprintf(line, sizeof(line), " [%s] %s\n", formatTimestamp(bootHistory[i].timestamp).c_str(), bootHistory[i].reason);
      finalOut += line;
    }
    
    finalOut += "==================================================";
    server.send(200, "text/plain; charset=utf-8", finalOut);
    
  } else {
    String html;
    html.reserve(4096);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
    html += "<title>Crash Logs & Debug</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E🛠️%3C/text%3E%3C/svg%3E'>";
    html += "<script>setInterval(()=>fetch('/api/live').then(r=>r.json()).then(d=>{document.getElementById('up').innerText=d.u; document.getElementById('upf').innerText=d.uf;}), 1000);</script></head>";
    html += "<body style='background:#11111b;color:#cdd6f4;font-family:monospace;padding:20px;'>";
    
    char top[1024];
    snprintf(top, sizeof(top), 
      "<h2 style='color:#89b4fa;'>🛠️ Crash Logs & Debug</h2>"
      "<b>Uptime Since Boot:</b> <span id='up' style='color:#a6e3a1;'>%s</span><br>"
      "<b>Uptime Since Flash:</b> <span id='upf' style='color:#a6e3a1;'>%s</span><br>"
      "<b>Boot Time:</b> %u ms<br>"
      "<b>Wi-Fi Connect Time:</b> %u ms<br><br>"
      "<b>Total Boot Count:</b> %u<br>",
      bootDuration, flashDuration, boot_time_ms, wifi_connect_ms, totalBootCount
    );
    html += top;

    if (totalBootCount > 0) {
      char resetData[512];
      snprintf(resetData, sizeof(resetData),
        "<b>Last Reset Cause:</b> %s<br>"
        "<b>Last Reset Time:</b> <span style='color:#f38ba8;'>%s</span><br><br>",
        lastResetCause.c_str(), lastResetTime.c_str()
      );
      html += resetData;
    } else {
      html += "<b>Log Status:</b> <span style='color:#a6e3a1;'>Clean (0 Resets)</span><br><br>";
    }
    
    for (int i = 1; i < limitLogs; i++) {
      if (bootHistory[i].timestamp == 0) continue;
      if (!hasHistory) {
        html += "<h3 style='border-bottom:1px solid #313244;'>Previous Boot History</h3><pre style='color:#f9e2af;white-space:pre-wrap;'>";
        hasHistory = true;
      }
      char line[128];
      snprintf(line, sizeof(line), " [%s] %s\n", formatTimestamp(bootHistory[i].timestamp).c_str(), bootHistory[i].reason);
      html += line;
    }
    
    if (hasHistory) { html += "</pre><br>"; }
    
    html += "<form action='/reboot' method='POST' style='display:inline;'><button type='submit' style='background:#f38ba8;color:#11111b;border:none;padding:8px 16px;border-radius:6px;font-weight:bold;cursor:pointer;margin-right:15px;'>🔄 Reboot ESP32</button></form>";
    html += "<form action='/clear-logs' method='POST' style='display:inline;' onsubmit='return confirm(\"Are you sure you want to clear all crash history and flash timers?\");'><button type='submit' style='background:#fab387;color:#11111b;border:none;padding:8px 16px;border-radius:6px;font-weight:bold;cursor:pointer;margin-right:15px;'>🗑️ Clear Logs</button></form>";
    html += "<br><br><a href='/main' style='color:#89b4fa;text-decoration:none;'>&larr; Back to Dashboard</a></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  }
}

void handleClearLogs() {
  prefs.begin("esp_log", false);
  prefs.clear();
  prefs.end();

  memset(bootHistory, 0, sizeof(bootHistory));
  totalBootCount = 0; 
  log_time_fixed = false;

  if (time_synced) {
    time_t now; time(&now);
    first_boot_epoch = (uint32_t)now - (millis() / 1000);
    prefs.begin("esp_log", false);
    prefs.putUInt("first_boot", first_boot_epoch);
    prefs.end();
  } else {
    first_boot_epoch = 0;
  }

  if (isCurl()) {
    server.send(200, "text/plain; charset=utf-8", "\n[ SYSTEM ] Crash logs and flash timers cleared successfully.\n\n");
  } else {
    String html;
    html.reserve(1024);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Logs Cleared</title>";
    html += "<link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E🗑️%3C/text%3E%3C/svg%3E'></head>";
    html += "<body style='background:#11111b;color:#a6e3a1;text-align:center;font-family:sans-serif;padding-top:50px;'><h2>🗑️ Logs Cleared!</h2><p style='color:#cdd6f4;'>Crash history and flash timers have been reset. Redirecting...</p>";
    html += "<script>setTimeout(function(){ window.location.href = '/debug'; }, 2000);</script></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  }
}

void handleReboot() {
  if (isCurl()) {
    server.send(200, "text/plain; charset=utf-8", "\n[ SYSTEM ] Rebooting ESP32 now...\n\n");
  } else {
    String html;
    html.reserve(1024);
    html += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>Rebooting...</title>";
    html += "<link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E🔄%3C/text%3E%3C/svg%3E'></head>";
    html += "<body style='background:#11111b;color:#f38ba8;text-align:center;font-family:sans-serif;padding-top:50px;'><h2>🔄 Rebooting...</h2><p style='color:#cdd6f4;'>The ESP32 is restarting. Please wait 5 seconds before refreshing.</p>";
    html += "<script>setTimeout(function(){ window.location.href = '/main'; }, 5000);</script></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
  }
  delay(500); 
  ESP.restart();
}

void handleNotFound() {
  server.send(404, "text/plain; charset=utf-8", "Error 404: Endpoint not found.\n");
}

void setup() {
  setCpuFrequencyMhz(80);

  initServo();
  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle);
  delay(300);
  myservo.detach();

  recordBootEvent();

  uint32_t wifi_start = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(true);
  WiFi.config(local_IP, gateway, subnet, primaryDNS);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) delay(200);
  wifi_connect_ms = millis() - wifi_start;

  configTime(gmtOffset_sec, daylightOffset_sec, "pool.ntp.org", "time.google.com", "time.nist.gov");

  if (MDNS.begin(deviceHostname)) MDNS.addService("http", "tcp", 80);
  
  ArduinoOTA.setHostname(deviceHostname);
  ArduinoOTA.begin();

  const char* headerkeys[] = {"User-Agent", "Host"};
  server.collectHeaders(headerkeys, 2);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/main", HTTP_GET, handleMain);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/reboot", HTTP_ANY, handleReboot);
  server.on("/clear-logs", HTTP_ANY, handleClearLogs);
  server.on("/api/live", HTTP_GET, handleApiLive);
  server.onNotFound(handleNotFound);
  
  server.begin();

  boot_time_ms = millis();
}

void loop() {
  ArduinoOTA.handle();
  server.handleClient();
  if (!time_synced) syncTimeIfNeeded();
  delay(20); 
}