#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ESP32Servo.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <esp_system.h>
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
}

// --- Configuration ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

IPAddress local_IP(192, 168, 1, 50);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(1, 1, 1, 1);
IPAddress secondaryDNS(8, 8, 8, 8);

const int servoPin   = 1;
int restAngle        = DEFAULT_REST_ANGLE;
int pressAngle       = DEFAULT_PRESS_ANGLE;
int pressDurationMs  = DEFAULT_PRESS_DURATION_MS;
bool isCalibrated    = false;

// OTA gate key - shown embedded in the /debug page's "Enable OTA" form and in
// the curl menu. This is obscurity, not real auth - the device already
// assumes a trusted local network (same tier as the WiFi password / Tailscale
// auth key). Defined in secrets.h as OTA_KEY - every reference to OTA_KEY
// below picks it up from there automatically.
const unsigned long OTA_AUTO_TIMEOUT_MS = 10UL * 60UL * 1000UL; // auto-disable after 10 min

// How often we persist a "last known alive" heartbeat to NVS, used to
// estimate downtime between sessions. Shorter = tighter downtime estimate,
// more (still very cheap) flash writes.
const unsigned long HEARTBEAT_INTERVAL_MS = 30UL * 1000UL; // 30 seconds

Servo myservo;
WebServer server(80);
Preferences prefs;

uint32_t last_press_time = 0;
const uint32_t PRESS_COOLDOWN_MS = 2000;

// Async servo trigger: handleRoot() sets this flag and immediately returns the
// HTTP 200 response. loop() picks it up and fires the servo without blocking
// the TCP connection through the 700ms servo motion.
static volatile bool pendingPress = false;

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

// --- Crash logging: ring buffer over individual NVS keys ---
// Each slot is its own NVS key ("b0".."b49"), so a boot only touches
// boot_cnt, w_idx, and one slot key - not the whole history blob.
const int MAX_BOOT_LOGS = 50;
struct BootLog {
  uint32_t timestamp;   // Unix timestamp of boot event (0 = empty/no NTP yet)
  uint32_t downtimeSec; // Approx downtime seconds before this boot (0 = unknown/first)
  uint8_t  reasonCode;  // esp_reset_reason_t value (0-10, fits a byte)
};
BootLog bootHistory[MAX_BOOT_LOGS]; // RAM cache, indexed by physical slot
uint32_t bootWriteIdx = 0;          // next physical slot to write
uint32_t totalBootCount = 0;

// --- Servo trigger logging: NVS ring buffer ---
const int MAX_SERVO_LOGS = 20;
struct ServoLog {
  uint32_t timestamp;  // Unix epoch (0 = pre-NTP)
  uint8_t  fromCurl;   // 1 if triggered via curl, 0 if web
};
ServoLog servoHistory[MAX_SERVO_LOGS];
uint32_t servoWriteIdx = 0;
uint32_t servoLogCount = 0;

// --- Tailscale session logging: NVS ring buffer ---
const int MAX_TAILSCALE_LOGS = 20;
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
  delay(300);
  myservo.detach();
}

float getEstimatedPowerW() {
  float current_mA = 15.0;
  if (ESP.getCpuFreqMHz() == 80) current_mA += 12.0;
  else if (ESP.getCpuFreqMHz() == 240) current_mA += 30.0;
  if (WiFi.status() == WL_CONNECTED) current_mA += 80.0;
  return (current_mA * 3.3) / 1000.0;
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
  prefs.begin("esp_log", true); // read-only
  totalBootCount = prefs.getUInt("boot_cnt", 0);
  bootWriteIdx   = prefs.getUInt("w_idx", 0) % MAX_BOOT_LOGS;
  first_boot_epoch = prefs.getUInt("first_boot", 0);
  for (int i = 0; i < MAX_BOOT_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "b%d", i);
    if (!prefs.isKey(key)) {
      bootHistory[i].timestamp = 0;
      bootHistory[i].downtimeSec = 0;
      bootHistory[i].reasonCode = 0;
      continue;
    }
    size_t len = prefs.getBytes(key, &bootHistory[i], sizeof(BootLog));
    if (len == sizeof(BootLog)) {
      // modern struct
    } else if (len == 5 || len == 8) {
      // legacy struct without downtimeSec
      bootHistory[i].downtimeSec = 0;
    } else {
      bootHistory[i].timestamp = 0;
      bootHistory[i].downtimeSec = 0;
      bootHistory[i].reasonCode = 0;
    }
  }
  prefs.end();
}

// Record this boot: writes only boot_cnt, w_idx, and ONE slot key.
void recordBootEvent() {
  prefs.begin("esp_log", false);
  totalBootCount = prefs.getUInt("boot_cnt", 0) + 1;
  prefs.putUInt("boot_cnt", totalBootCount);

  uint32_t idx = prefs.getUInt("w_idx", 0) % MAX_BOOT_LOGS;

  BootLog entry;
  time_t now; time(&now);
  entry.timestamp = (now > 1600000000UL) ? (uint32_t)now : 0;
  entry.downtimeSec = 0;
  entry.reasonCode = (uint8_t)esp_reset_reason();

  char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)idx);
  prefs.putBytes(key, &entry, sizeof(entry));
  bootHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_BOOT_LOGS;
  prefs.putUInt("w_idx", newIdx);
  bootWriteIdx = newIdx;

  first_boot_epoch = prefs.getUInt("first_boot", 0);
  prefs.end();
}

// chronoIdx: 0 = most recent boot, 1 = one before that, etc.
bool getBootLogAt(int chronoIdx, BootLog &out) {
  if (chronoIdx < 0 || chronoIdx >= MAX_BOOT_LOGS) return false;
  int slot = ((int)bootWriteIdx - 1 - chronoIdx + 2 * MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
  out = bootHistory[slot];
  return true;
}

// --- Servo trigger NVS logging ---
void loadServoHistory() {
  prefs.begin("servo_log", true);
  servoLogCount  = prefs.getUInt("s_cnt", 0);
  servoWriteIdx  = prefs.getUInt("s_idx", 0) % MAX_SERVO_LOGS;
  for (int i = 0; i < MAX_SERVO_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "s%d", i);
    if (!prefs.isKey(key)) {
      servoHistory[i].timestamp = 0;
      servoHistory[i].fromCurl = 0;
      continue;
    }
    size_t len = prefs.getBytes(key, &servoHistory[i], sizeof(ServoLog));
    if (len != sizeof(ServoLog)) {
      servoHistory[i].timestamp = 0;
      servoHistory[i].fromCurl = 0;
    }
  }
  prefs.end();
}

void recordServoTrigger(bool curl) {
  time_t now; time(&now);
  ServoLog entry;
  entry.timestamp = (now > 1600000000UL) ? (uint32_t)now : 0;
  entry.fromCurl = curl ? 1 : 0;

  prefs.begin("servo_log", false);
  uint32_t idx = prefs.getUInt("s_idx", 0) % MAX_SERVO_LOGS;
  char key[6]; snprintf(key, sizeof(key), "s%u", (unsigned)idx);
  prefs.putBytes(key, &entry, sizeof(entry));
  servoHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_SERVO_LOGS;
  prefs.putUInt("s_idx", newIdx);
  servoWriteIdx = newIdx;

  servoLogCount = prefs.getUInt("s_cnt", 0) + 1;
  prefs.putUInt("s_cnt", servoLogCount);
  prefs.end();
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
  prefs.begin("ts_log", true);
  tailscaleLogCount = prefs.getUInt("ts_cnt", 0);
  tailscaleWriteIdx = prefs.getUInt("ts_idx", 0) % MAX_TAILSCALE_LOGS;
  lastTailscaleStopTime = prefs.getUInt("t_stop", 0);
  for (int i = 0; i < MAX_TAILSCALE_LOGS; i++) {
    char key[6]; snprintf(key, sizeof(key), "t%d", i);
    if (!prefs.isKey(key)) {
      tailscaleHistory[i].startTime = 0;
      tailscaleHistory[i].endTime = 0;
      tailscaleHistory[i].downtimeSec = 0;
      continue;
    }
    size_t len = prefs.getBytes(key, &tailscaleHistory[i], sizeof(TailscaleLog));
    if (len != sizeof(TailscaleLog)) {
      tailscaleHistory[i].startTime = 0;
      tailscaleHistory[i].endTime = 0;
      tailscaleHistory[i].downtimeSec = 0;
    }
  }
  prefs.end();

  // If previous boot ended with an open/active session, close it using last known heartbeat or RTC memory
  if (tailscaleLogCount > 0) {
    uint32_t activeSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
    if (tailscaleHistory[activeSlot].endTime == 0 && tailscaleHistory[activeSlot].startTime > 0) {
      prefs.begin("esp_log", true);
      uint32_t prevAlive = prefs.getUInt("last_alive", 0);
      prefs.end();

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

      prefs.begin("ts_log", false);
      char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)activeSlot);
      prefs.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
      prefs.putUInt("t_stop", closeTime);
      prefs.end();
    }
  }
  rtc_ts_was_active = false;
  rtc_ts_duration_s = 0;
}

void recordTailscaleStart() {
  time_t now; time(&now);
  uint32_t nowEpoch = (now > 1600000000UL) ? (uint32_t)now : 0;

  prefs.begin("ts_log", false);
  uint32_t lastStop = prefs.getUInt("t_stop", 0);
  uint32_t downtime = 0;
  if (nowEpoch > 0 && lastStop > 0 && nowEpoch > lastStop) {
    downtime = nowEpoch - lastStop;
  }

  TailscaleLog entry;
  entry.startTime = nowEpoch;
  entry.endTime = 0; // 0 = Active / Ongoing
  entry.downtimeSec = downtime;

  uint32_t idx = prefs.getUInt("ts_idx", 0) % MAX_TAILSCALE_LOGS;
  char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)idx);
  prefs.putBytes(key, &entry, sizeof(entry));
  tailscaleHistory[idx] = entry;

  uint32_t newIdx = (idx + 1) % MAX_TAILSCALE_LOGS;
  prefs.putUInt("ts_idx", newIdx);
  tailscaleWriteIdx = newIdx;

  tailscaleLogCount = prefs.getUInt("ts_cnt", 0) + 1;
  prefs.putUInt("ts_cnt", tailscaleLogCount);
  prefs.end();
}

void recordTailscaleStop() {
  time_t now; time(&now);
  uint32_t nowEpoch = (now > 1600000000UL) ? (uint32_t)now : 0;

  prefs.begin("ts_log", false);
  if (nowEpoch > 0) {
    prefs.putUInt("t_stop", nowEpoch);
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
      prefs.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
    }
  }
  prefs.end();
}

bool getTailscaleLogAt(int chronoIdx, TailscaleLog &out) {
  if (chronoIdx < 0 || chronoIdx >= MAX_TAILSCALE_LOGS || chronoIdx >= (int)tailscaleLogCount) return false;
  int slot = ((int)tailscaleWriteIdx - 1 - chronoIdx + 2 * MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
  out = tailscaleHistory[slot];
  return true;
}

void startTailscale(const char* reason = nullptr) {
  if (mlRunning || ml == nullptr) return;
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
  if (!mlRunning || ml == nullptr) return;
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
  if (!time_synced && WiFi.status() == WL_CONNECTED) {
    time_t now; time(&now);

    if (now > 1600000000UL) {
      time_synced = true;
      uint32_t thisBootEpoch = (uint32_t)now - (millis() / 1000);

      prefs.begin("esp_log", false);

      if (first_boot_epoch == 0) {
        first_boot_epoch = thisBootEpoch;
        prefs.putUInt("first_boot", first_boot_epoch);
      }

      if (!log_time_fixed) {
        BootLog latest;
        if (getBootLogAt(0, latest) && latest.timestamp < 1600000000UL) {
          latest.timestamp = thisBootEpoch;
          uint32_t slot = (bootWriteIdx - 1 + MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
          bootHistory[slot] = latest;
          char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)slot);
          prefs.putBytes(key, &latest, sizeof(latest));
        }
        log_time_fixed = true;
      }

      // If the latest Tailscale session started before NTP synced, fix its timestamp
      if (tailscaleLogCount > 0) {
        uint32_t activeSlot = ((int)tailscaleWriteIdx - 1 + MAX_TAILSCALE_LOGS) % MAX_TAILSCALE_LOGS;
        if (tailscaleHistory[activeSlot].startTime < 1600000000UL) {
          tailscaleHistory[activeSlot].startTime = thisBootEpoch;
          char key[6]; snprintf(key, sizeof(key), "t%u", (unsigned)activeSlot);
          prefs.begin("ts_log", false);
          prefs.putBytes(key, &tailscaleHistory[activeSlot], sizeof(TailscaleLog));
          prefs.end();
        }
      }

      // --- Downtime estimate: compare against the last heartbeat the
      // previous session managed to write before it died. ---
      if (!last_off_computed) {
        uint32_t prevAlive = prefs.getUInt("last_alive", 0);
        lastOffDuration = (prevAlive > 0 && thisBootEpoch > prevAlive) ? (thisBootEpoch - prevAlive) : 0;
        prefs.putUInt("last_off", lastOffDuration);

        BootLog latest;
        if (getBootLogAt(0, latest)) {
          latest.downtimeSec = (uint32_t)lastOffDuration;
          uint32_t slot = (bootWriteIdx - 1 + MAX_BOOT_LOGS) % MAX_BOOT_LOGS;
          bootHistory[slot] = latest;
          char key[6]; snprintf(key, sizeof(key), "b%u", (unsigned)slot);
          prefs.putBytes(key, &latest, sizeof(latest));
        }

        lastAliveEpoch = thisBootEpoch;
        prefs.putUInt("last_alive", lastAliveEpoch);
        lastHeartbeatMs = millis(); // restart the periodic heartbeat timer from now
        last_off_computed = true;
      }

      prefs.end();
    }
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
  prefs.begin("esp_log", false);
  prefs.putUInt("last_alive", lastAliveEpoch);
  prefs.end();
}

String formatTimestamp(uint32_t epoch) {
  if (epoch < 1600000000UL) return "Awaiting NTP Sync...";
  time_t t_epoch = epoch;
  struct tm timeinfo;
  localtime_r(&t_epoch, &timeinfo);
  char buf[64];
  strftime(buf, sizeof(buf), "%I:%M:%S %p %d-%b-%Y", &timeinfo);
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
    snprintf(buffer, maxLen, "%.2f s", (float)ms / 1000.0f);
  } else {
    snprintf(buffer, maxLen, "%lu ms", (unsigned long)ms);
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
  if (now - lastTempMs >= 2000) { cachedTemp = temperatureRead(); lastTempMs = now; }
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

String getConnectionType() {
  if (mlStandbyMode && !mlRunning) return "Subnet Active";
  if (ml == nullptr || !mlRunning || !microlink_is_connected(ml)) return "Not Connected";
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
    return reg.length() ? "Mixed (" + reg + ")" : "Mixed";
  }
  if (foundDirect) return "Direct (UDP)";
  if (foundRelayed) {
    return reg.length() ? "Relayed (" + reg + ")" : "Relayed";
  }
  return reg.length() ? "Relayed (" + reg + ")" : "Relayed";
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

  String url = isNumeric
    ? ("https://api.tailscale.com/api/v2/device/" + String(deviceIdent))
    : "https://api.tailscale.com/api/v2/tailnet/-/devices";

  String payload;
  payload.reserve(isNumeric ? 2048 : 8192);

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

  bool isConnected = false;

  if (err == ESP_OK && httpCode == 200 && payload.length() > 0) {
    cJSON *root = cJSON_Parse(payload.c_str());
    if (root != nullptr) {
      if (isNumeric) {
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

void handleRoot() {
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
  if (now - last_press_time < PRESS_COOLDOWN_MS) {
    server.sendHeader("Connection", "close");
    server.send(429, "text/plain; charset=utf-8", "Cooldown active.\n");
    return;
  }
  last_press_time = now;
  bool curlReq = isCurl();
  recordServoTrigger(curlReq);
  pendingPress = true;  // loop() will fire the servo; respond first so TCP
                        // doesn't sit open through the 700ms motion delay.

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
      "<div class='row'><span class='k'>Servo tap</span><span class='v ok'>Queued</span></div>"
      "<div class='row'><span class='k'>ESP uptime</span><span class='v mono'>%s</span></div>"
      "</div>"
      "<a class='back' href='/main'>&larr; Back to dashboard</a>",
      upBuf);
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("SwitchBot Trigger", "&#9889;", body, "", true));
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
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("SwitchBot Dashboard", "&#127920;", body.c_str(), "", true));
  }
}

void handleInfo() {
  uint32_t ramTotal = ESP.getHeapSize() / 1024;
  uint32_t ramFree = ESP.getFreeHeap() / 1024;
  uint32_t flashTotal = ESP.getFlashChipSize() / 1024;
  uint32_t flashUsed = ESP.getSketchSize() / 1024;
  uint32_t psramTotal = ESP.getPsramSize() / 1024;
  uint32_t psramFree = ESP.getFreePsram() / 1024;
  updateSensorCache(); // make sure there's a real reading before the first poll tick

  bool vpnConn = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  String vpnIp = "Not Valid";
  if (vpnConn) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) {
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%lu.%lu.%lu.%lu",
        (unsigned long)((rawIp >> 24) & 0xFF), (unsigned long)((rawIp >> 16) & 0xFF),
        (unsigned long)((rawIp >> 8) & 0xFF), (unsigned long)(rawIp & 0xFF));
      vpnIp = String(ipBuf);
    }
  }
  String connType = getConnectionType();
  const char* tailscaleStatus = vpnConn ? "CONNECTED" : (mlStandbyMode ? "STANDBY" : "Not Connected");
  const char* tailscalePillClass = vpnConn ? "on" : (mlStandbyMode ? "standby" : "off");
  const char* tailscalePillText = vpnConn ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected");

  if (isCurl()) {
    char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));
    const size_t n = 1500;
    std::unique_ptr<char[]> out(new char[n]);
    snprintf(out.get(), n,
      "==================================================\n"
      " [i] ESP32-S3 DEVICE INFO\n"
      "==================================================\n"
      " Uptime     : %-15s | Temp  : %.1f C\n"
      " CPU Clock  : %lu MHz          | Power : ~%.2f W\n"
      " Device     : ESP32-S3-WROOM-N16R8 DOIT\n"
      "--------------------------------------------------\n"
      " RAM Used   : %lu/%lu KB\n"
      " Flash Used : %lu/%lu KB\n"
      " PSRAM Used : %lu/%lu KB\n"
      "--------------------------------------------------\n"
      " Wi-Fi SSID : %s\n"
      " IP Address : %s (esp32.local)\n"
      " Firmware   : Core 1 | QIO 80MHz | SPIFFS 4MB\n"
      "--------------------------------------------------\n"
      " [ TAILSCALE ]\n"
      " Status     : %s\n"
      " Hostname   : %s\n"
      " IP Address : %s\n"
      " Connection : %s\n"
      "==================================================",
      upBuf, cachedTemp,
      ESP.getCpuFreqMHz(), cachedPower,
      (ramTotal - ramFree), ramTotal,
      flashUsed, flashTotal,
      (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
      tailscaleStatus,
      TAILSCALE_HOST,
      vpnIp.c_str(),
      connType.c_str()
    );
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out.get());
  } else {
    const size_t n = 3600;
    std::unique_ptr<char[]> body(new char[n]);
    snprintf(body.get(), n,
      "<h1>&#128187; Device Info</h1>"
      "<h3>Live</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Uptime</span><span class='v mono' id='up'>--</span></div>"
      "<div class='row'><span class='k'>CPU temp</span><span class='v mono' id='tmp'>--</span></div>"
      "<div class='row'><span class='k'>CPU clock</span><span class='v mono' id='clk'>--</span></div>"
      "<div class='row'><span class='k'>Est. power</span><span class='v mono' id='pwr'>--</span></div>"
      "<div class='row'><span class='k'>RAM used</span><span class='v mono' id='ram'>--</span></div>"
      "</div>"
      "<h3>Hardware</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Device</span><span class='v'>ESP32-S3-WROOM-N16R8</span></div>"
      "<div class='row'><span class='k'>Flash</span><span class='v mono'>%lu/%lu KB</span></div>"
      "<div class='row'><span class='k'>PSRAM</span><span class='v mono'>%lu/%lu KB</span></div>"
      "</div>"
      "<h3>Network</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>SSID</span><span class='v'>%s</span></div>"
      "<div class='row'><span class='k'>IP</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>Hostname</span><span class='v mono'>esp32.local</span></div>"
      "</div>"
      "<h3>Tailscale</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Status</span>"
      "<span class='v'><span class='pill %s' id='ts-pill'>%s</span></span></div>"
      "<div class='row'><span class='k'>Hostname</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>IP</span><span class='v mono' id='ts-ip'>%s</span></div>"
      "<div class='row'><span class='k'>Connection</span><span class='v mono' id='ts-conn'>%s</span></div>"
      "</div>"
      "<a class='back' href='/main'>&larr; Back to dashboard</a>",
      flashUsed, flashTotal, (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(),
      tailscalePillClass, tailscalePillText,
      TAILSCALE_HOST,
      vpnIp.c_str(),
      connType.c_str()
    );
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("Device Info", "&#128187;", body.get(), POLL_SCRIPT));
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
  String vpnIp = "-";
  if (vpnConn) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) {
      char ipBuf[16];
      snprintf(ipBuf, sizeof(ipBuf), "%lu.%lu.%lu.%lu",
        (unsigned long)((rawIp >> 24) & 0xFF), (unsigned long)((rawIp >> 16) & 0xFF),
        (unsigned long)((rawIp >> 8) & 0xFF), (unsigned long)(rawIp & 0xFF));
      vpnIp = String(ipBuf);
    }
  }
  String connType = getConnectionType();
  const char* tsSt = vpnConn ? "Connected" : (mlStandbyMode ? "Standby" : "Not Connected");
  const char* tsCls = vpnConn ? "on" : (mlStandbyMode ? "standby" : "off");

  char tsConnectBuf[32] = "";
  if (vpnConn && ts_connect_ms > 0) {
    formatMs(ts_connect_ms, tsConnectBuf, sizeof(tsConnectBuf));
  }

  char json[700];
  snprintf(json, sizeof(json),
    "{\"u\":\"%s\",\"uf\":\"%s\",\"t\":%.1f,\"ru\":%lu,\"rt\":%lu,\"c\":%lu,\"p\":%.2f,\"ota\":%d,\"sl\":%lu,\"st\":%lu,\"sc\":%lu,\"ts\":%lu,\"ts_st\":\"%s\",\"ts_cls\":\"%s\",\"ts_ip\":\"%s\",\"ts_conn\":\"%s\",\"ts_cm\":%lu,\"ts_cs\":\"%s\",\"cal\":%d,\"s_rest\":%d,\"s_press\":%d,\"s_dur\":%d}",
    upBuf, flashBuf, cachedTemp, (ramTotal - ramFree), ramTotal, ESP.getCpuFreqMHz(), cachedPower,
    otaEnabled ? 1 : 0, (unsigned long)servoTimestamp, (unsigned long)nowSec, (unsigned long)servoLogCount,
    (unsigned long)tsStart, tsSt, tsCls, vpnIp.c_str(), connType.c_str(),
    (unsigned long)ts_connect_ms, tsConnectBuf,
    isCalibrated ? 1 : 0, restAngle, pressAngle, pressDurationMs);
  
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

  char downtimeDuration[32] = "N/A (first boot)";
  if (last_off_computed && lastOffDuration > 0) formatDuration(lastOffDuration, downtimeDuration, sizeof(downtimeDuration));

  BootLog latest;
  bool hasLatest = getBootLogAt(0, latest) && (totalBootCount > 0);
  String lastResetCause = hasLatest ? String(getResetReasonString((esp_reset_reason_t)latest.reasonCode)) : "N/A";
  String lastResetTime = hasLatest ? formatTimestamp(latest.timestamp) : "N/A";

  ServoLog latestServo;
  bool hasServoLatest = getServoLogAt(0, latestServo) && (servoLogCount > 0) && latestServo.timestamp > 0;
  String lastServoAgo;
  if (hasServoLatest && time_synced) {
    time_t now; time(&now);
    if ((uint32_t)now > latestServo.timestamp) {
      char agoBuf[32];
      formatDuration((uint32_t)now - latestServo.timestamp, agoBuf, sizeof(agoBuf));
      lastServoAgo = String(agoBuf) + " ago";
    } else {
      lastServoAgo = "just now";
    }
  } else if (hasServoLatest) {
    lastServoAgo = "Awaiting NTP Sync...";
  }

  TailscaleLog latestTs;
  bool hasTsLatest = getTailscaleLogAt(0, latestTs) && (tailscaleLogCount > 0);
  const char* tailscaleStatus = mlRunning ? "CONNECTED" : (mlStandbyMode ? "STANDBY" : "Not Connected");

  int limitLogs = isCurl() ? 6 : MAX_BOOT_LOGS;

  char bootTimeBuf[32]; formatMs(boot_time_ms, bootTimeBuf, sizeof(bootTimeBuf));
  char wifiConnectBuf[32]; formatMs(wifi_connect_ms, wifiConnectBuf, sizeof(wifiConnectBuf));

  bool vpnConn = (ml != nullptr) && mlRunning && microlink_is_connected(ml);
  char tsConnectBuf[32] = "";
  if (vpnConn && ts_connect_ms > 0) {
    formatMs(ts_connect_ms, tsConnectBuf, sizeof(tsConnectBuf));
  }

  if (isCurl()) {
    const size_t n = 4800;
    std::unique_ptr<char[]> out(new char[n]);
    int off = snprintf(out.get(), n,
      "==================================================\n"
      " [!] ESP32-S3 LOGS & DEBUG\n"
      "==================================================\n"
      " Uptime Since Boot    : %s\n"
      " Uptime Since Flash   : %s\n"
      " Last Downtime        : %s\n"
      " Boot Time            : %s\n"
      " Wi-Fi Connect Time   : %s\n",
      bootDuration, flashDuration, downtimeDuration, bootTimeBuf, wifiConnectBuf
    );

    if (vpnConn && tsConnectBuf[0]) {
      off += snprintf(out.get() + off, n - off,
        " Tailscale Connect    : %s\n", tsConnectBuf);
    }

    off += snprintf(out.get() + off, n - off,
      " Total Boot Count     : %lu\n"
      " OTA Status           : %s\n",
      totalBootCount,
      otaEnabled ? "ENABLED" : "disabled"
    );

    if (hasServoLatest) {
      off += snprintf(out.get() + off, n - off,
        "--------------------------------------------------\n"
        " [ SERVO ACTIVITY ]\n"
        " Total Triggers       : %lu\n"
        " Last Tap             : %s (%s)\n",
        (unsigned long)servoLogCount, lastServoAgo.c_str(), latestServo.fromCurl ? "curl" : "web");

      bool hasServoHistory = false;
      for (int i = 1; i < limitLogs && i < MAX_SERVO_LOGS; i++) {
        ServoLog sentry;
        if (!getServoLogAt(i, sentry) || sentry.timestamp == 0) continue;
        if (!hasServoHistory) {
          off += snprintf(out.get() + off, n - off, " [ PREVIOUS SERVO HISTORY ]\n");
          hasServoHistory = true;
        }
        off += snprintf(out.get() + off, n - off, " [%s] via %s\n",
          formatTimestamp(sentry.timestamp).c_str(), sentry.fromCurl ? "curl" : "web");
      }
    }


    off += snprintf(out.get() + off, n - off,
      "--------------------------------------------------\n"
      " [ TAILSCALE ACTIVITY ]\n"
      " Status          : %s\n"
      " Total Sessions  : %lu\n",
      tailscaleStatus, (unsigned long)tailscaleLogCount);

    if (hasTsLatest) {
      char tsDurBuf[32];
      time_t nowTs; time(&nowTs);
      uint32_t tsDur = 0;
      if (latestTs.endTime == 0) {
        tsDur = (nowTs > latestTs.startTime) ? (uint32_t)(nowTs - latestTs.startTime) : 0;
      } else {
        tsDur = (latestTs.endTime > latestTs.startTime) ? (latestTs.endTime - latestTs.startTime) : 0;
      }
      formatDuration(tsDur, tsDurBuf, sizeof(tsDurBuf));

      char tsDtBuf[64] = "";
      if (latestTs.downtimeSec > 0) {
        char b[32]; formatDuration(latestTs.downtimeSec, b, sizeof(b));
        snprintf(tsDtBuf, sizeof(tsDtBuf), " (downtime: %s)", b);
      }

      if (latestTs.endTime == 0) {
        off += snprintf(out.get() + off, n - off,
          " Active Session  : %s%s\n", tsDurBuf, tsDtBuf);
      } else {
        off += snprintf(out.get() + off, n - off,
          " Last Session    : %s%s\n"
          " Last Connected  : %s\n",
          tsDurBuf, tsDtBuf, formatTimestamp(latestTs.startTime).c_str());
      }

      bool hasTsHistory = false;
      for (int i = 1; i < limitLogs && i < (int)tailscaleLogCount && i < MAX_TAILSCALE_LOGS; i++) {
        TailscaleLog tentry;
        if (!getTailscaleLogAt(i, tentry) || tentry.startTime == 0) continue;
        if (!hasTsHistory) {
          off += snprintf(out.get() + off, n - off, " [ PREVIOUS TAILSCALE HISTORY ]\n");
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
        char itemDt[48] = "";
        if (tentry.downtimeSec > 0) {
          char b[32]; formatDuration(tentry.downtimeSec, b, sizeof(b));
          snprintf(itemDt, sizeof(itemDt), " (downtime: %s)", b);
        }

        off += snprintf(out.get() + off, n - off,
          " [%s]  Duration: %-6s%s\n",
          formatTimestamp(tentry.startTime).c_str(),
          itemDur,
          itemDt);
      }
    }

    off += snprintf(out.get() + off, n - off,
      "--------------------------------------------------\n"
      " [ RESET FORENSICS ]\n"
      " Last Reset Cause     : %s\n"
      " Last Reset Time      : %s\n",
      lastResetCause.c_str(),
      lastResetTime.c_str()
    );

    bool hasBootHistory = false;
    for (int i = 1; i < limitLogs; i++) {
      BootLog entry;
      if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
      if (!hasBootHistory) {
        off += snprintf(out.get() + off, n - off, "\n [ PREVIOUS BOOT HISTORY ]\n");
        hasBootHistory = true;
      }
      char dtBuf[32];
      formatDuration(entry.downtimeSec, dtBuf, sizeof(dtBuf));
      char dtStr[48] = "";
      if (entry.downtimeSec > 0) snprintf(dtStr, sizeof(dtStr), " (down: %s)", dtBuf);
      off += snprintf(out.get() + off, n - off, " [%s]  %-24s%s\n",
        formatTimestamp(entry.timestamp).c_str(),
        getResetReasonString((esp_reset_reason_t)entry.reasonCode),
        dtStr);
    }
    off += snprintf(out.get() + off, n - off, "==================================================\n");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out.get());
  } else {
    // Build servo trigger history HTML (start at 1, latest shown separately in summary card)
    String servoHtml;
    servoHtml.reserve(1500);
    for (int i = 1; i < MAX_SERVO_LOGS; i++) {
      ServoLog sentry;
      if (!getServoLogAt(i, sentry) || sentry.timestamp == 0) continue;
      char sline[320];
      snprintf(sline, sizeof(sline),
        "<div class='log-item'>"
        "<div class='log-meta'>"
        "<span class='log-title'>Servo Actuation</span>"
        "<span class='log-sub'>%s</span>"
        "</div>"
        "<span class='log-badge'>%s</span>"
        "</div>",
        formatTimestamp(sentry.timestamp).c_str(),
        sentry.fromCurl ? "CURL" : "WEB");
      servoHtml += sline;
    }

    // Build Tailscale connection history HTML
    String tsHtml;
    tsHtml.reserve(2500);
    time_t nowTs; time(&nowTs);
    for (int i = 0; i < MAX_TAILSCALE_LOGS && i < (int)tailscaleLogCount; i++) {
      TailscaleLog tentry;
      if (!getTailscaleLogAt(i, tentry) || tentry.startTime == 0) continue;

      char durBuf[32];
      if (tentry.endTime == 0) {
        uint32_t d = (nowTs > tentry.startTime) ? (uint32_t)(nowTs - tentry.startTime) : 0;
        formatDuration(d, durBuf, sizeof(durBuf));
      } else {
        uint32_t d = (tentry.endTime > tentry.startTime) ? (tentry.endTime - tentry.startTime) : 0;
        formatDuration(d, durBuf, sizeof(durBuf));
      }

      char dtHtml[64] = "";
      if (tentry.downtimeSec > 0) {
        char dtBuf[32]; formatDuration(tentry.downtimeSec, dtBuf, sizeof(dtBuf));
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

      String rangeStr = formatSessionRange(tentry.startTime, tentry.endTime);

      char line[512];
      if (tentry.endTime == 0) {
        snprintf(line, sizeof(line),
          "<div class='log-item%s'>"
          "<div class='log-meta'>"
          "<span class='log-title'>Active Session</span>"
          "<span class='log-sub'>%s</span>"
          "<span class='log-sub'>Duration: <span id='ts-dur-val'>%s</span>%s</span>"
          "</div>"
          "<span class='log-badge %s'>%s</span>"
          "</div>",
          (i == 0) ? " latest-entry" : "",
          rangeStr.c_str(),
          durBuf,
          dtHtml,
          badgeClass,
          badgeText);
      } else {
        snprintf(line, sizeof(line),
          "<div class='log-item%s'>"
          "<div class='log-meta'>"
          "<span class='log-title'>Tailscale Session</span>"
          "<span class='log-sub'>%s</span>"
          "<span class='log-sub'>Duration: <b>%s</b>%s</span>"
          "</div>"
          "<span class='log-badge %s'>%s</span>"
          "</div>",
          (i == 0) ? " latest-entry" : "",
          rangeStr.c_str(),
          durBuf,
          dtHtml,
          badgeClass,
          badgeText);
      }
      tsHtml += line;
    }

    // Build boot history HTML
    String historyHtml;
    historyHtml.reserve(2500);
    for (int i = 1; i < limitLogs; i++) {
      BootLog entry;
      if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
      char dtHtml[64] = "";
      if (entry.downtimeSec > 0) {
        char dtBuf[32]; formatDuration(entry.downtimeSec, dtBuf, sizeof(dtBuf));
        snprintf(dtHtml, sizeof(dtHtml), " &bull; Approx. downtime: %s", dtBuf);
      }
      char line[320];
      const char* cls = getResetReasonClass((esp_reset_reason_t)entry.reasonCode);
      snprintf(line, sizeof(line),
        "<div class='log-item'>"
        "<div class='log-meta'>"
        "<span class='log-title %s'>%s</span>"
        "<span class='log-sub'>%s%s</span>"
        "</div>"
        "</div>",
        cls,
        getResetReasonString((esp_reset_reason_t)entry.reasonCode),
        formatTimestamp(entry.timestamp).c_str(),
        dtHtml);
      historyHtml += line;
    }

    const size_t n = 5120 + historyHtml.length() + servoHtml.length() + tsHtml.length();
    std::unique_ptr<char[]> body(new char[n]);
    int off = snprintf(body.get(), n,
      "<h1>&#128295; Logs &amp; Debug</h1>"
      "<h3>General</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Uptime since boot</span><span class='v mono' id='up'>%s</span></div>"
      "<div class='row'><span class='k'>Uptime since flash</span><span class='v mono' id='upf'>%s</span></div>"
      "<div class='row'><span class='k'>Last approx downtime</span><span class='v mono'>%s</span></div>"
      "</div>"
      "<h3>Boot Stats</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Boot time</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>Wi-Fi connect</span><span class='v mono'>%s</span></div>"
      "<div class='row' id='ts-connect-row'%s><span class='k'>Tailscale connect</span><span class='v mono' id='ts-conn-time'>%s</span></div>"
      "<div class='row'><span class='k'>Total boots</span><span class='v mono'>%lu</span></div>"
      "<div class='row'><span class='k'>Last reset</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>Last reset time</span><span class='v mono'>%s</span></div>"
      "</div>",
      bootDuration, flashDuration, downtimeDuration,
      bootTimeBuf, wifiConnectBuf,
      (vpnConn && tsConnectBuf[0]) ? "" : " style='display:none;'",
      (vpnConn && tsConnectBuf[0]) ? tsConnectBuf : "--",
      totalBootCount,
      lastResetCause.c_str(), lastResetTime.c_str()
    );

    // Servo summary card (placed right after Boot Stats)
    if (hasServoLatest) {
      off += snprintf(body.get() + off, n - off,
        "<h3>Servo</h3>"
        "<div class='card'>"
        "<div class='row'><span class='k'>Last tap</span><span class='v mono' id='servo-ago'>%s</span></div>"
        "<div class='row'><span class='k'>Source</span><span class='v mono'>%s</span></div>"
        "<div class='row'><span class='k'>Total triggers</span><span class='v mono'>%lu</span></div>"
        "</div>",
        lastServoAgo.c_str(), latestServo.fromCurl ? "curl" : "web", servoLogCount);
    }

    // Servo Trigger History (placed ON TOP OF reset logs!)
    if (servoHtml.length() > 0) {
      off += snprintf(body.get() + off, n - off,
        "<h3>Servo Trigger History</h3><div class='card'>%s</div>", servoHtml.c_str());
    }

    // Tailscale Connection History
    if (tsHtml.length() > 0) {
      off += snprintf(body.get() + off, n - off,
        "<h3>Tailscale Connection History</h3><div class='card'>%s</div>", tsHtml.c_str());
    } else {
      off += snprintf(body.get() + off, n - off,
        "<h3>Tailscale Connection History</h3><div class='card'><div class='row'><span class='k'>Status</span><span class='v mono'>Subnet Active</span></div></div>");
    }

    // Reset / Previous Boot History
    if (historyHtml.length() > 0) {
      off += snprintf(body.get() + off, n - off,
        "<h3>Previous Boot History</h3><div class='card'>%s</div>", historyHtml.c_str());
    }

    // OTA Updates (placed at the very bottom, right before divider and reboot/clear logs buttons)
    off += snprintf(body.get() + off, n - off,
      "<h3>OTA Updates</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Status</span>"
      "<span class='v'><span class='pill %s'>%s</span></span></div></div>",
      otaEnabled ? "on" : "off", otaEnabled ? "Enabled" : "Disabled"
    );

    if (otaEnabled) {
      off += snprintf(body.get() + off, n - off,
        "<div class='actions'>"
        "<form action='/ota/disable' method='POST'><button type='submit'>Disable OTA</button></form>"
        "</div>");
    } else {
      off += snprintf(body.get() + off, n - off,
        "<div class='actions'>"
        "<form action='/ota/enable' method='POST'><input type='hidden' name='key' value='%s'>"
        "<button class='warn' type='submit'>Enable OTA</button></form>"
        "</div>", OTA_KEY);
    }

    // Divider between OTA/logs and destructive actions
    off += snprintf(body.get() + off, n - off, "<hr class='divider'>");

    if (isCalibrated) {
      off += snprintf(body.get() + off, n - off,
        "<div class='actions' style='margin-bottom:12px;'>"
        "<a href='/calibrate' style='width:100%%;text-decoration:none;'><button type='button'>&#127919; Recalibrate Servo</button></a>"
        "</div>");
    }

    snprintf(body.get() + off, n - off,
      "<div class='actions'>"
      "<form action='/reboot' method='POST' onsubmit='return confirm(\"Reboot the ESP32 now?\");'>"
      "<button class='danger' type='submit'>&#128260; Reboot</button></form>"
      "<form action='/clear-logs' method='POST' onsubmit='return confirm(\"Clear all logs and flash timers?\");'>"
      "<button class='warn' type='submit'>&#128465; Clear Logs</button></form>"
      "</div>"
      "<a class='back' href='/main'>&larr; Back to dashboard</a>"
    );

    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("Logs & Debug", "&#128295;", body.get(), POLL_SCRIPT));
  }
}

void handleOtaEnable() {
  if (!server.hasArg("key") || server.arg("key") != OTA_KEY) {
    server.sendHeader("Connection", "close");
    server.send(403, "text/plain; charset=utf-8", "Forbidden: bad or missing key.\n");
    return;
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
  prefs.begin("esp_log", false);
  prefs.clear();
  prefs.end();

  memset(bootHistory, 0, sizeof(bootHistory));
  totalBootCount = 0;
  bootWriteIdx = 0;
  log_time_fixed = false;

  // Clear servo trigger logs
  prefs.begin("servo_log", false);
  prefs.clear();
  prefs.end();
  memset(servoHistory, 0, sizeof(servoHistory));
  servoLogCount = 0;
  servoWriteIdx = 0;

  // Clear Tailscale session logs
  prefs.begin("ts_log", false);
  prefs.clear();
  prefs.end();
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
    prefs.begin("esp_log", false);
    prefs.putUInt("first_boot", first_boot_epoch);
    prefs.putUInt("last_alive", lastAliveEpoch);
    prefs.end();
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

void setup() {
  setCpuFrequencyMhz(80);
  loadCalibration();
  initServo();
  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle); delay(300); myservo.detach();

  loadBootHistory();
  loadServoHistory();
  loadTailscaleHistory();
  recordBootEvent();

  uint32_t wifi_start = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false); // Modem sleep disabled so incoming HTTP/curl connections respond immediately
  WiFi.setTxPower(WIFI_POWER_15dBm);
  WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS);
  ip_addr_t router_dns;
  IP_ADDR4(&router_dns, 192, 168, 1, 1);
  dns_setserver(2, &router_dns);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) delay(100);
  wifi_connect_ms = millis() - wifi_start;

  // Give 802.11 association, block ack, and router forwarding time to settle
  delay(1500);

  microlink_config_t ml_conf;
  memset(&ml_conf, 0, sizeof(ml_conf));
  ml_conf.auth_key = TAILSCALE_KEY;
  ml_conf.device_name = TAILSCALE_HOST;
  // Route is advertised by primary subnet router (moto-g32 -> 192.168.1.0/24); keep nullptr to prevent /32 hijacking
  ml_conf.advertise_routes = nullptr;
  ml_conf.enable_derp = true;
  ml_conf.enable_stun = true;
  ml_conf.enable_disco = true;
  ml_conf.max_peers = 8;
  ml_conf.wifi_tx_power_dbm = 15;

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

  configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

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

  const char* headerkeys[] = {"User-Agent", "Host"};
  server.collectHeaders(headerkeys, 2);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/main", HTTP_GET, handleMain);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/debug", HTTP_GET, handleDebug);
  server.on("/reboot", HTTP_ANY, handleReboot);
  server.on("/clear-logs", HTTP_ANY, handleClearLogs);
  server.on("/api/live", HTTP_GET, handleApiLive);
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
        vTaskDelay(pdMS_TO_TICKS(10));
      }
    },
    "http_srv",   /* task name   */
    8192,         /* stack bytes */
    nullptr,      /* arg         */
    4,            /* priority    */
    nullptr,      /* handle out  */
    0             /* Core 0 — microlink net_io and derp_tx also run here,
                     keeping WebServer off Core 1 where wg_mgr lives */
  );

  boot_time_ms = millis();
}

void loop() {
  if (otaEnabled) {
    ArduinoOTA.handle();
    if (millis() - otaEnabledAt > OTA_AUTO_TIMEOUT_MS) stopOTA();
  }

  // Fire the servo after the HTTP response has already been sent.
  // This is the async half of the handleRoot() optimisation: the client gets
  // its 200 immediately, then the servo moves without blocking anything.
  if (pendingPress) {
    pendingPress = false;
    triggerPress();
  }

  if (!time_synced) syncTimeIfNeeded();
  heartbeatIfNeeded();
  checkSubnetAndFailoverIfNeeded();

  if (mlRunning && mlStartedAtMs > 0) {
    rtc_ts_duration_s = (millis() - mlStartedAtMs) / 1000;
    if (!ts_connected_latched && (ml != nullptr) && microlink_is_connected(ml)) {
      ts_connect_ms = millis() - mlStartedAtMs;
      ts_connected_latched = true;
    }
  }

  vTaskDelay(pdMS_TO_TICKS(20));
}