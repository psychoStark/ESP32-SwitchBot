#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_system.h>
#include <esp_heap_caps.h>
#include "soc/soc_caps.h"
#include <time.h>
#include <memory>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include "secrets.h"
#include "web_pages.h"
#include "curl_scripts.h"
#include "calibration.h"
extern "C" {
  #include "microlink.h"
  #include "microlink_internal.h"
  #include "cJSON.h"
  #include "esp_http_client.h"
  #include "esp_crt_bundle.h"
  #include "mdns.h"
  #include "lwip/dns.h"
  #include "esp_pm.h"
  #include "esp_wifi.h"
  #include "esp_netif.h"
  #include "esp_netif_net_stack.h"
  #include "lwip/etharp.h"
  #include "lwip/netif.h"
}

// ==============================================================================
// User-Configurable Network & System Settings
// ==============================================================================

// Operation Mode: Set to true if running strictly on local LAN / subnet router (skips Tailscale & Microlink completely)
#if defined(FULLY_LOCAL_MODE)
const bool isLocalOnly = (FULLY_LOCAL_MODE != 0);
#else
const bool isLocalOnly = (strlen(TAILSCALE_KEY) == 0);
#endif

// Set your preferred timezone offset (e.g. "0530", "+05:30", "+0630", "-05:00", "-0500", "0")
const char* TIMEZONE_OFFSET = "+05:30";

// Hardware Model Name (Optional Manual Override)
// Leave empty ("") to let firmware automatically detect your ESP32 chip model, flash, and PSRAM (e.g. "ESP32-S3-N16R8").
// If you want to change it or if detection is wrong, specify your custom board name here (e.g. "ESP32-S3 DOIT"):
#define BOARD_NAME ""


// Wi-Fi Credentials & Failover Configuration (Up to 6 networks: #1 is main, falls back to 2, 3.. 6)
struct WifiNetworkConfig {
  const char* ssid;
  const char* password;
};

#if defined(WIFI_SSID_1)
  #define _CFG_SSID_1 WIFI_SSID_1
  #define _CFG_PASS_1 WIFI_PASSWORD_1
#elif defined(WIFI_SSID)
  #define _CFG_SSID_1 WIFI_SSID
  #define _CFG_PASS_1 WIFI_PASSWORD
#else
  #define _CFG_SSID_1 ""
  #define _CFG_PASS_1 ""
#endif

#ifndef WIFI_SSID_2
  #define WIFI_SSID_2 ""
  #define WIFI_PASSWORD_2 ""
#endif
#ifndef WIFI_SSID_3
  #define WIFI_SSID_3 ""
  #define WIFI_PASSWORD_3 ""
#endif
#ifndef WIFI_SSID_4
  #define WIFI_SSID_4 ""
  #define WIFI_PASSWORD_4 ""
#endif
#ifndef WIFI_SSID_5
  #define WIFI_SSID_5 ""
  #define WIFI_PASSWORD_5 ""
#endif
#ifndef WIFI_SSID_6
  #define WIFI_SSID_6 ""
  #define WIFI_PASSWORD_6 ""
#endif

const WifiNetworkConfig configuredWifiNetworks[] = {
  { _CFG_SSID_1, _CFG_PASS_1 },
  { WIFI_SSID_2, WIFI_PASSWORD_2 },
  { WIFI_SSID_3, WIFI_PASSWORD_3 },
  { WIFI_SSID_4, WIFI_PASSWORD_4 },
  { WIFI_SSID_5, WIFI_PASSWORD_5 },
  { WIFI_SSID_6, WIFI_PASSWORD_6 }
};
const size_t TOTAL_WIFI_SLOTS = sizeof(configuredWifiNetworks) / sizeof(configuredWifiNetworks[0]);
int activeWifiCount = 0;
int currentWifiIndex = 0;

// Backward-compatible alias for primary network
const char* ssid     = _CFG_SSID_1;
const char* password = _CFG_PASS_1;

// Wi-Fi Static IP & Subnet Settings (defaults for standard 192.168.1.x home networks)
IPAddress local_IP(192, 168, 1, 50);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(1, 1, 1, 1);
IPAddress secondaryDNS(8, 8, 8, 8);
IPAddress routerDNS(192, 168, 1, 1);

// Tailscale subnet route advertised by this device
const char* tailscaleAdvertiseRoute = "192.168.1.0/24";

// Controls whether web dashboards use Google Fonts (Inter) or local system fonts.
// Automatically disabled if running in fully local mode.
bool enableOnlineFonts = (!isLocalOnly && strlen(TAILSCALE_KEY) > 0);

// Change this GPIO pin number if your servo signal wire is connected elsewhere
const int servoPin   = 1;
int restAngle        = DEFAULT_REST_ANGLE;
int pressAngle       = DEFAULT_PRESS_ANGLE;
int pressDurationMs  = DEFAULT_PRESS_DURATION_MS;
bool isCalibrated    = false;

// Asynchronous test tap state (executed in background loop on Core 1)
volatile bool pendingTestTap = false;
int testRestAngle = DEFAULT_REST_ANGLE;
int testPressAngle = DEFAULT_PRESS_ANGLE;
int testDurationMs = DEFAULT_PRESS_DURATION_MS;

// Hold safety watchdog state
volatile bool isHoldActive = false;
unsigned long holdStartTimeMs = 0;
const unsigned long MAX_HOLD_DURATION_MS = 10000; // 10 seconds safety limit

// Parse timezone offset string like "0530", "+05:30", "+0630", "-05:00", "-0500", "+5.5", "+5", "-5", "0" into seconds
inline long parseTimezoneOffsetSec(const char* tzStr) {
  if (!tzStr || !tzStr[0]) return 0;
  while (*tzStr == ' ') tzStr++;
  int sign = 1;
  if (*tzStr == '-') { sign = -1; tzStr++; }
  else if (*tzStr == '+') { sign = 1; tzStr++; }

  const char* colon = strchr(tzStr, ':');
  const char* dot = strchr(tzStr, '.');
  long hours = 0;
  long minutes = 0;
  if (colon) {
    hours = strtol(tzStr, nullptr, 10);
    minutes = strtol(colon + 1, nullptr, 10);
  } else if (dot) {
    float f = strtof(tzStr, nullptr);
    return sign * (long)(f * 3600.0f);
  } else {
    long val = strtol(tzStr, nullptr, 10);
    if (val >= 100 || val <= -100) {
      hours = val / 100;
      minutes = val % 100;
    } else {
      hours = val;
      minutes = 0;
    }
  }
  return sign * (hours * 3600L + minutes * 60L);
}

// OTA gate key - Defined in secrets.h as OTA_KEY.
// If OTA_KEY is non-empty, the user must explicitly provide the key to open the 10-minute
// OTA portal (via password prompt in Web and cURL). The key is never leaked into HTML or scripts.
// If OTA_KEY is empty (""), the portal opens with a single click without requesting a password.
// Automatically close the wireless update window after 10 minutes for safety
const unsigned long OTA_AUTO_TIMEOUT_MS = 10UL * 60UL * 1000UL;

// How often to save a heartbeat timestamp to flash to measure power outage downtime
const unsigned long HEARTBEAT_INTERVAL_MS = 60UL * 1000UL; // 60 seconds

Servo myservo;
WebServer server(80);

uint32_t last_press_time = 0;
// Minimum time to wait between button presses to protect the motor
const uint32_t PRESS_COOLDOWN_MS = 2000;

// Signals the background worker to move the servo without freezing web requests
static volatile bool pendingPress = false;
extern TaskHandle_t loopTaskHandle;
static String s_cachedSubnetDeviceId = "";

uint32_t first_boot_epoch = 0;
uint32_t boot_time_ms = 0;
uint32_t wifi_connect_ms = 0;
bool time_synced = false;
bool log_time_fixed = false;

// --- Downtime tracking ---
bool last_off_computed = false;
uint32_t lastAliveEpoch = 0;
uint32_t lastOffDuration = 0; // seconds; 0 = unknown/first-ever boot
unsigned long lastHeartbeatMs = 0;

// --- OTA gate state ---
bool otaEnabled = false;
unsigned long otaEnabledAt = 0;

// --- Tailscale / microlink ---
microlink_t *ml = nullptr; // promoted to global so /debug can query live status
bool mlRunning = false;    // true when microlink tasks are active
bool mlStandbyMode = false;// true when Tailscale is standing by because Moto G32 is active
int subnetFailCount = 0;   // consecutive probe failures to subnet router
unsigned long lastSubnetCheckMs = 0;

// --- Cached sensor readings ---
float cachedTemp = 0.0f;
float cachedPower = 0.0f;
unsigned long lastTempMs = 0;
unsigned long lastPowerMs = 0;
static uint32_t cachedFlashTotalKB = 0;
static uint32_t cachedFlashUsedKB = 0;

// --- Crash logging: ring buffer over individual NVS keys ---
// Each slot is its own NVS key ("b0".."b24"), so a boot only touches
// boot_cnt, w_idx, and one slot key - not the whole history blob.
const int MAX_BOOT_LOGS = 25;
struct BootLog {
  uint32_t timestamp;   // Unix timestamp of boot event (0 = empty/no NTP yet)
  uint32_t downtimeSec; // Approx downtime seconds before this boot (0 = unknown/first)
  uint8_t  reasonCode;  // esp_reset_reason_t value (0-10, fits a byte)
  uint8_t  wifiIdx;     // Index of connected WiFi network (0-5, 0xFF = unknown/none)
};
BootLog bootHistory[MAX_BOOT_LOGS]; // RAM cache, indexed by physical slot
uint32_t bootWriteIdx = 0;          // next physical slot to write
uint32_t totalBootCount = 0;

// --- Servo trigger logging: NVS ring buffer ---
const int MAX_SERVO_LOGS = 15;
struct ServoLog {
  uint32_t timestamp;  // Unix epoch (0 = pre-NTP)
  uint8_t  fromCurl;   // 1 if triggered via curl, 0 if web
};
ServoLog servoHistory[MAX_SERVO_LOGS];
uint32_t servoWriteIdx = 0;
uint32_t servoLogCount = 0;

// --- Tailscale session logging: NVS ring buffer ---
const int MAX_TAILSCALE_LOGS = 10;
struct TailscaleLog {
  uint32_t startTime;   // Unix epoch when Tailscale connection started (0 = pre-NTP)
  uint32_t endTime;     // Unix epoch when Tailscale connection ended/standby (0 = still active)
  uint32_t downtimeSec; // Approx downtime seconds before this connection started
};
TailscaleLog tailscaleHistory[MAX_TAILSCALE_LOGS];
uint32_t tailscaleWriteIdx = 0;
uint32_t tailscaleLogCount = 0;
uint32_t lastTailscaleStopTime = 0;
RTC_DATA_ATTR static uint32_t rtc_ts_duration_s = 0;
RTC_DATA_ATTR static bool rtc_ts_was_active = false;
static uint32_t mlStartedAtMs = 0;
uint32_t ts_connect_ms = 0;
static bool ts_connected_latched = false;

// --- Helper Functions ---

bool isCurl() {
  return server.header("User-Agent").indexOf("curl") >= 0;
}

void initServo() {
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  myservo.setPeriodHertz(50);
}

void triggerPress() {
  myservo.attach(servoPin, 500, 2400);
  myservo.write(pressAngle);
  delay(pressDurationMs);
  myservo.write(restAngle);
  int travelDelay = max(60, abs(pressAngle - restAngle) * 3);
  delay(travelDelay);
  myservo.detach();
}

float getEstimatedPowerW() {
  float current_mA = 18.0f;
  if (WiFi.status() == WL_CONNECTED) {
    current_mA += WiFi.getSleep() ? 25.0f : 75.0f;
  }
  if (!isLocalOnly && mlRunning) current_mA += 28.0f;
  if (pendingPress || pendingTestTap || isHoldActive) current_mA += 200.0f;
  return (current_mA * 3.3f) / 1000.0f;
}

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

// Load every slot from NVS into the RAM cache. Called once at boot.
void loadBootHistory() {
  Preferences p;
  p.begin("esp_log", true); // read-only
  totalBootCount = p.getUInt("boot_cnt", 0);
  bootWriteIdx   = p.getUInt("w_idx", 0) % MAX_BOOT_LOGS;
  first_boot_epoch = p.getUInt("first_boot", 0);
  for (int i = 0; i < MAX_BOOT_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "b%d", i);
    if (!p.isKey(key)) {
      bootHistory[i].timestamp = 0;
      bootHistory[i].downtimeSec = 0;
      bootHistory[i].reasonCode = 0;
      bootHistory[i].wifiIdx = 0xFF;
      continue;
    }
    size_t len = p.getBytes(key, &bootHistory[i], sizeof(BootLog));
    if (len == sizeof(BootLog)) {
      // modern struct
    } else if (len == 9) {
      // legacy struct with downtimeSec and reasonCode, but no wifiIdx
      bootHistory[i].wifiIdx = 0xFF;
    } else if (len == 5 || len == 8) {
      // legacy struct without downtimeSec
      bootHistory[i].downtimeSec = 0;
      bootHistory[i].wifiIdx = 0xFF;
    } else {
      bootHistory[i].timestamp = 0;
      bootHistory[i].downtimeSec = 0;
      bootHistory[i].reasonCode = 0;
      bootHistory[i].wifiIdx = 0xFF;
    }
  }
  p.end();
}

// Record this boot: writes only boot_cnt, w_idx, and ONE slot key.
void recordBootEvent() {
  Preferences p;
  p.begin("esp_log", false);
  totalBootCount = p.getUInt("boot_cnt", 0) + 1;
  p.putUInt("boot_cnt", totalBootCount);

  uint32_t idx = p.getUInt("w_idx", 0) % MAX_BOOT_LOGS;

  BootLog entry;
  time_t now; time(&now);
  entry.timestamp = (now > 1600000000UL) ? (uint32_t)now : 0;
  entry.downtimeSec = 0;
  entry.reasonCode = (uint8_t)esp_reset_reason();
  entry.wifiIdx = 0xFF;

  char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)idx);
  p.putBytes(key, &entry, sizeof(entry));
  bootHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_BOOT_LOGS;
  p.putUInt("w_idx", newIdx);
  bootWriteIdx = newIdx;

  first_boot_epoch = p.getUInt("first_boot", 0);
  p.end();
}

// chronoIdx: 0 = most recent boot, 1 = one before that, etc.
bool getBootLogAt(int chronoIdx, BootLog &out) {
  if (chronoIdx < 0 || chronoIdx >= MAX_BOOT_LOGS) return false;
  int slot = ((int)bootWriteIdx - 1 - chronoIdx + 2 * MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
  out = bootHistory[slot];
  return true;
}

void updateBootWifiIndex(uint8_t idx) {
  BootLog latest;
  if (getBootLogAt(0, latest)) {
    if (latest.wifiIdx != idx) {
      latest.wifiIdx = idx;
      uint32_t slot = (bootWriteIdx - 1 + MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
      bootHistory[slot] = latest;
      Preferences pLog;
      pLog.begin("esp_log", false);
      char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)slot);
      pLog.putBytes(key, &latest, sizeof(latest));
      pLog.end();
    }
  }
}

// --- Servo trigger NVS logging ---
void loadServoHistory() {
  Preferences p;
  p.begin("servo_log", true);
  servoLogCount  = p.getUInt("s_cnt", 0);
  servoWriteIdx  = p.getUInt("s_idx", 0) % MAX_SERVO_LOGS;
  for (int i = 0; i < MAX_SERVO_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "s%d", i);
    if (!p.isKey(key)) {
      servoHistory[i].timestamp = 0;
      servoHistory[i].fromCurl = 0;
      continue;
    }
    size_t len = p.getBytes(key, &servoHistory[i], sizeof(ServoLog));
    if (len != sizeof(ServoLog)) {
      servoHistory[i].timestamp = 0;
      servoHistory[i].fromCurl = 0;
    }
  }
  p.end();
}

void recordServoTrigger(bool curl) {
  time_t now; time(&now);
  ServoLog entry;
  if (now > 1600000000UL) {
    entry.timestamp = (uint32_t)now;
  } else if (first_boot_epoch > 0) {
    entry.timestamp = first_boot_epoch + (millis() / 1000);
  } else {
    entry.timestamp = 0;
  }
  entry.fromCurl = curl ? 1 : 0;

  Preferences p;
  p.begin("servo_log", false);
  uint32_t idx = p.getUInt("s_idx", 0) % MAX_SERVO_LOGS;
  char key[6]; snprintf(key, sizeof(key), "s%u", (unsigned)idx);
  p.putBytes(key, &entry, sizeof(entry));
  servoHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_SERVO_LOGS;
  p.putUInt("s_idx", newIdx);
  servoWriteIdx = newIdx;

  servoLogCount = p.getUInt("s_cnt", 0) + 1;
  p.putUInt("s_cnt", servoLogCount);
  p.end();
}

// chronoIdx: 0 = most recent trigger, 1 = one before that, etc.
bool getServoLogAt(int chronoIdx, ServoLog &out) {
  if (chronoIdx < 0 || chronoIdx >= MAX_SERVO_LOGS) return false;
  int slot = ((int)servoWriteIdx - 1 - chronoIdx + 2 * MAX_SERVO_LOGS) % MAX_SERVO_LOGS;
  out = servoHistory[slot];
  return true;
}

// --- Tailscale session NVS logging ---
void loadTailscaleHistory() {
  Preferences p;
  p.begin("ts_log", true);
  tailscaleLogCount = p.getUInt("ts_cnt", 0);
  tailscaleWriteIdx = p.getUInt("ts_idx", 0) % MAX_TAILSCALE_LOGS;
  lastTailscaleStopTime = p.getUInt("t_stop", 0);
  for (int i = 0; i < MAX_TAILSCALE_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "t%d", i);
    if (!p.isKey(key)) {
      tailscaleHistory[i].startTime = 0;
      tailscaleHistory[i].endTime = 0;
      tailscaleHistory[i].downtimeSec = 0;
      continue;
    }
    size_t len = p.getBytes(key, &tailscaleHistory[i], sizeof(TailscaleLog));
    if (len != sizeof(TailscaleLog)) {
      tailscaleHistory[i].startTime = 0;
      tailscaleHistory[i].endTime = 0;
      tailscaleHistory[i].downtimeSec = 0;
    }
  }
  p.end();

  // If previous boot ended with an open/active session, close it using last known heartbeat or RTC memory
  if (tailscaleLogCount > 0) {
    uint32_t activeSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
    if (tailscaleHistory[activeSlot].endTime == 0 && tailscaleHistory[activeSlot].startTime > 0) {
      Preferences pEsp;
      pEsp.begin("esp_log", true);
      uint32_t prevAlive = pEsp.getUInt("last_alive", 0);
      pEsp.end();

      uint32_t closeTime = 0;
      if (rtc_ts_was_active && rtc_ts_duration_s > 0) {
        closeTime = tailscaleHistory[activeSlot].startTime + rtc_ts_duration_s;
      } else if (prevAlive > 0 && prevAlive >= tailscaleHistory[activeSlot].startTime) {
        closeTime = prevAlive;
      } else {
        closeTime = tailscaleHistory[activeSlot].startTime;
      }
      tailscaleHistory[activeSlot].endTime = closeTime;
      lastTailscaleStopTime = closeTime;

      Preferences pTs;
      pTs.begin("ts_log", false);
      char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)activeSlot);
      pTs.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
      pTs.putUInt("t_stop", closeTime);
      pTs.end();
    }
  }
  rtc_ts_was_active = false;
  rtc_ts_duration_s = 0;
}

void recordTailscaleStart() {
  time_t now; time(&now);
  uint32_t nowEpoch = (now > 1600000000UL) ? (uint32_t)now : 0;

  Preferences p;
  p.begin("ts_log", false);
  uint32_t lastStop = p.getUInt("t_stop", 0);
  if (lastStop == 0 && tailscaleLogCount > 0) {
    uint32_t prevSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
    if (tailscaleHistory[prevSlot].endTime > 0) {
      lastStop = tailscaleHistory[prevSlot].endTime;
    }
  }
  uint32_t downtime = 0;
  if (nowEpoch > 0 && lastStop > 0 && nowEpoch > lastStop) {
    downtime = nowEpoch - lastStop;
  }

  TailscaleLog entry;
  entry.startTime = nowEpoch;
  entry.endTime = 0; // 0 = Active / Ongoing
  entry.downtimeSec = downtime;

  uint32_t idx = p.getUInt("ts_idx", 0) % MAX_TAILSCALE_LOGS;
  char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)idx);
  p.putBytes(key, &entry, sizeof(entry));
  tailscaleHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_TAILSCALE_LOGS;
  p.putUInt("ts_idx", newIdx);
  tailscaleWriteIdx = newIdx;

  tailscaleLogCount = p.getUInt("ts_cnt", 0) + 1;
  p.putUInt("ts_cnt", tailscaleLogCount);
  p.end();
}

void recordTailscaleStop() {
  time_t now; time(&now);
  uint32_t nowEpoch = (now > 1600000000UL) ? (uint32_t)now : 0;

  Preferences p;
  p.begin("ts_log", false);
  if (nowEpoch > 0) {
    p.putUInt("t_stop", nowEpoch);
    lastTailscaleStopTime = nowEpoch;
  }

  if (tailscaleLogCount > 0) {
    uint32_t activeSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
    if (tailscaleHistory[activeSlot].endTime == 0) {
      uint32_t dur = (mlStartedAtMs > 0) ? ((millis() - mlStartedAtMs) / 1000) : 0;
      if (nowEpoch > 0 && tailscaleHistory[activeSlot].startTime > 0 && nowEpoch >= tailscaleHistory[activeSlot].startTime) {
        tailscaleHistory[activeSlot].endTime = nowEpoch;
      } else if (tailscaleHistory[activeSlot].startTime > 0) {
        tailscaleHistory[activeSlot].endTime = tailscaleHistory[activeSlot].startTime + dur;
      } else {
        tailscaleHistory[activeSlot].endTime = nowEpoch;
      }
      char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)activeSlot);
      p.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
    }
  }
  p.end();
}

bool getTailscaleLogAt(int chronoIdx, TailscaleLog &out) {
  if (chronoIdx < 0 || chronoIdx >= MAX_TAILSCALE_LOGS || chronoIdx >= (int)tailscaleLogCount) return false;
  int slot = ((int)tailscaleWriteIdx - 1 - chronoIdx + 2 * MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
  out = tailscaleHistory[slot];
  return true;
}

void startTailscale(const char* reason = nullptr) {
  if (isLocalOnly || mlRunning || ml == nullptr) return;
  ESP_LOGI("watchdog", "Starting Tailscale (%s)...", reason ? reason : "failover");
  microlink_start(ml);
  mlRunning = true;
  mlStandbyMode = false;
  mlStartedAtMs = millis();
  ts_connected_latched = false;
  ts_connect_ms = 0;
  rtc_ts_was_active = true;
  rtc_ts_duration_s = 0;
  recordTailscaleStart();
}

void stopTailscale(const char* reason = nullptr) {
  if (isLocalOnly || !mlRunning || ml == nullptr) return;
  ESP_LOGI("watchdog", "Stopping Tailscale (%s)...", reason ? reason : "standby");
  microlink_stop(ml);
  mlRunning = false;
  mlStandbyMode = true;
  ts_connected_latched = false;
  ts_connect_ms = 0;
  subnetFailCount = 0;
  rtc_ts_was_active = false;
  recordTailscaleStop();
}

void syncTimeIfNeeded() {
  if (time_synced || WiFi.status() != WL_CONNECTED) return;
  static unsigned long lastNtpCheckMs = 0;
  unsigned long nowMs = millis();
  if (nowMs - lastNtpCheckMs < 1000) return;
  lastNtpCheckMs = nowMs;

  time_t now; time(&now);
  if (now > 1600000000UL) {
      time_synced = true;
      uint32_t thisBootEpoch = (uint32_t)now - (millis() / 1000);

      Preferences pLog;
      pLog.begin("esp_log", false);

      if (first_boot_epoch == 0) {
        first_boot_epoch = thisBootEpoch;
        pLog.putUInt("first_boot", first_boot_epoch);
      }

      if (!log_time_fixed) {
        BootLog latest;
        if (getBootLogAt(0, latest) && latest.timestamp < 1600000000UL) {
          latest.timestamp = thisBootEpoch;
          uint32_t slot = (bootWriteIdx - 1 + MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
          bootHistory[slot] = latest;
          char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)slot);
          pLog.putBytes(key, &latest, sizeof(latest));
        }
        log_time_fixed = true;
      }

      // If the latest Tailscale session started before NTP synced, fix its timestamp
      if (tailscaleLogCount > 0) {
        uint32_t activeSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
        if (tailscaleHistory[activeSlot].startTime < 1600000000UL) {
          tailscaleHistory[activeSlot].startTime = thisBootEpoch;
          char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)activeSlot);
          Preferences pTs;
          pTs.begin("ts_log", false);
          pTs.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
          pTs.end();
        }
      }

      // If any Servo trigger occurred before NTP synced, fix its timestamp
      if (servoLogCount > 0) {
        Preferences pServo;
        pServo.begin("servo_log", false);
        for (int i = 0; i < MAX_SERVO_LOGS && i < (int)servoLogCount; i++) {
          if (servoHistory[i].timestamp < 1600000000UL) {
            servoHistory[i].timestamp = thisBootEpoch;
            char key[6]; snprintf(key, sizeof(key), "s%u", (unsigned)i);
            pServo.putBytes(key, &servoHistory[i], sizeof(ServoLog));
          }
        }
        pServo.end();
      }

      // --- Downtime estimate: compare against the last heartbeat the
      // previous session managed to write before it died. ---
      if (!last_off_computed) {
        uint32_t prevAlive = pLog.getUInt("last_alive", 0);
        lastOffDuration = (prevAlive > 0 && thisBootEpoch > prevAlive) ? (thisBootEpoch - prevAlive) : 0;
        pLog.putUInt("last_off", lastOffDuration);

        BootLog latest;
        if (getBootLogAt(0, latest)) {
          latest.downtimeSec = (uint32_t)lastOffDuration;
          uint32_t slot = (bootWriteIdx - 1 + MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
          bootHistory[slot] = latest;
          char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)slot);
          pLog.putBytes(key, &latest, sizeof(latest));
        }

        lastAliveEpoch = thisBootEpoch;
        pLog.putUInt("last_alive", lastAliveEpoch);
        lastHeartbeatMs = millis(); // restart the periodic heartbeat timer from now
        last_off_computed = true;
      }

      pLog.end();
    }
}

// Periodic "we're still alive" write - called from loop(). Keeps last_alive
// fresh so the NEXT boot's downtime estimate is accurate to within this
// interval, without touching the boot-log ring buffer at all.
void heartbeatIfNeeded() {
  if (!time_synced) return;
  if (millis() - lastHeartbeatMs < HEARTBEAT_INTERVAL_MS) return;
  lastHeartbeatMs = millis();

  time_t now; time(&now);
  lastAliveEpoch = (uint32_t)now;
  Preferences p;
  p.begin("esp_log", false);
  p.putUInt("last_alive", lastAliveEpoch);
  p.end();
}

void formatTimestampBuf(uint32_t epoch, char* buf, size_t maxLen) {
  if (epoch < 1600000000UL) {
    snprintf(buf, maxLen, "Awaiting NTP Sync...");
    return;
  }
  time_t t_epoch = epoch;
  struct tm timeinfo;
  localtime_r(&t_epoch, &timeinfo);
  strftime(buf, maxLen, "%I:%M:%S %p %d-%b-%Y", &timeinfo);
}

String formatTimestamp(uint32_t epoch) {
  char buf[64];
  formatTimestampBuf(epoch, buf, sizeof(buf));
  return String(buf);
}

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

void formatMs(uint32_t ms, char* buffer, size_t maxLen) {
  if (ms >= 1000) {
    snprintf(buffer, maxLen, "%.2fs", (float)ms / 1000.0f);
  } else {
    snprintf(buffer, maxLen, "%lums", (unsigned long)ms);
  }
}

void getDeviceModelName(char* out, size_t maxLen) {
#if defined(BOARD_NAME)
  if (strlen(BOARD_NAME) > 0) {
    snprintf(out, maxLen, "%s", BOARD_NAME);
    return;
  }
#endif
  const char* chip = ESP.getChipModel();
  uint32_t flashMB = cachedFlashTotalKB > 0 ? (cachedFlashTotalKB / 1024) : (ESP.getFlashChipSize() / (1024 * 1024));
  size_t spiramBytes = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
  if (spiramBytes == 0) spiramBytes = ESP.getPsramSize();
  uint32_t psramMB = spiramBytes / (1024 * 1024);

  if (psramMB > 0) {
    snprintf(out, maxLen, "%s-N%luR%lu", chip, (unsigned long)flashMB, (unsigned long)psramMB);
  } else {
    snprintf(out, maxLen, "%s-N%lu", chip, (unsigned long)flashMB);
  }
}


String formatSessionRange(uint32_t startEpoch, uint32_t endEpoch) {
  if (startEpoch < 1600000000UL) return "Awaiting NTP Sync...";
  time_t s_epoch = startEpoch;
  struct tm s_tm;
  localtime_r(&s_epoch, &s_tm);

  char s_time[16], s_date[16];
  strftime(s_time, sizeof(s_time), "%I:%M:%S %p", &s_tm);
  strftime(s_date, sizeof(s_date), "%d-%b-%Y", &s_tm);

  if (endEpoch == 0) {
    char buf[64];
    snprintf(buf, sizeof(buf), "Started: %s &bull; %s", s_time, s_date);
    return String(buf);
  }

  time_t e_epoch = endEpoch;
  struct tm e_tm;
  localtime_r(&e_epoch, &e_tm);
  char e_time[16], e_date[16];
  strftime(e_time, sizeof(e_time), "%I:%M:%S %p", &e_tm);
  strftime(e_date, sizeof(e_date), "%d-%b-%Y", &e_tm);

  char buf[96];
  if (strcmp(s_date, e_date) == 0) {
    snprintf(buf, sizeof(buf), "%s &ndash; %s &bull; %s", s_time, e_time, s_date);
  } else {
    snprintf(buf, sizeof(buf), "%s (%s) &ndash; %s (%s)", s_time, s_date, e_time, e_date);
  }
  return String(buf);
}

// --- OTA gating ---

void startOTA() {
  if (!otaEnabled) {
    ArduinoOTA.setHostname("esp32");
    ArduinoOTA.begin();
    otaEnabled = true;
  }
  otaEnabledAt = millis();
}

void stopOTA() {
  if (!otaEnabled) return;
  ArduinoOTA.end();
  otaEnabled = false;
}

// --- Sensor caching ---

void updateSensorCache() {
  unsigned long now = millis();
#if defined(SOC_TEMP_SENSOR_SUPPORTED) && SOC_TEMP_SENSOR_SUPPORTED
  if (now - lastTempMs >= 2000) { cachedTemp = roundf(temperatureRead()); lastTempMs = now; }
#else
  cachedTemp = -999.0f; // placeholder for chips without hardware temp sensor
#endif
  if (now - lastPowerMs >= 5000) { cachedPower = getEstimatedPowerW(); lastPowerMs = now; }
}

// --- Tailscale / microlink status ---
// Pulls together everything the documented microlink.h API surface actually
// exposes: connection state, assigned VPN IPv4, and a MagicDNS self-check
// (resolve our own configured device name and see if it matches our own IP).
// NOTE: microlink.h's public API (per the project docs) has no IPv6 getter -
// if your actual header exposes one under a different name, let me know and
// I'll wire it in rather than guess a symbol that might not exist.
void getVpnStatus(bool &connected, String &ip, String &magicDns) {
  connected = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  ip = "Not Valid";
  magicDns = "Not Valid";
  if (connected) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) {
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%lu.%lu.%lu.%lu",
        (unsigned long)((rawIp >> 24) & 0xFF), (unsigned long)((rawIp >> 16) & 0xFF),
        (unsigned long)((rawIp >> 8) & 0xFF), (unsigned long)(rawIp & 0xFF));
      ip = String(ipBuf);
    }

    uint32_t resolved = microlink_resolve(ml, TAILSCALE_HOST);
    if (resolved != 0 && rawIp != 0 && resolved == rawIp) magicDns = "OK";
    else if (resolved != 0) magicDns = "Mismatch";
    else magicDns = "Not resolving";
  }
}

String getDerpRegionName() {
  if (ml == nullptr || !mlRunning) return "";
  uint16_t reg = ml->derp_home_region ? ml->derp_home_region : ML_DERP_REGION;
  for (int i = 0; i < ml->derp_region_count; i++) {
    if (ml->derp_regions[i].region_id == reg) {
      if (ml->derp_regions[i].code[0]) {
        return String(ml->derp_regions[i].code);
      }
    }
  }
  switch (reg) {
    case 1: return "nyc";
    case 2: return "sfo";
    case 3: return "sin";
    case 4: return "fra";
    case 5: return "syd";
    case 6: return "blr";
    case 7: return "tok";
    case 8: return "lhr";
    case 9: return "fra";
    case 10: return "sea";
    case 11: return "sao";
    case 12: return "ord";
    case 13: return "mad";
    case 14: return "dfw";
    case 15: return "mia";
    case 16: return "waw";
    case 17: return "del";
    case 18: return "phx";
    case 19: return "den";
    default: {
      char b[16];
      snprintf(b, sizeof(b), "reg%u", (unsigned)reg);
      return String(b);
    }
  }
}

void getConnectionType(char* out, size_t maxLen) {
  if (isLocalOnly) { snprintf(out, maxLen, "Local Only"); return; }
  if (mlStandbyMode && !mlRunning) { snprintf(out, maxLen, "Subnet Active"); return; }
  if (ml == nullptr || !mlRunning || !microlink_is_connected(ml)) { snprintf(out, maxLen, "Not Connected"); return; }
  int count = microlink_get_peer_count(ml);
  bool foundDirect = false;
  bool foundRelayed = false;
  for (int i = 0; i < count; i++) {
    microlink_peer_info_t pinfo;
    if (microlink_get_peer_info(ml, i, &pinfo) == ESP_OK && pinfo.online) {
      if (pinfo.direct_path) foundDirect = true;
      else foundRelayed = true;
    }
  }
  String reg = getDerpRegionName();
  if (foundDirect && foundRelayed) {
    if (reg.length()) snprintf(out, maxLen, "Mixed (%s)", reg.c_str());
    else snprintf(out, maxLen, "Mixed");
  } else if (foundDirect) {
    snprintf(out, maxLen, "Direct (UDP)");
  } else {
    if (reg.length()) snprintf(out, maxLen, "Relayed (%s)", reg.c_str());
    else snprintf(out, maxLen, "Relayed");
  }
}

String getConnectionType() {
  char b[48];
  getConnectionType(b, sizeof(b));
  return String(b);
}

const char* getResetReasonClass(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON:   return "ok";
    case ESP_RST_PANIC:
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
    case ESP_RST_BROWNOUT:  return "danger";
    case ESP_RST_SW:
    case ESP_RST_DEEPSLEEP: return "warn";
    default:                return "";
  }
}

// --- Tailscale Cloud REST API Watchdog ---

static bool s_lastMotoConnected = false;
static unsigned long s_lastApiCheckMs = 0;
static int s_lastApiHttpStatus = 0;

static esp_err_t ts_http_event_handler(esp_http_client_event_t *evt) {
  if (evt->event_id == HTTP_EVENT_ON_DATA) {
    String *buf = (String *)evt->user_data;
    if (buf && evt->data && evt->data_len > 0) {
      buf->concat((const char *)evt->data, evt->data_len);
    }
  }
  return ESP_OK;
}

/**
 * @brief Checks if the primary subnet router device is actively connected to the
 * Tailscale control plane by querying api.tailscale.com over standard HTTPS using
 * esp_http_client with cert bundle.
 * Runs in ~250-350ms, allowing Wi-Fi radio to return to modem-sleep and keeping
 * the ESP32 cool at ~38-41°C.
 *
 * Supports numeric device ID or machine hostname.
 */
bool checkTailscaleSubnetRouterOnline(const char* apiKey, const char* deviceIdent) {
  if (!apiKey || strlen(apiKey) == 0) return false;
  if (!deviceIdent || strlen(deviceIdent) == 0) return false;

  bool isNumeric = true;
  size_t identLen = strlen(deviceIdent);
  for (size_t i = 0; i < identLen; i++) {
    if (!isdigit((unsigned char)deviceIdent[i])) { isNumeric = false; break; }
  }

  // Use cached device ID if available to query the compact single-device endpoint (~400 bytes vs 30KB full fleet inventory)
  bool usingDirectDeviceEndpoint = isNumeric || (s_cachedSubnetDeviceId.length() > 0);
  String targetId = isNumeric ? String(deviceIdent) : (s_cachedSubnetDeviceId.length() > 0 ? s_cachedSubnetDeviceId : "");

  String url = usingDirectDeviceEndpoint
    ? ("https://api.tailscale.com/api/v2/device/" + targetId)
    : "https://api.tailscale.com/api/v2/tailnet/-/devices";

  String payload;
  payload.reserve(usingDirectDeviceEndpoint ? 1024 : 8192);

  esp_http_client_config_t config = {};
  config.url = url.c_str();
  config.event_handler = ts_http_event_handler;
  config.user_data = &payload;
  config.timeout_ms = 6000;
  config.crt_bundle_attach = esp_crt_bundle_attach;
  config.skip_cert_common_name_check = true;

  esp_http_client_handle_t client = esp_http_client_init(&config);
  if (!client) {
    ESP_LOGE("watchdog", "Failed to initialize HTTP client for %s", url.c_str());
    return false;
  }

  String authHeader = "Bearer " + String(apiKey);
  esp_http_client_set_header(client, "Authorization", authHeader.c_str());
  esp_http_client_set_header(client, "User-Agent", "ESP32-SwitchBot/1.0");

  esp_err_t err = esp_http_client_perform(client);
  int httpCode = esp_http_client_get_status_code(client);
  esp_http_client_cleanup(client);

  s_lastApiHttpStatus = httpCode;
  s_lastApiCheckMs = millis();

  // If query using cached ID failed with 404, invalidate cache so next cycle rediscovers via /tailnet/-/devices
  if (usingDirectDeviceEndpoint && !isNumeric && httpCode == 404) {
    ESP_LOGW("watchdog", "Cached device ID %s returned 404; clearing cache to re-discover", s_cachedSubnetDeviceId.c_str());
    s_cachedSubnetDeviceId = "";
  }

  bool isConnected = false;

  if (err == ESP_OK && httpCode == 200 && payload.length() > 0) {
    cJSON *root = cJSON_Parse(payload.c_str());
    if (root != nullptr) {
      if (usingDirectDeviceEndpoint) {
        cJSON *conn = cJSON_GetObjectItem(root, "connectedToControl");
        if (cJSON_IsBool(conn)) {
          isConnected = cJSON_IsTrue(conn);
        }
      } else {
        cJSON *devices = cJSON_GetObjectItem(root, "devices");
        if (cJSON_IsArray(devices)) {
          int count = cJSON_GetArraySize(devices);
          for (int i = 0; i < count; i++) {
            cJSON *dev = cJSON_GetArrayItem(devices, i);
            if (!dev) continue;
            cJSON *host = cJSON_GetObjectItem(dev, "hostname");
            cJSON *name = cJSON_GetObjectItem(dev, "name");
            const char* hStr = (host && cJSON_IsString(host)) ? host->valuestring : "";
            const char* nStr = (name && cJSON_IsString(name)) ? name->valuestring : "";
            if (strcasestr(hStr, deviceIdent) != nullptr || strcasestr(nStr, deviceIdent) != nullptr) {
              cJSON *conn = cJSON_GetObjectItem(dev, "connectedToControl");
              if (cJSON_IsBool(conn)) {
                isConnected = cJSON_IsTrue(conn);
              }
              cJSON *nodeId = cJSON_GetObjectItem(dev, "id");
              if (nodeId && cJSON_IsString(nodeId) && nodeId->valuestring && strlen(nodeId->valuestring) > 0) {
                s_cachedSubnetDeviceId = String(nodeId->valuestring);
                ESP_LOGI("watchdog", "Cached numeric Tailscale device ID for '%s': %s", deviceIdent, s_cachedSubnetDeviceId.c_str());
              }
              break;
            }
          }
        }
      }
      cJSON_Delete(root);
    } else {
      ESP_LOGE("watchdog", "Failed to parse Tailscale API JSON response");
    }
  } else {
    ESP_LOGW("watchdog", "Tailscale API query failed, err: %d, HTTP code: %d", err, httpCode);
  }

  s_lastMotoConnected = isConnected;
  return isConnected;
}

// --- Routes ---

// Handles button press requests: sends an immediate reply then moves the servo
void handleRoot() {
  // If not yet calibrated, direct the user to the calibration wizard
  if (!isCalibrated) {
    server.sendHeader("Connection", "close");
    if (isCurl()) {
      String host = server.header("Host");
      if (host.length() == 0) host = "esp32.local";
      char out[256];
      snprintf(out, sizeof(out), "\n[!] Device is not calibrated. Please calibrate first.\n    Run: curl -s http://%s/calibrate | bash\n\n", host.c_str());
      server.send(200, "text/plain; charset=utf-8", out);
    } else {
      server.sendHeader("Location", "/calibrate", true);
      server.send(302, "text/plain", "");
    }
    return;
  }

  uint32_t now = millis();
  // Prevent rapid clicks to protect the motor from overheating
  if (now - last_press_time < PRESS_COOLDOWN_MS) {
    server.sendHeader("Connection", "close");
    server.send(429, "text/plain; charset=utf-8", "Cooldown active.\n");
    return;
  }
  last_press_time = now;
  bool curlReq = isCurl();
  // Save this button press to the history log in flash memory
  recordServoTrigger(curlReq);
  // Tell the background worker to move the motor and wake it up immediately
  pendingPress = true;
  if (loopTaskHandle != nullptr) {
    xTaskNotifyGive(loopTaskHandle);
  }

  char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));

  if (curlReq) {
    char out[192];
    snprintf(out, sizeof(out), "\n[+] SUCCESS: Servo tap queued.\n[i] ESP Uptime: %s\n\n", upBuf);
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out);
  } else {
    char body[320];
    snprintf(body, sizeof(body),
      "<h1>&#9889; Success</h1>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Servo Tap</span><span class='v ok'>Queued</span></div>"
      "<div class='row'><span class='k'>ESP Uptime</span><span class='v mono'>%s</span></div>"
      "</div>"
      "<a class='back' href='/main'>&larr; Back to Dashboard</a>",
      upBuf);
    sendWrappedPage(server, "SwitchBot Trigger", "&#9889;", body, "", true);
  }
}

void handleMain() {
  if (isCurl()) {
    String host = server.header("Host");
    if (host.length() == 0) host = "esp32.local";
    String script = generateCurlDashboardScript(host, OTA_KEY);
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", script);
  } else {
    String primaryAction = isCalibrated
      ? "<a class='primary' href='/'>&#9889; Trigger Servo</a>"
      : "<a class='primary' href='/calibrate'>&#127919; Calibrate Servo</a>";

    String body =
      "<h1>SwitchBot</h1>"
      "<div class='nav'>"
      + primaryAction +
      "<a href='/info'>&#128187; Device Info</a>"
      "<a href='/debug'>&#128295; Logs &amp; Debug</a>"
      "</div>";
    sendWrappedPage(server, "SwitchBot Dashboard", "&#127920;", body.c_str(), "", true);
  }
}

void handleInfo() {
  uint32_t ramTotal = ESP.getHeapSize() / 1024;
  uint32_t ramFree = ESP.getFreeHeap() / 1024;
  if (cachedFlashTotalKB == 0) {
    cachedFlashTotalKB = ESP.getFlashChipSize() / 1024;
    cachedFlashUsedKB = ESP.getSketchSize() / 1024;
  }
  uint32_t flashTotal = cachedFlashTotalKB;
  uint32_t flashUsed = cachedFlashUsedKB;
  uint32_t psramTotal = heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024;
  uint32_t psramFree = heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024;
  if (cachedTemp < -50.0f) {
    updateSensorCache(); // only read hardware ADC if no reading exists yet
  }

  bool vpnConn = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  char vpnIpBuf[16] = "Not Valid";
  if (vpnConn) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) {
      snprintf(vpnIpBuf, sizeof(vpnIpBuf), "%lu.%lu.%lu.%lu",
        (unsigned long)((rawIp >> 24) & 0xFF), (unsigned long)((rawIp >> 16) & 0xFF),
        (unsigned long)((rawIp >> 8) & 0xFF), (unsigned long)(rawIp & 0xFF));
    }
  }
  char connTypeBuf[48];
  getConnectionType(connTypeBuf, sizeof(connTypeBuf));
  const char* tailscaleStatus = isLocalOnly ? "Disabled (Local Only)" : (vpnConn ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected"));
  const char* tailscalePillClass = isLocalOnly ? "off" : (vpnConn ? "on" : (mlStandbyMode ? "standby" : "off"));
  const char* tailscalePillText = isLocalOnly ? "Local Only" : (vpnConn ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected"));

  char tempBuf[16];
  if (cachedTemp > -50.0f) {
    snprintf(tempBuf, sizeof(tempBuf), "%.0f °C", cachedTemp);
  } else {
    snprintf(tempBuf, sizeof(tempBuf), "-");
  }

  bool hasTemp = (cachedTemp > -50.0f);
  bool hasTsIp = (vpnConn && vpnIpBuf[0] != '-' && strcmp(vpnIpBuf, "Not Valid") != 0);
  char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));
  char devModelBuf[64]; getDeviceModelName(devModelBuf, sizeof(devModelBuf));

  if (isCurl()) {
    char cpuBuf[32]; snprintf(cpuBuf, sizeof(cpuBuf), "%lu MHz", (unsigned long)ESP.getCpuFreqMHz());

    const size_t n = 1500;
    std::unique_ptr<char[]> out(new char[n]);
    int off = snprintf(out.get(), n,
      "==================================================\n"
      " [i] %s DEVICE INFO\n"
      "==================================================\n",
      ESP.getChipModel()
    );
    if (hasTemp) {
      off += snprintf(out.get() + off, n - off,
        " Uptime     : %-12s | Temp.  : %-7s\n"
        " CPU Clock  : %-12s | Power  : ~%.2f W\n",
        upBuf, tempBuf,
        cpuBuf, cachedPower
      );
    } else {
      off += snprintf(out.get() + off, n - off,
        " Uptime     : %-12s | Power  : ~%.2f W\n"
        " CPU Clock  : %-12s\n",
        upBuf, cachedPower,
        cpuBuf
      );
    }
    off += snprintf(out.get() + off, n - off,
      " Device     : %s\n"
      "--------------------------------------------------\n"
      " RAM Used   : %lu/%lu KB\n"
      " Flash Used : %lu/%lu KB\n",
      devModelBuf,
      (ramTotal - ramFree), ramTotal,
      flashUsed, flashTotal
    );
    if (psramTotal > 0) {
      off += snprintf(out.get() + off, n - off,
        " PSRAM Used : %lu/%lu KB\n",
        (unsigned long)(psramTotal - psramFree), (unsigned long)psramTotal
      );
    }
    off += snprintf(out.get() + off, n - off,
      "--------------------------------------------------\n"
      " Wi-Fi SSID : %s\n"
      " IP Address : %s (esp32.local)\n"
      " Firmware   : Core 1 | QIO 80MHz | SPIFFS 4MB\n",
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str()
    );
    if (!isLocalOnly) {
      off += snprintf(out.get() + off, n - off,
        "--------------------------------------------------\n"
        " [ TAILSCALE ]\n"
        " Status     : %s\n"
        " Hostname   : %s\n",
        tailscaleStatus,
        TAILSCALE_HOST
      );
      if (hasTsIp) {
        off += snprintf(out.get() + off, n - off,
          " IP Address : %s\n",
          vpnIpBuf
        );
      }
      off += snprintf(out.get() + off, n - off,
        " Connection : %s\n",
        connTypeBuf
      );
    }
    off += snprintf(out.get() + off, n - off, "==================================================");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out.get());
  } else {
    char b1[600];
    int off1 = snprintf(b1, sizeof(b1),
      "<h1>&#128187; Device Info</h1>"
      "<h3>Live</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Uptime</span><span class='v mono' id='up'>%s</span></div>"
      "<div class='row'><span class='k'>CPU Clock</span><span class='v mono' id='clk'>%lu MHz</span></div>",
      upBuf,
      (unsigned long)ESP.getCpuFreqMHz()
    );
    if (hasTemp) {
      off1 += snprintf(b1 + off1, sizeof(b1) - off1,
        "<div class='row'><span class='k'>CPU Temp.</span><span class='v mono' id='tmp'>%.0f &deg;C</span></div>",
        cachedTemp
      );
    }
    snprintf(b1 + off1, sizeof(b1) - off1,
      "<div class='row'><span class='k'>Est. Power</span><span class='v mono' id='pwr'>~%.2f W</span></div>"
      "</div>",
      cachedPower
    );

    const char* activeSsid = (configuredWifiNetworks[currentWifiIndex].ssid && configuredWifiNetworks[currentWifiIndex].ssid[0])
      ? configuredWifiNetworks[currentWifiIndex].ssid : WiFi.SSID().c_str();

    char b2[800];
    int off2 = snprintf(b2, sizeof(b2),
      "<h3>Hardware &amp; Storage</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Model</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>RAM</span><span class='v mono' id='ram'>%lu/%lu KB</span></div>"
      "<div class='row'><span class='k'>Flash</span><span class='v mono'>%lu/%lu KB</span></div>",
      devModelBuf,
      (ramTotal - ramFree), ramTotal,
      flashUsed, flashTotal
    );
    if (psramTotal > 0) {
      off2 += snprintf(b2 + off2, sizeof(b2) - off2,
        "<div class='row'><span class='k'>PSRAM</span><span class='v mono'>%lu/%lu KB</span></div>",
        (unsigned long)(psramTotal - psramFree), (unsigned long)psramTotal
      );
    }
    snprintf(b2 + off2, sizeof(b2) - off2,
      "</div>"
      "<h3>Network &amp; Connectivity</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Wi-Fi SSID</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>IP Address</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>Hostname</span><span class='v mono'>esp32.local</span></div>"
      "</div>",
      activeSsid,
      WiFi.localIP().toString().c_str()
    );

    char b3[600] = "";
    if (!isLocalOnly) {
      snprintf(b3, sizeof(b3),
        "<h3>Tailscale</h3>"
        "<div class='card'>"
        "<div class='row'><span class='k'>Status</span>"
        "<span class='v'><span class='pill %s' id='ts-pill'>%s</span></span></div>"
        "<div class='row'><span class='k'>Hostname</span><span class='v mono'>%s</span></div>"
        "<div class='row' id='ts-ip-row'%s><span class='k'>IP Address</span><span class='v mono' id='ts-ip'>%s</span></div>"
        "<div class='row'><span class='k'>Connection</span><span class='v mono' id='ts-conn'>%s</span></div>"
        "</div>",
        tailscalePillClass, tailscalePillText,
        TAILSCALE_HOST,
        hasTsIp ? "" : " style='display:none;'",
        vpnIpBuf,
        connTypeBuf
      );
    }

    String body;
    body.reserve(strlen(b1) + strlen(b2) + strlen(b3) + 80);
    body += b1;
    body += b2;
    body += b3;
    body += "<a class='back' href='/main'>&larr; Back to Dashboard</a>";

    sendWrappedPage(server, "Device Info", "&#128187;", body.c_str(), "", false);
  }
}

void handleApiLive() {
  updateSensorCache();

  char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));

  char flashBuf[32] = "Awaiting NTP Sync...";
  if (time_synced && first_boot_epoch > 0) {
    time_t now; time(&now);
    if (now > first_boot_epoch) formatDuration(now - first_boot_epoch, flashBuf, sizeof(flashBuf));
  }

  uint32_t ramTotal = ESP.getHeapSize() / 1024;
  uint32_t ramFree = ESP.getFreeHeap() / 1024;

  ServoLog latestServo;
  uint32_t servoTimestamp = 0;
  if (getServoLogAt(0, latestServo) && servoLogCount > 0) {
    servoTimestamp = latestServo.timestamp;
  }

  time_t nowSec = 0;
  if (time_synced) time(&nowSec);

  uint32_t tsStart = 0;
  TailscaleLog curTs;
  if (mlRunning && getTailscaleLogAt(0, curTs) && curTs.endTime == 0) {
    tsStart = curTs.startTime;
  }

  bool vpnConn = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  char vpnIpBuf[16] = "-";
  if (vpnConn) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) {
      snprintf(vpnIpBuf, sizeof(vpnIpBuf), "%lu.%lu.%lu.%lu",
        (unsigned long)((rawIp >> 24) & 0xFF), (unsigned long)((rawIp >> 16) & 0xFF),
        (unsigned long)((rawIp >> 8) & 0xFF), (unsigned long)(rawIp & 0xFF));
    }
  }
  char connTypeBuf[48];
  getConnectionType(connTypeBuf, sizeof(connTypeBuf));
  const char* tsSt = isLocalOnly ? "Local Only" : (vpnConn ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected"));
  const char* tsCls = isLocalOnly ? "off" : (vpnConn ? "on" : (mlStandbyMode ? "standby" : "off"));

  char tsConnectBuf[32] = "";
  if (vpnConn && ts_connect_ms > 0) {
    formatMs(ts_connect_ms, tsConnectBuf, sizeof(tsConnectBuf));
  }

  uint32_t ts0_act = 0;
  char ts0_end_buf[40] = "";
  char ts0_dur_buf[32] = "";
  char ts0_dt_buf[64] = "";
  TailscaleLog ts0 = {};
  if (getTailscaleLogAt(0, ts0) && tailscaleLogCount > 0 && ts0.startTime > 0) {
    if (ts0.endTime == 0) {
      ts0_act = 1;
      if (nowSec > ts0.startTime) {
        formatDuration((uint32_t)(nowSec - ts0.startTime), ts0_dur_buf, sizeof(ts0_dur_buf));
      } else {
        snprintf(ts0_dur_buf, sizeof(ts0_dur_buf), "0s");
      }
    } else {
      ts0_act = 0;
      String endStr = formatTimestamp(ts0.endTime);
      snprintf(ts0_end_buf, sizeof(ts0_end_buf), "%s", endStr.c_str());
      uint32_t d = (ts0.endTime > ts0.startTime) ? (ts0.endTime - ts0.startTime) : 0;
      formatDuration(d, ts0_dur_buf, sizeof(ts0_dur_buf));
    }
    uint32_t dtSec = ts0.downtimeSec;
    if (dtSec == 0 && ts0.startTime > 0) {
      TailscaleLog prevTs = {};
      if (getTailscaleLogAt(1, prevTs) && prevTs.endTime > 0 && ts0.startTime > prevTs.endTime) {
        dtSec = ts0.startTime - prevTs.endTime;
      }
    }
    if (dtSec > 0) {
      char dtDur[32]; formatDuration(dtSec, dtDur, sizeof(dtDur));
      snprintf(ts0_dt_buf, sizeof(ts0_dt_buf), " &bull; Downtime: %s", dtDur);
    }
  }

  char json[896];
  snprintf(json, sizeof(json),
    "{\"u\":\"%s\",\"uf\":\"%s\",\"t\":%.0f,\"ru\":%lu,\"rt\":%lu,\"c\":%lu,\"p\":%.2f,\"ota\":%d,\"sl\":%lu,\"st\":%lu,\"sc\":%lu,\"ts\":%lu,\"ts_st\":\"%s\",\"ts_cls\":\"%s\",\"ts_ip\":\"%s\",\"ts_conn\":\"%s\",\"ts_cm\":%lu,\"ts_cs\":\"%s\",\"ts0_act\":%lu,\"ts0_end\":\"%s\",\"ts0_dur\":\"%s\",\"ts0_dt\":\"%s\",\"cal\":%d,\"s_rest\":%d,\"s_press\":%d,\"s_dur\":%d,\"local\":%d,\"w_idx\":%d,\"w_tot\":%d}",
    upBuf, flashBuf, cachedTemp, (ramTotal - ramFree), ramTotal, ESP.getCpuFreqMHz(), cachedPower,
    otaEnabled ? 1 : 0, (unsigned long)servoTimestamp, (unsigned long)nowSec, (unsigned long)servoLogCount,
    (unsigned long)tsStart, tsSt, tsCls, vpnIpBuf, connTypeBuf,
    (unsigned long)ts_connect_ms, tsConnectBuf,
    (unsigned long)ts0_act, ts0_end_buf, ts0_dur_buf, ts0_dt_buf,
    isCalibrated ? 1 : 0, restAngle, pressAngle, pressDurationMs,
    isLocalOnly ? 1 : 0, currentWifiIndex + 1, activeWifiCount);
  
  server.sendHeader("Connection", "close");
  server.send(200, "application/json", json);
}

void handleDebug() {
  char flashDuration[32] = "Awaiting NTP Sync...";
  if (time_synced && first_boot_epoch > 0) {
    time_t now; time(&now);
    if (now > first_boot_epoch) formatDuration(now - first_boot_epoch, flashDuration, sizeof(flashDuration));
  }
  char bootDuration[32];
  formatDuration(esp_timer_get_time() / 1000000ULL, bootDuration, sizeof(bootDuration));

  char downtimeDuration[32] = "No data";
  if (last_off_computed && lastOffDuration > 0) formatDuration(lastOffDuration, downtimeDuration, sizeof(downtimeDuration));

  BootLog latest = {};
  bool hasLatest = getBootLogAt(0, latest) && (totalBootCount > 0);
  const char* lastResetCause = hasLatest ? getResetReasonString((esp_reset_reason_t)latest.reasonCode) : "-";
  char lastResetTime[48] = "-";
  if (hasLatest) formatTimestampBuf(latest.timestamp, lastResetTime, sizeof(lastResetTime));

  ServoLog latestServo = {};
  bool hasServoLatest = getServoLogAt(0, latestServo) && (servoLogCount > 0);
  char lastServoAgo[48] = "Recent (Pre-NTP)";
  if (hasServoLatest && time_synced && latestServo.timestamp > 0) {
    time_t now; time(&now);
    if ((uint32_t)now > latestServo.timestamp) {
      char agoBuf[32];
      formatDuration((uint32_t)now - latestServo.timestamp, agoBuf, sizeof(agoBuf));
      snprintf(lastServoAgo, sizeof(lastServoAgo), "%s ago", agoBuf);
    } else {
      snprintf(lastServoAgo, sizeof(lastServoAgo), "Just now");
    }
  } else if (hasServoLatest && latestServo.timestamp > 0) {
    snprintf(lastServoAgo, sizeof(lastServoAgo), "Awaiting NTP Sync...");
  }

  TailscaleLog latestTs = {};
  bool hasTsLatest = getTailscaleLogAt(0, latestTs) && (tailscaleLogCount > 0) && (latestTs.startTime > 0);
  const char* tailscaleStatus = mlRunning ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected");

  int limitLogs = isCurl() ? 6 : MAX_BOOT_LOGS;

  char bootTimeBuf[32]; formatMs(boot_time_ms, bootTimeBuf, sizeof(bootTimeBuf));
  char wifiConnectBuf[32]; formatMs(wifi_connect_ms, wifiConnectBuf, sizeof(wifiConnectBuf));

  bool vpnConn = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  char tsConnectBuf[32] = "";
  if (vpnConn && ts_connect_ms > 0) {
    formatMs(ts_connect_ms, tsConnectBuf, sizeof(tsConnectBuf));
  }

  if (isCurl()) {
    server.sendHeader("Connection", "close");
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, "text/plain; charset=utf-8", "");

    char b[512];
    int off = snprintf(b, sizeof(b),
      "==================================================\n"
      " [!] %s LOGS & DEBUG\n"
      "==================================================\n\n"
      " Uptime Since Boot    : %s\n"
      " Uptime Since Flash   : %s\n",
      ESP.getChipModel(),
      bootDuration, flashDuration
    );

    if (last_off_computed && lastOffDuration > 0) {
      off += snprintf(b + off, sizeof(b) - off,
        " Last Approx. Downtime: %s\n", downtimeDuration);
    }

    off += snprintf(b + off, sizeof(b) - off,
      " Boot Time            : %s\n"
      " Wi-Fi Connect        : %s\n",
      bootTimeBuf, wifiConnectBuf
    );

    if (!isLocalOnly && vpnConn && tsConnectBuf[0]) {
      off += snprintf(b + off, sizeof(b) - off,
        " Tailscale Connect    : %s\n", tsConnectBuf);
    }

    off += snprintf(b + off, sizeof(b) - off,
      " Total Boot Count     : %lu\n"
      " OTA Status           : %s\n",
      totalBootCount,
      otaEnabled ? "Enabled" : "Disabled"
    );
    server.sendContent(b);

    if (hasServoLatest) {
      snprintf(b, sizeof(b),
        "\n--------------------------------------------------\n\n"
        " [ SERVO ACTIVITY ]\n"
        " Total Triggers       : %lu\n"
        " Last Trigger         : %s (%s)\n",
        (unsigned long)servoLogCount, lastServoAgo, latestServo.fromCurl ? "cURL" : "Web");
      server.sendContent(b);

      bool hasServoHistory = false;
      for (int i = 1; i < limitLogs && i < MAX_SERVO_LOGS && i < (int)servoLogCount; i++) {
        ServoLog sentry;
        if (!getServoLogAt(i, sentry)) continue;
        if (!hasServoHistory) {
          server.sendContent("\n [ PREVIOUS SERVO HISTORY ]\n");
          hasServoHistory = true;
        }
        char timeBuf[48];
        if (sentry.timestamp > 0) {
          formatTimestampBuf(sentry.timestamp, timeBuf, sizeof(timeBuf));
        } else {
          snprintf(timeBuf, sizeof(timeBuf), "Pre-NTP Sync");
        }
        snprintf(b, sizeof(b), " [%s] via %s\n",
          timeBuf, sentry.fromCurl ? "cURL" : "Web");
        server.sendContent(b);
      }
    }

    if (!isLocalOnly && hasTsLatest) {
      snprintf(b, sizeof(b),
        "\n--------------------------------------------------\n\n"
        " [ TAILSCALE ACTIVITY ]\n"
        " Status          : %s\n"
        " Total Sessions  : %lu\n",
        tailscaleStatus, (unsigned long)tailscaleLogCount);
      server.sendContent(b);

      char tsDurBuf[32];
      time_t nowTs; time(&nowTs);
      uint32_t tsDur = 0;
      if (latestTs.endTime == 0) {
        tsDur = (nowTs > latestTs.startTime) ? (uint32_t)(nowTs - latestTs.startTime) : 0;
      } else {
        tsDur = (latestTs.endTime > latestTs.startTime) ? (latestTs.endTime - latestTs.startTime) : 0;
      }
      formatDuration(tsDur, tsDurBuf, sizeof(tsDurBuf));

      uint32_t latestDtSec = latestTs.downtimeSec;
      if (latestDtSec == 0 && latestTs.startTime > 0) {
        TailscaleLog prevTs = {};
        if (getTailscaleLogAt(1, prevTs) && prevTs.endTime > 0 && latestTs.startTime > prevTs.endTime) {
          latestDtSec = latestTs.startTime - prevTs.endTime;
        }
      }

      char tsDtBuf[64] = "";
      if (latestDtSec > 0) {
        char bDt[32]; formatDuration(latestDtSec, bDt, sizeof(bDt));
        snprintf(tsDtBuf, sizeof(tsDtBuf), " (Downtime: %s)", bDt);
      }

      if (latestTs.endTime == 0) {
        snprintf(b, sizeof(b),
          " Active Session  : %s%s\n", tsDurBuf, tsDtBuf);
      } else {
        char startBuf[48];
        formatTimestampBuf(latestTs.startTime, startBuf, sizeof(startBuf));
        snprintf(b, sizeof(b),
          " Last Session    : %s%s\n"
          " Last Connected  : %s\n",
          tsDurBuf, tsDtBuf, startBuf);
      }
      server.sendContent(b);

      bool hasTsHistory = false;
      for (int i = 1; i < limitLogs && i < (int)tailscaleLogCount && i < MAX_TAILSCALE_LOGS; i++) {
        TailscaleLog tentry = {};
        if (!getTailscaleLogAt(i, tentry) || tentry.startTime == 0) continue;
        if (!hasTsHistory) {
          server.sendContent("\n [ PREVIOUS TAILSCALE HISTORY ]\n");
          hasTsHistory = true;
        }
        char itemDur[32];
        if (tentry.endTime == 0) {
          uint32_t d = (nowTs > tentry.startTime) ? (uint32_t)(nowTs - tentry.startTime) : 0;
          formatDuration(d, itemDur, sizeof(itemDur));
        } else {
          uint32_t d = (tentry.endTime > tentry.startTime) ? (tentry.endTime - tentry.startTime) : 0;
          formatDuration(d, itemDur, sizeof(itemDur));
        }
        uint32_t dtSec = tentry.downtimeSec;
        if (dtSec == 0 && tentry.startTime > 0) {
          TailscaleLog prevTs = {};
          if (getTailscaleLogAt(i + 1, prevTs) && prevTs.endTime > 0 && tentry.startTime > prevTs.endTime) {
            dtSec = tentry.startTime - prevTs.endTime;
          }
        }
        char itemDt[48] = "";
        if (dtSec > 0) {
          char bDt[32]; formatDuration(dtSec, bDt, sizeof(bDt));
          snprintf(itemDt, sizeof(itemDt), " (Downtime: %s)", bDt);
        }

        char tStartBuf[48];
        formatTimestampBuf(tentry.startTime, tStartBuf, sizeof(tStartBuf));
        snprintf(b, sizeof(b),
          " [%s]\n"
          "   Duration: %s%s\n\n",
          tStartBuf,
          itemDur,
          itemDt);
        server.sendContent(b);
      }
    }

    if (totalBootCount > 0 && hasLatest) {
      char lastResetTimeBuf[48];
      formatTimestampBuf(latest.timestamp, lastResetTimeBuf, sizeof(lastResetTimeBuf));
      snprintf(b, sizeof(b),
        "\n--------------------------------------------------\n\n"
        " [ RESET FORENSICS ]\n"
        " Last Reset Cause     : %s\n"
        " Last Reset Time      : %s\n",
        lastResetCause,
        lastResetTimeBuf
      );
      server.sendContent(b);

      if (activeWifiCount > 1 && latest.wifiIdx < TOTAL_WIFI_SLOTS && configuredWifiNetworks[latest.wifiIdx].ssid && configuredWifiNetworks[latest.wifiIdx].ssid[0]) {
        snprintf(b, sizeof(b),
          " Connected Wi-Fi      : %s\n",
          configuredWifiNetworks[latest.wifiIdx].ssid);
        server.sendContent(b);
      }

      bool hasBootHistory = false;
      for (int i = 1; i < limitLogs; i++) {
        BootLog entry = {};
        if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
        if (!hasBootHistory) {
          server.sendContent("\n [ PREVIOUS BOOT HISTORY ]\n");
          hasBootHistory = true;
        }
        char dtStr[48] = "";
        if (entry.downtimeSec > 0) {
          char dtBuf[32]; formatDuration(entry.downtimeSec, dtBuf, sizeof(dtBuf));
          snprintf(dtStr, sizeof(dtStr), " (Downtime: %s)", dtBuf);
        }
        char wifiStr[64] = "";
        if (activeWifiCount > 1 && entry.wifiIdx < TOTAL_WIFI_SLOTS && configuredWifiNetworks[entry.wifiIdx].ssid && configuredWifiNetworks[entry.wifiIdx].ssid[0]) {
          snprintf(wifiStr, sizeof(wifiStr), " (Wi-Fi: %s)", configuredWifiNetworks[entry.wifiIdx].ssid);
        }
        char bootEntryTime[48];
        formatTimestampBuf(entry.timestamp, bootEntryTime, sizeof(bootEntryTime));
        snprintf(b, sizeof(b),
          " [%s]\n"
          "   %s%s%s\n\n",
          bootEntryTime,
          getResetReasonString((esp_reset_reason_t)entry.reasonCode),
          dtStr,
          wifiStr);
        server.sendContent(b);
      }
    }
    server.sendContent("==================================================\n");
    server.sendContent(""); // Terminate chunked transfer
  } else {
    sendWrappedPageStream(server, "Logs & Debug", "&#128295;", [&]() {
      char b[768];
      int offGeneral = snprintf(b, sizeof(b),
        "<h1>&#128295; Logs &amp; Debug</h1>"
        "<h3>General</h3>"
        "<div class='card'>"
        "<div class='row'><span class='k'>Uptime Since Boot</span><span class='v mono' id='up'>%s</span></div>"
        "<div class='row'><span class='k'>Uptime Since Flash</span><span class='v mono' id='upf'>%s</span></div>",
        bootDuration, flashDuration
      );
      if (last_off_computed && lastOffDuration > 0) {
        offGeneral += snprintf(b + offGeneral, sizeof(b) - offGeneral,
          "<div class='row'><span class='k'>Last Approx. Downtime</span><span class='v mono'>%s</span></div>",
          downtimeDuration
        );
      }
      snprintf(b + offGeneral, sizeof(b) - offGeneral, "</div>");
      server.sendContent(b);

      int offBoot = snprintf(b, sizeof(b),
        "<h3>Boot Stats</h3>"
        "<div class='card'>"
        "<div class='row'><span class='k'>Boot Time</span><span class='v mono'>%s</span></div>"
        "<div class='row'><span class='k'>Wi-Fi Connect</span><span class='v mono'>%s</span></div>",
        bootTimeBuf, wifiConnectBuf
      );
      if (!isLocalOnly) {
        offBoot += snprintf(b + offBoot, sizeof(b) - offBoot,
          "<div class='row' id='ts-connect-row'%s><span class='k'>Tailscale Connect</span><span class='v mono' id='ts-conn-time'>%s</span></div>",
          (vpnConn && tsConnectBuf[0]) ? "" : " style='display:none;'",
          (vpnConn && tsConnectBuf[0]) ? tsConnectBuf : "--"
        );
      }
      offBoot += snprintf(b + offBoot, sizeof(b) - offBoot,
        "<div class='row'><span class='k'>Total Boots</span><span class='v mono'>%lu</span></div>",
        totalBootCount
      );
      if (totalBootCount > 0 && hasLatest) {
        offBoot += snprintf(b + offBoot, sizeof(b) - offBoot,
          "<div class='row'><span class='k'>Last Reset Cause</span><span class='v mono'>%s</span></div>"
          "<div class='row'><span class='k'>Last Reset Time</span><span class='v mono'>%s</span></div>",
          lastResetCause, lastResetTime
        );
        if (activeWifiCount > 1 && latest.wifiIdx < TOTAL_WIFI_SLOTS && configuredWifiNetworks[latest.wifiIdx].ssid && configuredWifiNetworks[latest.wifiIdx].ssid[0]) {
          offBoot += snprintf(b + offBoot, sizeof(b) - offBoot,
            "<div class='row'><span class='k'>Connected Wi-Fi</span><span class='v mono'>%s</span></div>",
            configuredWifiNetworks[latest.wifiIdx].ssid
          );
        }
      }
      snprintf(b + offBoot, sizeof(b) - offBoot, "</div>");
      server.sendContent(b);

      if (hasServoLatest) {
        snprintf(b, sizeof(b),
          "<h3>Servo</h3>"
          "<div class='card'>"
          "<div class='row'><span class='k'>Last Trigger</span><span class='v mono' id='servo-ago'>%s</span></div>"
          "<div class='row'><span class='k'>Trigger Source</span><span class='v mono'>%s</span></div>"
          "<div class='row'><span class='k'>Total Triggers</span><span class='v mono'>%lu</span></div>"
          "</div>",
          lastServoAgo, latestServo.fromCurl ? "cURL" : "Web", servoLogCount);
        server.sendContent(b);
      }

      if (servoLogCount > 1) {
        server.sendContent("<h3>Servo Trigger History</h3><div class='card'>");
        for (int i = 1; i < MAX_SERVO_LOGS && i < (int)servoLogCount; i++) {
          ServoLog sentry = {};
          if (!getServoLogAt(i, sentry)) continue;
          char timeBuf[48];
          if (sentry.timestamp > 0) {
            formatTimestampBuf(sentry.timestamp, timeBuf, sizeof(timeBuf));
          } else {
            snprintf(timeBuf, sizeof(timeBuf), "Pre-NTP Sync");
          }
          char sline[320];
          snprintf(sline, sizeof(sline),
            "<div class='log-item'>"
            "<div class='log-meta'>"
            "<span class='log-title'>Servo Actuation</span>"
            "<span class='log-sub'>%s</span>"
            "</div>"
            "<span class='log-badge'>%s</span>"
            "</div>",
            timeBuf,
            sentry.fromCurl ? "CURL" : "WEB");
          server.sendContent(sline);
        }
        server.sendContent("</div>");
      }

      if (!isLocalOnly && hasTsLatest) {
        server.sendContent("<h3>Tailscale Connection History</h3><div class='card'>");
        time_t nowTs; time(&nowTs);
        for (int i = 0; i < MAX_TAILSCALE_LOGS && i < (int)tailscaleLogCount; i++) {
          TailscaleLog tentry = {};
          if (!getTailscaleLogAt(i, tentry) || tentry.startTime == 0) continue;

          char durBuf[32];
          if (tentry.endTime == 0) {
            uint32_t d = (nowTs > tentry.startTime) ? (uint32_t)(nowTs - tentry.startTime) : 0;
            formatDuration(d, durBuf, sizeof(durBuf));
          } else {
            uint32_t d = (tentry.endTime > tentry.startTime) ? (tentry.endTime - tentry.startTime) : 0;
            formatDuration(d, durBuf, sizeof(durBuf));
          }

          uint32_t dtSec = tentry.downtimeSec;
          if (dtSec == 0 && tentry.startTime > 0) {
            TailscaleLog prevTs = {};
            if (getTailscaleLogAt(i + 1, prevTs) && prevTs.endTime > 0 && tentry.startTime > prevTs.endTime) {
              dtSec = tentry.startTime - prevTs.endTime;
            }
          }

          char dtHtml[64] = "";
          if (dtSec > 0) {
            char dtBuf[32]; formatDuration(dtSec, dtBuf, sizeof(dtBuf));
            snprintf(dtHtml, sizeof(dtHtml), " &bull; Downtime: %s", dtBuf);
          }

          const char* badgeClass = "";
          const char* badgeText = "ENDED";
          if (tentry.endTime == 0) {
            badgeClass = "on";
            badgeText = "ACTIVE";
          } else if (i == 0) {
            badgeClass = "latest";
            badgeText = "LATEST";
          }

          char startStr[48];
          formatTimestampBuf(tentry.startTime, startStr, sizeof(startStr));
          char endStr[48] = "";
          if (tentry.endTime != 0) formatTimestampBuf(tentry.endTime, endStr, sizeof(endStr));

          char line[768];
          if (tentry.endTime == 0) {
            snprintf(line, sizeof(line),
              "<div class='log-item%s'%s>"
              "<div class='log-meta'>"
              "<span class='log-title'%s>Active Session</span>"
              "<span class='log-sub'%s>Started: %s</span>"
              "<span class='log-sub'%s style='display:none;'></span>"
              "<span class='log-sub'%s>Duration: <span id='ts-dur-val'>%s</span><span id='ts-dt-0'>%s</span></span>"
              "</div>"
              "<span class='log-badge %s'%s>%s</span>"
              "</div>",
              (i == 0) ? " latest-entry" : "",
              (i == 0) ? " id='ts-item-0'" : "",
              (i == 0) ? " id='ts-title-0'" : "",
              (i == 0) ? " id='ts-start-0'" : "",
              startStr,
              (i == 0) ? " id='ts-end-0'" : "",
              (i == 0) ? " id='ts-dur-line-0'" : "",
              durBuf,
              dtHtml,
              badgeClass,
              (i == 0) ? " id='ts-badge-0'" : "",
              badgeText);
          } else {
            snprintf(line, sizeof(line),
              "<div class='log-item%s'%s>"
              "<div class='log-meta'>"
              "<span class='log-title'%s>Tailscale Session</span>"
              "<span class='log-sub'%s>Started: %s</span>"
              "<span class='log-sub'%s>Ended: %s</span>"
              "<span class='log-sub'%s>Duration: <b>%s</b><span id='ts-dt-0'>%s</span></span>"
              "</div>"
              "<span class='log-badge %s'%s>%s</span>"
              "</div>",
              (i == 0) ? " latest-entry" : "",
              (i == 0) ? " id='ts-item-0'" : "",
              (i == 0) ? " id='ts-title-0'" : "",
              (i == 0) ? " id='ts-start-0'" : "",
              startStr,
              (i == 0) ? " id='ts-end-0'" : "",
              endStr,
              (i == 0) ? " id='ts-dur-line-0'" : "",
              durBuf,
              dtHtml,
              badgeClass,
              (i == 0) ? " id='ts-badge-0'" : "",
              badgeText);
          }
          server.sendContent(line);
        }
        server.sendContent("</div>");
      }

      bool hasBootHist = false;
      for (int i = 1; i < limitLogs; i++) {
        BootLog entry = {};
        if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
        if (!hasBootHist) {
          server.sendContent("<h3>Previous Boot History</h3><div class='card'>");
          hasBootHist = true;
        }
        char dtHtml[64] = "";
        if (entry.downtimeSec > 0) {
          char dtBuf[32]; formatDuration(entry.downtimeSec, dtBuf, sizeof(dtBuf));
          snprintf(dtHtml, sizeof(dtHtml), " &bull; Approx. Downtime: %s", dtBuf);
        }
        char wifiHtml[64] = "";
        if (activeWifiCount > 1 && entry.wifiIdx < TOTAL_WIFI_SLOTS && configuredWifiNetworks[entry.wifiIdx].ssid && configuredWifiNetworks[entry.wifiIdx].ssid[0]) {
          snprintf(wifiHtml, sizeof(wifiHtml), " &bull; Wi-Fi: %s", configuredWifiNetworks[entry.wifiIdx].ssid);
        }
        char line[320];
        const char* cls = getResetReasonClass((esp_reset_reason_t)entry.reasonCode);
        char bootEntryTime[48];
        formatTimestampBuf(entry.timestamp, bootEntryTime, sizeof(bootEntryTime));
        snprintf(line, sizeof(line),
          "<div class='log-item'>"
          "<div class='log-meta'>"
          "<span class='log-title %s'>%s</span>"
          "<span class='log-sub'>%s%s%s</span>"
          "</div>"
          "</div>",
          cls,
          getResetReasonString((esp_reset_reason_t)entry.reasonCode),
          bootEntryTime,
          dtHtml,
          wifiHtml);
        server.sendContent(line);
      }
      if (hasBootHist) {
        server.sendContent("</div>");
      }

      snprintf(b, sizeof(b),
        "<h3>OTA Updates</h3>"
        "<div class='card'>"
        "<div class='row'><span class='k'>Status</span>"
        "<span class='v'><span class='pill %s'>%s</span></span></div></div>",
        otaEnabled ? "on" : "off", otaEnabled ? "Enabled" : "Disabled"
      );
      server.sendContent(b);

      if (otaEnabled) {
        server.sendContent("<div class='actions'><form action='/ota/disable' method='POST'><button type='submit'>Disable OTA</button></form></div>");
      } else {
        if (strlen(OTA_KEY) > 0) {
          server.sendContent(
            "<div class='actions'>"
            "<form action='/ota/enable' method='POST' style='display:flex;gap:10px;align-items:center;'>"
            "<input type='password' name='key' placeholder='OTA Key' required style='flex:1;min-width:0;'>"
            "<button class='warn' type='submit' style='flex:1;'>Enable OTA</button></form>"
            "</div>");
        } else {
          server.sendContent(
            "<div class='actions'>"
            "<form action='/ota/enable' method='POST'>"
            "<button class='warn' type='submit'>Enable OTA</button></form>"
            "</div>");
        }
      }

      server.sendContent("<hr class='divider'>");

      if (isCalibrated) {
        server.sendContent(
          "<div class='actions' style='margin-bottom:12px;'>"
          "<a href='/calibrate' style='width:100%;text-decoration:none;'><button type='button'>&#127919; Recalibrate Servo</button></a>"
          "</div>");
      }

      server.sendContent(
        "<div class='actions'>"
        "<form action='/reboot' method='POST' onsubmit='return confirm(\"Reboot the ESP32 now?\");'>"
        "<button class='danger' type='submit'>&#128260; Reboot</button></form>"
        "<form action='/clear-logs' method='POST' onsubmit='return confirm(\"Clear all logs and flash timers?\");'>"
        "<button class='warn' type='submit'>&#128465; Clear Logs</button></form>"
        "</div>"
        "<a class='back' href='/main'>&larr; Back to Dashboard</a>"
      );
    }, "");
  }
}

void handleOtaEnable() {
  if (strlen(OTA_KEY) > 0) {
    if (!server.hasArg("key") || server.arg("key") != OTA_KEY) {
      server.sendHeader("Connection", "close");
      if (isCurl()) {
        server.send(403, "text/plain; charset=utf-8", "Forbidden: bad or missing key.\n");
      } else {
        const char* body =
          "<div class='card' style='text-align:center;padding:36px 20px;'>"
          "<div style='font-size:44px;margin-bottom:14px;line-height:1;'>&#9888;</div>"
          "<h1 style='margin-bottom:16px;font-size:22px;'>Invalid OTA Key</h1>"
          "<p style='color:var(--on-surface-v);font-size:13.5px;line-height:1.6;'>The key entered was incorrect.<br>Redirecting to debug&hellip;</p>"
          "</div>";
        String page = wrapPage("Invalid Key", "&#9888;", body,
          "<script>setTimeout(function(){window.location.href='/debug';},2500);</script>", true);
        server.send(403, "text/html; charset=utf-8", page);
      }
      return;
    }
  }
  startOTA();
  server.sendHeader("Connection", "close");
  server.sendHeader("Location", "/debug", true);
  server.send(302, "text/plain", "");
}

void handleOtaDisable() {
  stopOTA();
  server.sendHeader("Connection", "close");
  server.sendHeader("Location", "/debug", true);
  server.send(302, "text/plain", "");
}

void handleClearLogs() {
  Preferences p;
  p.begin("esp_log", false);
  p.clear();
  p.end();

  memset(bootHistory, 0, sizeof(bootHistory));
  totalBootCount = 0;
  bootWriteIdx = 0;
  log_time_fixed = false;

  // Clear servo trigger logs
  p.begin("servo_log", false);
  p.clear();
  p.end();
  memset(servoHistory, 0, sizeof(servoHistory));
  servoLogCount = 0;
  servoWriteIdx = 0;

  // Clear Tailscale session logs
  p.begin("ts_log", false);
  p.clear();
  p.end();
  memset(tailscaleHistory, 0, sizeof(tailscaleHistory));
  tailscaleLogCount = 0;
  tailscaleWriteIdx = 0;
  lastTailscaleStopTime = 0;
  if (mlRunning) {
    recordTailscaleStart();
  }

  if (time_synced) {
    time_t now; time(&now);
    first_boot_epoch = (uint32_t)now - (millis() / 1000);
    lastAliveEpoch = (uint32_t)now;
    p.begin("esp_log", false);
    p.putUInt("first_boot", first_boot_epoch);
    p.putUInt("last_alive", lastAliveEpoch);
    p.end();
  } else {
    first_boot_epoch = 0;
  }

  if (isCurl()) {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", "\n[ SYSTEM ] Crash logs and flash timers cleared successfully.\n\n");
  } else {
    const char* body =
      "<div class='card' style='text-align:center;padding:36px 20px;'>"
      "<div style='font-size:44px;margin-bottom:14px;line-height:1;'>&#128465;</div>"
      "<h1 style='margin-bottom:16px;font-size:22px;'>Logs Cleared</h1>"
      "<p style='color:var(--on-surface-v);font-size:13.5px;line-height:1.6;'>Crash history and flash timers have been reset.<br>Redirecting to debug&hellip;</p>"
      "</div>";
    String page = wrapPage("Logs Cleared", "&#128465;", body,
      "<script>setTimeout(function(){window.location.href='/debug';},2000);</script>", true);
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", page);
  }
}

void handleReboot() {
  if (isCurl()) {
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", "\n[ SYSTEM ] Rebooting ESP32 now...\n\n");
  } else {
    const char* body =
      "<div class='card' style='text-align:center;padding:36px 20px;'>"
      "<div style='font-size:44px;margin-bottom:14px;line-height:1;'>&#128260;</div>"
      "<h1 style='margin-bottom:16px;font-size:22px;'>Rebooting&hellip;</h1>"
      "<p style='color:var(--on-surface-v);font-size:13.5px;line-height:1.6;'>The ESP32 is restarting.<br>Please wait 5 seconds before refreshing.</p>"
      "</div>";
    String page = wrapPage("Rebooting...", "&#128260;", body,
      "<script>setTimeout(function(){window.location.href='/main';},5000);</script>", true);
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", page);
  }
  delay(500);
  ESP.restart();
}

void handleNotFound() {
  server.sendHeader("Connection", "close");
  server.send(404, "text/plain; charset=utf-8", "Error 404: Endpoint not found.\n");
}

// --- Subnet Failover Logic ---

void checkSubnetAndFailoverIfNeeded() {
  if (isLocalOnly) return;
#if defined(TAILSCALE_API_KEY) && defined(TAILSCALE_SUBNET_DEVICE_ID)
  if (strlen(TAILSCALE_API_KEY) == 0 || strlen(TAILSCALE_SUBNET_DEVICE_ID) == 0) return;

  unsigned long now = millis();
  // Query every 45 seconds (<0.8% radio duty cycle; chip stays cool at ~38-41°C)
  if (now - lastSubnetCheckMs < 45000) return;
  lastSubnetCheckMs = now;

  bool motoOnline = checkTailscaleSubnetRouterOnline(TAILSCALE_API_KEY, TAILSCALE_SUBNET_DEVICE_ID);

  if (mlRunning) {
    // Mode 1: ESP32 Tailscale is running (failover mode).
    // If Moto has reconnected to Tailscale control plane, return ESP32 to cool Standby!
    if (motoOnline) {
      ESP_LOGI("watchdog", "Subnet router '%s' reconnected to Tailscale! Returning ESP32 to cold STANDBY.", TAILSCALE_SUBNET_DEVICE_ID);
      stopTailscale("subnet router reconnected to Tailscale");
      mlStandbyMode = true;
      subnetFailCount = 0;
    }
  } else {
    // Mode 2: ESP32 is in Standby (Moto is expected to route Tailscale).
    if (motoOnline) {
      if (subnetFailCount > 0) {
        ESP_LOGI("watchdog", "Subnet router '%s' verified online with Tailscale control plane. Remaining in STANDBY.", TAILSCALE_SUBNET_DEVICE_ID);
      }
      subnetFailCount = 0;
      mlStandbyMode = true;
    } else {
      subnetFailCount++;
      ESP_LOGW("watchdog", "Subnet router '%s' disconnected from Tailscale (check %d/2, HTTP status: %d)",
               TAILSCALE_SUBNET_DEVICE_ID, subnetFailCount, s_lastApiHttpStatus);
      if (subnetFailCount >= 2) {
        ESP_LOGE("watchdog", "Subnet router '%s' disconnected from Tailscale after 2 consecutive checks (~90s). Activating Tailscale failover!", TAILSCALE_SUBNET_DEVICE_ID);
        startTailscale("failover - subnet router disconnected from Tailscale");
      }
    }
  }
#endif
}

// Periodic ARP / Gateway keepalive: sends a Gratuitous ARP and a Gateway ARP probe
// every 45 seconds so consumer Wi-Fi routers (and local devices) never drop 192.168.1.50
// from their ARP routing table while the ESP32 is in Wi-Fi modem sleep.
void sendLanArpKeepaliveIfNeeded() {
  if (WiFi.status() != WL_CONNECTED) return;
  static unsigned long lastArpKeepaliveMs = 0;
  unsigned long now = millis();
  if (now - lastArpKeepaliveMs < 45000) return;
  lastArpKeepaliveMs = now;

  esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
  if (sta) {
    struct netif *nif = (struct netif *)esp_netif_get_netif_impl(sta);
    if (nif) {
      etharp_gratuitous(nif);
      ip4_addr_t gw_ip;
      gw_ip.addr = (uint32_t)gateway;
      etharp_request(nif, &gw_ip);
    }
  }
}

void checkWifiReconnectIfNeeded() {
  if (activeWifiCount <= 1) return;

  static unsigned long lastWifiCheckMs = 0;
  static unsigned long disconnectStartMs = 0;
  unsigned long now = millis();

  if (now - lastWifiCheckMs < 2000) return;
  lastWifiCheckMs = now;

  if (WiFi.status() != WL_CONNECTED) {
    if (disconnectStartMs == 0) {
      disconnectStartMs = now;
    } else if (now - disconnectStartMs > 15000) {
      currentWifiIndex = (currentWifiIndex + 1) % activeWifiCount;
      ESP_LOGW("wifi", "Connection lost for >15s. Switching to fallback Wi-Fi [%d/%d]: '%s'",
               currentWifiIndex + 1, activeWifiCount, configuredWifiNetworks[currentWifiIndex].ssid);
      WiFi.disconnect(false);
      delay(100);
      WiFi.begin(configuredWifiNetworks[currentWifiIndex].ssid, configuredWifiNetworks[currentWifiIndex].password);
      disconnectStartMs = now;
    }
  } else {
    if (disconnectStartMs != 0) {
      updateBootWifiIndex((uint8_t)currentWifiIndex);
      disconnectStartMs = 0;
    }
  }
}

void setup() {
  // Run CPU at 80 MHz to save power and keep the chip cool (~38-41°C)
  setCpuFrequencyMhz(80);

#if CONFIG_PM_ENABLE
  esp_pm_config_esp32s3_t pm_config = {
    .max_freq_mhz = 80,
    .min_freq_mhz = 80,
    .light_sleep_enable = false
  };
  esp_pm_configure(&pm_config);
#endif

  // Load saved angles from flash and move arm to resting position
  loadCalibration();
  initServo();
  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle); delay(300); myservo.detach();

  // Load past logs from flash and record this new boot event
  loadBootHistory();
  loadServoHistory();
  loadTailscaleHistory();
  recordBootEvent();

  // Determine number of configured Wi-Fi networks (1 to 6)
  activeWifiCount = 0;
  for (size_t i = 0; i < TOTAL_WIFI_SLOTS; i++) {
    if (configuredWifiNetworks[i].ssid && strlen(configuredWifiNetworks[i].ssid) > 0) {
      activeWifiCount++;
    }
  }

  uint32_t wifi_start = millis();
  WiFi.mode(WIFI_STA);
  wifi_config_t sta_conf;
  if (esp_wifi_get_config(WIFI_IF_STA, &sta_conf) == ESP_OK) {
    sta_conf.sta.listen_interval = 1;
    esp_wifi_set_config(WIFI_IF_STA, &sta_conf);
  }
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(true);
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  ip_addr_t router_dns;
  IP_ADDR4(&router_dns, routerDNS[0], routerDNS[1], routerDNS[2], routerDNS[3]);
  dns_setserver(2, &router_dns);

  bool connected = false;
  for (int i = 0; i < activeWifiCount; i++) {
    ESP_LOGI("wifi", "Attempting Wi-Fi network [%d/%d]: '%s'...", i + 1, activeWifiCount, configuredWifiNetworks[i].ssid);
    WiFi.begin(configuredWifiNetworks[i].ssid, configuredWifiNetworks[i].password);

    uint32_t attempt_start = millis();
    uint32_t timeout_ms = (i == 0) ? 12000 : 8000;
    while (WiFi.status() != WL_CONNECTED && (millis() - attempt_start < timeout_ms)) {
      delay(100);
    }

    if (WiFi.status() == WL_CONNECTED) {
      connected = true;
      currentWifiIndex = i;
      updateBootWifiIndex((uint8_t)i);
      wifi_connect_ms = millis() - wifi_start;
      ESP_LOGI("wifi", "Connected to Wi-Fi network [%d/%d]: '%s' (IP: %s) in %lu ms",
               i + 1, activeWifiCount, configuredWifiNetworks[i].ssid,
               WiFi.localIP().toString().c_str(), wifi_connect_ms);
      break;
    } else {
      ESP_LOGW("wifi", "Failed to connect to Wi-Fi [%d/%d]: '%s'.", i + 1, activeWifiCount, configuredWifiNetworks[i].ssid);
      WiFi.disconnect(false);
      delay(150);
    }
  }

  if (!connected) {
    wifi_connect_ms = 0;
    ESP_LOGE("wifi", "Failed to connect to any configured Wi-Fi network!");
    if (activeWifiCount > 0) {
      WiFi.begin(configuredWifiNetworks[0].ssid, configuredWifiNetworks[0].password);
      currentWifiIndex = 0;
    }
  }

  // Re-apply listen_interval = 1 to guarantee station wakes on every beacon (102.4ms)
  // preventing AP buffer overflow and packet drops during subnet routing
  if (esp_wifi_get_config(WIFI_IF_STA, &sta_conf) == ESP_OK) {
    sta_conf.sta.listen_interval = 1;
    esp_wifi_set_config(WIFI_IF_STA, &sta_conf);
  }

  // Give 802.11 association, block ack, and router forwarding time to settle
  delay(1500);

  // Tailscale / Microlink initialization (completely bypassed if in fully local mode)
  if (!isLocalOnly && strlen(TAILSCALE_KEY) > 0) {
    microlink_config_t ml_conf;
    memset(&ml_conf, 0, sizeof(ml_conf));
    ml_conf.auth_key = TAILSCALE_KEY;
    ml_conf.device_name = TAILSCALE_HOST;
    // Advertise exact same /24 route as moto-g32 for official Tailscale HA subnet failover
    ml_conf.advertise_routes = tailscaleAdvertiseRoute;
    ml_conf.enable_derp = true;
    ml_conf.enable_stun = true;
    ml_conf.enable_disco = true;
    ml_conf.max_peers = 8;
    ml_conf.wifi_tx_power_dbm = 13;

    ml = microlink_init(&ml_conf); // initialized, ready for start

#if defined(TAILSCALE_API_KEY) && defined(TAILSCALE_SUBNET_DEVICE_ID)
    if (strlen(TAILSCALE_API_KEY) > 0 && strlen(TAILSCALE_SUBNET_DEVICE_ID) > 0) {
      ESP_LOGI("watchdog", "Tailscale API watchdog active. Target subnet router: %s", TAILSCALE_SUBNET_DEVICE_ID);
      bool motoOnline = false;
      for (int attempt = 1; attempt <= 3; attempt++) {
        if (checkTailscaleSubnetRouterOnline(TAILSCALE_API_KEY, TAILSCALE_SUBNET_DEVICE_ID)) {
          motoOnline = true;
          break;
        }
        if (attempt < 3) {
          ESP_LOGW("watchdog", "Boot check attempt %d/3 failed, retrying in 1s...", attempt);
          delay(1000);
        }
      }

      if (motoOnline) {
        ESP_LOGI("watchdog", "Subnet router '%s' is CONNECTED to Tailscale. Entering cold STANDBY (~38-41°C).", TAILSCALE_SUBNET_DEVICE_ID);
        mlRunning = false;
        mlStandbyMode = true;
        subnetFailCount = 0;
      } else {
        ESP_LOGW("watchdog", "Subnet router '%s' is NOT connected to Tailscale. Starting Tailscale failover immediately.", TAILSCALE_SUBNET_DEVICE_ID);
        startTailscale("boot - subnet router disconnected from Tailscale");
      }
    } else {
      ESP_LOGI("watchdog", "No TAILSCALE_API_KEY configured. Connecting directly to Tailscale at boot.");
      startTailscale("direct startup");
    }
#else
    startTailscale("direct startup");
#endif
  } else {
    ESP_LOGI("system", "Running in FULLY LOCAL MODE (Tailscale & Microlink bypassed).");
    mlRunning = false;
    mlStandbyMode = false;
  }

  configTime(parseTimezoneOffsetSec(TIMEZONE_OFFSET), 0, "216.239.35.0", "pool.ntp.org", "time.google.com");

  if (MDNS.begin("esp32")) {
    MDNS.addService("http", "tcp", 80);
    mdns_ip_addr_t m_addr;
    memset(&m_addr, 0, sizeof(m_addr));
    m_addr.addr.type = ESP_IPADDR_TYPE_V4;
    m_addr.addr.u_addr.ip4.addr = static_cast<uint32_t>(local_IP);
    mdns_delegate_hostname_add("esp", &m_addr);
  }

  // NOTE: ArduinoOTA is intentionally NOT started here.
  // It only starts when /ota/enable is hit (see handleOtaEnable / startOTA),
  // and auto-disables after OTA_AUTO_TIMEOUT_MS - see loop().

  const char* headerkeys[] = {"User-Agent", "Host", "Accept-Encoding"};
  server.collectHeaders(headerkeys, 3);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/main", HTTP_GET, handleMain);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/reboot", HTTP_ANY, handleReboot);
  server.on("/clear-logs", HTTP_ANY, handleClearLogs);
  server.on("/api/live", HTTP_GET, handleApiLive);
  server.on("/style.css", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send_P(200, "text/css; charset=utf-8", COMMON_CSS);
  });
  server.on("/app.js", HTTP_GET, []() {
    server.sendHeader("Cache-Control", "public, max-age=604800, immutable");
    server.send_P(200, "application/javascript; charset=utf-8", APP_JS);
  });
  server.on("/ota/enable", HTTP_POST, handleOtaEnable);
  server.on("/ota/disable", HTTP_POST, handleOtaDisable);
  registerCalibrationRoutes(server);
  server.onNotFound(handleNotFound);
  server.begin();

  // Run the HTTP server on its own task (Core 0, pri 4) so a slow TCP
  // connection over Tailscale DERP can never block loop(), OTA, or the servo.
  xTaskCreatePinnedToCore(
    [](void*) {
      for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
      }
    },
    "http_srv",   /* task name   */
    6144,         /* stack bytes */
    nullptr,      /* arg         */
    4,            /* priority    */
    nullptr,      /* handle out  */
    0             /* Core 0 — microlink net_io and derp_tx also run here,
                     keeping WebServer off Core 1 where wg_mgr lives */
  );

#if defined(TAILSCALE_API_KEY) && defined(TAILSCALE_SUBNET_DEVICE_ID)
  if (!isLocalOnly && strlen(TAILSCALE_API_KEY) > 0 && strlen(TAILSCALE_SUBNET_DEVICE_ID) > 0) {
    // Dedicated low-priority watchdog task (Core 0) so blocking TLS HTTPS checks
    // to api.tailscale.com NEVER stall Core 1, loop(), or instantaneous servo actuation.
    xTaskCreatePinnedToCore(
      [](void*) {
        vTaskDelay(pdMS_TO_TICKS(45000));
        for (;;) {
          checkSubnetAndFailoverIfNeeded();
          vTaskDelay(pdMS_TO_TICKS(45000));
        }
      },
      "ts_watchdog",
      8192,
      nullptr,
      1,
      nullptr,
      0
    );
  }
#endif

  loopTaskHandle = xTaskGetCurrentTaskHandle();
  boot_time_ms = millis();
}

void loop() {
  // Check for wireless updates and close the window automatically after 10 minutes
  if (otaEnabled) {
    ArduinoOTA.handle();
    if (millis() - otaEnabledAt > OTA_AUTO_TIMEOUT_MS) stopOTA();
  }

  // Move the servo to press the button in the background
  if (pendingPress) {
    pendingPress = false;
    triggerPress();
  }

  // Execute test tap from calibration page in background
  if (pendingTestTap) {
    pendingTestTap = false;
    initServo();
    myservo.attach(servoPin, 500, 2400);
    myservo.write(testPressAngle);
    delay(testDurationMs);
    myservo.write(testRestAngle);
    int returnTime = max(60, abs(testPressAngle - testRestAngle) * 3);
    delay(returnTime);
    myservo.detach();
  }

  // Safety watchdog for calibration manual hold: auto-release if held > MAX_HOLD_DURATION_MS (10000ms)
  if (isHoldActive && (millis() - holdStartTimeMs > MAX_HOLD_DURATION_MS)) {
    isHoldActive = false;
    initServo();
    myservo.attach(servoPin, 500, 2400);
    myservo.write(restAngle);
    delay(100);
    myservo.detach();
    ESP_LOGW("servo", "Calibration hold auto-released after safety timeout (%lu ms)", MAX_HOLD_DURATION_MS);
  }

  // Sync clock with internet time and save heartbeat to measure outage downtime
  if (!time_synced) syncTimeIfNeeded();
  heartbeatIfNeeded();
  // Keep router and local LAN ARP caches fresh during Wi-Fi modem sleep
  sendLanArpKeepaliveIfNeeded();
  // Check if primary Wi-Fi is alive, or failover to backup network
  checkWifiReconnectIfNeeded();

  if (mlRunning && mlStartedAtMs > 0) {
    rtc_ts_duration_s = (millis() - mlStartedAtMs) / 1000;
    if (!ts_connected_latched && (ml != nullptr) && microlink_is_connected(ml)) {
      ts_connect_ms = millis() - mlStartedAtMs;
      ts_connected_latched = true;
    }
  }

  // Responsive 10ms sleep: wakes up instantly when a button is clicked or upon timeout;
  // FreeRTOS tickless idle (waiti 0) halts the CPU during this interval to preserve low temperature.
  ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(10));
}