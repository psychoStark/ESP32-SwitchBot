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
#include "secrets.h"
extern "C" {
  #include "microlink.h"
}

// --- Configuration ---
const char* ssid     = WIFI_SSID;
const char* password = WIFI_PASSWORD;

IPAddress local_IP(192, 168, 1, 50);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 255, 0);
IPAddress primaryDNS(192, 168, 1, 1);

const int servoPin   = 1;
const int restAngle  = 180;
const int pressAngle = 156;

// OTA gate key - shown embedded in the /debug page's "Enable OTA" form and in
// the curl menu. This is obscurity, not real auth - the device already
// assumes a trusted local network (same tier as the WiFi password / Tailscale
// auth key). Defined in secrets.h as OTA_KEY - every reference to OTA_KEY
// below picks it up from there automatically.
const unsigned long OTA_AUTO_TIMEOUT_MS = 10UL * 60UL * 1000UL; // auto-disable after 10 min

// How often we persist a "last known alive" heartbeat to NVS, used to
// estimate downtime between sessions. Shorter = tighter downtime estimate,
// more (still very cheap) flash writes.
const unsigned long HEARTBEAT_INTERVAL_MS = 5UL * 60UL * 1000UL; // 5 minutes

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
  uint8_t  reasonCode;  // esp_reset_reason_t value (0-10, fits a byte)
};
BootLog bootHistory[MAX_BOOT_LOGS]; // RAM cache, indexed by physical slot
uint32_t bootWriteIdx = 0;          // next physical slot to write
uint32_t totalBootCount = 0;

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
  delay(400);
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
    size_t len = prefs.getBytes(key, &bootHistory[i], sizeof(BootLog));
    if (len != sizeof(BootLog)) {
      bootHistory[i].timestamp = 0;
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

      // --- Downtime estimate: compare against the last heartbeat the
      // previous session managed to write before it died. ---
      if (!last_off_computed) {
        uint32_t prevAlive = prefs.getUInt("last_alive", 0);
        lastOffDuration = (prevAlive > 0 && thisBootEpoch > prevAlive) ? (thisBootEpoch - prevAlive) : 0;
        prefs.putUInt("last_off", lastOffDuration);

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
  connected = (ml != nullptr) && microlink_is_connected(ml);
  ip = "N/A";
  magicDns = "N/A";
  if (connected) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) ip = IPAddress(rawIp).toString();

    uint32_t resolved = microlink_resolve(ml, TAILSCALE_HOST);
    if (resolved != 0 && rawIp != 0 && resolved == rawIp) magicDns = "OK";
    else if (resolved != 0) magicDns = "Mismatch";
    else magicDns = "Not resolving";
  }
}

// --- Shared UI: CSS + page wrapper ---

static const char COMMON_CSS[] =
":root{--bg:#000;--surface:#0a0f1a;--border:#1c2b45;--blue:#3b82f6;--blue-glow:#60a5fa;"
"--cyan:#22d3ee;--text:#cdd9f0;--text-dim:#5b6b8c;--danger:#f87171;--warn:#fbbf24;--ok:#34d399;}"
"*{box-sizing:border-box;}"
"body{background:var(--bg);color:var(--text);margin:0;min-height:100vh;"
"font-family:-apple-system,'Segoe UI',Roboto,sans-serif;display:flex;justify-content:center;}"
".wrap{width:100%;max-width:480px;padding:28px 20px 40px;}"
"h1,h2,h3{font-weight:800;letter-spacing:.5px;color:#fff;margin:0 0 18px;}"
"h1{font-size:22px;text-transform:uppercase;text-align:center;}"
"h2{font-size:19px;}"
"h3{font-size:13px;color:var(--blue-glow);text-transform:uppercase;letter-spacing:1.5px;"
"border-bottom:1px solid var(--border);padding-bottom:6px;margin-top:22px;}"
".nav a,.btn{display:block;background:var(--surface);border:1px solid var(--border);"
"color:var(--blue-glow);font-weight:800;font-size:15px;letter-spacing:1px;text-transform:uppercase;"
"text-align:center;text-decoration:none;padding:16px;margin:10px 0;border-radius:12px;"
"transition:box-shadow .15s,border-color .15s;}"
".nav a.primary{border-color:var(--cyan);color:var(--cyan);box-shadow:0 0 14px rgba(34,211,238,.35);}"
".mono{font-family:'SFMono-Regular',Consolas,'Liberation Mono',Menlo,monospace;}"
".card{background:var(--surface);border:1px solid var(--border);border-radius:12px;"
"padding:6px 18px;margin-bottom:14px;}"
".row{display:flex;justify-content:space-between;padding:10px 0;"
"font-family:'SFMono-Regular',Consolas,monospace;font-size:14px;border-bottom:1px solid rgba(255,255,255,.04);}"
".row:last-child{border-bottom:none;}"
".row .k{color:var(--text-dim);}"
".row .v{color:var(--text);font-weight:600;}"
".v.ok{color:var(--ok);}.v.warn{color:var(--warn);}.v.danger{color:var(--danger);}"
".actions{display:flex;gap:10px;margin-top:8px;}"
".actions form{flex:1;margin:0;}"
"button{width:100%;background:transparent;border:1px solid var(--border);color:var(--text);"
"font-weight:800;letter-spacing:.5px;text-transform:uppercase;padding:12px;border-radius:10px;"
"cursor:pointer;font-size:13px;}"
"button.danger{border-color:var(--danger);color:var(--danger);}"
"button.warn{border-color:var(--warn);color:var(--warn);}"
".back{display:block;text-align:center;margin-top:22px;color:var(--text-dim);"
"text-decoration:none;font-size:13px;letter-spacing:.5px;}"
".pill{display:inline-block;padding:3px 10px;border-radius:20px;font-size:11px;font-weight:700;"
"letter-spacing:.5px;text-transform:uppercase;}"
".pill.on{background:rgba(52,211,153,.12);color:var(--ok);border:1px solid rgba(52,211,153,.4);}"
".pill.off{background:rgba(91,107,140,.12);color:var(--text-dim);border:1px solid var(--border);}";

// Visibility-gated polling: replaces "poll forever every 1s" with
// "poll every 1s only while this tab is actually visible". Zero server
// changes required - this is purely a client-side fix.
static const char POLL_SCRIPT[] =
"<script>"
"let __poll;"
"function __pollTick(){fetch('/api/live').then(function(r){return r.json();}).then(function(d){"
"var el;"
"if(el=document.getElementById('up'))el.textContent=d.u;"
"if(el=document.getElementById('upf'))el.textContent=d.uf;"
"if(el=document.getElementById('tmp'))el.innerHTML=d.t+' &deg;C';"
"if(el=document.getElementById('pwr'))el.textContent='~'+d.p+' W';"
"if(el=document.getElementById('ram'))el.textContent=d.ru+'/'+d.rt+' KB';"
"if(el=document.getElementById('clk'))el.textContent=d.c+' MHz';"
"});}"
"function __pollStart(){if(__poll)return;__pollTick();__poll=setInterval(__pollTick,1000);}"
"function __pollStop(){if(__poll){clearInterval(__poll);__poll=null;}}"
"document.addEventListener('visibilitychange',function(){if(document.hidden)__pollStop();else __pollStart();});"
"if(!document.hidden)__pollStart();"
"</script>";

String wrapPage(const char* title, const char* icon, const char* bodyHtml, const char* extraScript = "") {
  String out;
  out.reserve(strlen(bodyHtml) + sizeof(COMMON_CSS) + strlen(extraScript) + 400);
  out += "<!DOCTYPE html><html><head><meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'><title>";
  out += title;
  out += "</title><link rel='icon' href='data:image/svg+xml,%3Csvg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 100 100\"%3E%3Ctext y=\".9em\" font-size=\"90\"%3E";
  out += icon;
  out += "%3C/text%3E%3C/svg%3E'><style>";
  out += COMMON_CSS;
  out += "</style>";
  out += extraScript;
  out += "</head><body><div class='wrap'>";
  out += bodyHtml;
  out += "</div></body></html>";
  return out;
}

// --- Routes ---

void handleRoot() {
  uint32_t now = millis();
  if (now - last_press_time < PRESS_COOLDOWN_MS) {
    server.sendHeader("Connection", "close");
    server.send(429, "text/plain; charset=utf-8", "Cooldown active.\n");
    return;
  }
  last_press_time = now;
  pendingPress = true;  // loop() will fire the servo; respond first so TCP
                        // doesn't sit open through the 700ms motion delay.

  char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));

  if (isCurl()) {
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
    server.send(200, "text/html; charset=utf-8", wrapPage("SwitchBot Trigger", "&#9889;", body));
  }
}

void handleMain() {
  if (isCurl()) {
    String host = server.header("Host");
    if (host.length() == 0) host = "esp32.local";

    const size_t n = 3600;
    std::unique_ptr<char[]> buf(new char[n]);
    snprintf(buf.get(), n,
      "#!/bin/bash\n"
      "HOST=\"%s\"\n"
      "OTAKEY=\"%s\"\n"
      "while true; do\n"
      "  clear\n"
      "  echo -e '\\n=== SwitchBot Dashboard ==='\n"
      "  echo ' [1] Trigger Power Button'\n"
      "  echo ' [2] Device Info'\n"
      "  echo ' [3] Crash Logs & Debug'\n"
      "  echo ' [4] Reboot ESP32'\n"
      "  echo ' [5] Clear Logs'\n"
      "  echo ' [6] Enable OTA (10 min)'\n"
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
      "      echo -ne \"\\033[H$OUT\\n [B] Back to Menu | [C] Clear Logs | [R] Reboot | [O] Enable OTA | [X] Exit\\033[K\\n\"\n"
      "      key=\"\"\n"
      "      read -t 1 -n 1 -s key </dev/tty\n"
      "      if [[ \"$key\" == \"b\" || \"$key\" == \"B\" ]]; then break; fi\n"
      "      if [[ \"$key\" == \"x\" || \"$key\" == \"X\" ]]; then echo \"\"; exit 0; fi\n"
      "      if [[ \"$key\" == \"r\" || \"$key\" == \"R\" ]]; then curl -X POST -s http://$HOST/reboot; exit 0; fi\n"
      "      if [[ \"$key\" == \"c\" || \"$key\" == \"C\" ]]; then curl -X POST -s http://$HOST/clear-logs; sleep 1.5; break; fi\n"
      "      if [[ \"$key\" == \"o\" || \"$key\" == \"O\" ]]; then curl -X POST -s http://$HOST/ota/enable -d \"key=$OTAKEY\"; sleep 1; fi\n"
      "    done\n"
      "  elif [[ \"$opt\" == \"4\" ]]; then\n"
      "    curl -X POST -s http://$HOST/reboot\n"
      "    exit 0\n"
      "  elif [[ \"$opt\" == \"5\" ]]; then\n"
      "    curl -X POST -s http://$HOST/clear-logs\n"
      "    sleep 1.5\n"
      "  elif [[ \"$opt\" == \"6\" ]]; then\n"
      "    curl -X POST -s http://$HOST/ota/enable -d \"key=$OTAKEY\"\n"
      "    echo 'OTA enabled for 10 minutes.'\n"
      "    sleep 1.5\n"
      "  fi\n"
      "done\n",
      host.c_str(), OTA_KEY
    );
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", buf.get());
  } else {
    const char* body =
      "<h1>SwitchBot</h1>"
      "<div class='nav'>"
      "<a class='primary' href='/'>&#9889; Trigger Power Button</a>"
      "<a href='/info'>&#128187; Device Info</a>"
      "<a href='/debug'>&#128295; Crash Logs &amp; Debug</a>"
      "</div>";
    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("SwitchBot Dashboard", "&#127920;", body));
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

  if (isCurl()) {
    char upBuf[32]; formatDuration(esp_timer_get_time() / 1000000ULL, upBuf, sizeof(upBuf));
    const size_t n = 1024;
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
      "==================================================",
      upBuf, cachedTemp,
      ESP.getCpuFreqMHz(), cachedPower,
      (ramTotal - ramFree), ramTotal,
      flashUsed, flashTotal,
      (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str()
    );
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out.get());
  } else {
    const size_t n = 1600;
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
      "<a class='back' href='/main'>&larr; Back to dashboard</a>",
      flashUsed, flashTotal, (psramTotal > 0 ? psramTotal - psramFree : 0), psramTotal,
      WiFi.SSID().c_str(), WiFi.localIP().toString().c_str()
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

  char json[320];
  snprintf(json, sizeof(json), "{\"u\":\"%s\",\"uf\":\"%s\",\"t\":%.1f,\"ru\":%lu,\"rt\":%lu,\"c\":%lu,\"p\":%.2f}",
    upBuf, flashBuf, cachedTemp, (ramTotal - ramFree), ramTotal, ESP.getCpuFreqMHz(), cachedPower);
  
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

  bool vpnConnected = (ml != nullptr) && microlink_is_connected(ml);
  String vpnIp = "N/A";
  if (vpnConnected) {
    uint32_t rawIp = microlink_get_vpn_ip(ml);
    if (rawIp != 0) vpnIp = IPAddress(rawIp).toString();
  }

  int limitLogs = isCurl() ? 6 : MAX_BOOT_LOGS;

  if (isCurl()) {
    const size_t n = 2600;
    std::unique_ptr<char[]> out(new char[n]);
    int off = snprintf(out.get(), n,
      "==================================================\n"
      " [!] ESP32-S3 CRASH LOGS & DEBUG\n"
      "==================================================\n"
      " Uptime Since Boot    : %s\n"
      " Uptime Since Flash   : %s\n"
      " Approx. Downtime     : %s\n"
      " Boot Time            : %lu ms\n"
      " Wi-Fi Connect Time   : %lu ms\n"
      " Total Boot Count     : %lu\n"
      " OTA Status           : %s\n"
      " Tailscale (VPN)      : %s%s%s\n",
      bootDuration, flashDuration, downtimeDuration, boot_time_ms, wifi_connect_ms, totalBootCount,
      otaEnabled ? "ENABLED" : "disabled",
      vpnConnected ? "CONNECTED (" : "Not connected",
      vpnConnected ? vpnIp.c_str() : "",
      vpnConnected ? ")" : ""
    );
    if (hasLatest) {
      off += snprintf(out.get() + off, n - off,
        " Last Reset Cause     : %s\n"
        " Last Reset Time      : %s\n\n",
        lastResetCause.c_str(), lastResetTime.c_str());
    } else {
      off += snprintf(out.get() + off, n - off, " Log Status         : Clean (0 Resets)\n\n");
    }
    bool hasHistory = false;
    for (int i = 1; i < limitLogs; i++) {
      BootLog entry;
      if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
      if (!hasHistory) { off += snprintf(out.get() + off, n - off, " [ PREVIOUS BOOT HISTORY ]\n"); hasHistory = true; }
      off += snprintf(out.get() + off, n - off, " [%s] %s\n",
        formatTimestamp(entry.timestamp).c_str(), getResetReasonString((esp_reset_reason_t)entry.reasonCode));
    }
    snprintf(out.get() + off, n - off, "==================================================");
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain; charset=utf-8", out.get());
  } else {
    String historyHtml;
    historyHtml.reserve(1200);
    for (int i = 1; i < limitLogs; i++) {
      BootLog entry;
      if (!getBootLogAt(i, entry) || entry.timestamp == 0) continue;
      char line[160];
      snprintf(line, sizeof(line), "<div class='row'><span class='k mono'>%s</span><span class='v mono'>%s</span></div>",
        formatTimestamp(entry.timestamp).c_str(), getResetReasonString((esp_reset_reason_t)entry.reasonCode));
      historyHtml += line;
    }

    const size_t n = 2800 + historyHtml.length();
    std::unique_ptr<char[]> body(new char[n]);
    int off = snprintf(body.get(), n,
      "<h1>&#128295; Crash Logs &amp; Debug</h1>"
      "<h3>Live</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Uptime since boot</span><span class='v mono' id='up'>%s</span></div>"
      "<div class='row'><span class='k'>Uptime since flash</span><span class='v mono' id='upf'>%s</span></div>"
      "<div class='row'><span class='k'>Approx. downtime</span><span class='v mono'>%s</span></div>"
      "</div>"
      "<h3>Boot Stats</h3>"
      "<div class='card'>"
      "<div class='row'><span class='k'>Boot time</span><span class='v mono'>%lu ms</span></div>"
      "<div class='row'><span class='k'>Wi-Fi connect</span><span class='v mono'>%lu ms</span></div>"
      "<div class='row'><span class='k'>Total boots</span><span class='v mono'>%lu</span></div>"
      "<div class='row'><span class='k'>Last reset</span><span class='v mono'>%s</span></div>"
      "<div class='row'><span class='k'>Last reset time</span><span class='v mono'>%s</span></div>"
      "</div>"
      "<h3>Remote Access</h3>"
      "<div class='card'><div class='row'><span class='k'>Tailscale</span>"
      "<span class='v'><span class='pill %s'>%s</span></span></div>"
      "<div class='row'><span class='k'>VPN IP</span><span class='v mono'>%s</span></div></div>"
      "<h3>OTA Updates</h3>"
      "<div class='card'><div class='row'><span class='k'>Status</span>"
      "<span class='v'><span class='pill %s'>%s</span></span></div></div>"
      "<div class='actions'>"
      "<form action='/ota/enable' method='POST'><input type='hidden' name='key' value='%s'>"
      "<button class='warn' type='submit'>Enable 10m</button></form>"
      "<form action='/ota/disable' method='POST'><button type='submit'>Disable</button></form>"
      "</div>",
      bootDuration, flashDuration, downtimeDuration,
      boot_time_ms, wifi_connect_ms, totalBootCount,
      lastResetCause.c_str(), lastResetTime.c_str(),
      vpnConnected ? "on" : "off", vpnConnected ? "Connected" : "Not Connected",
      vpnIp.c_str(),
      otaEnabled ? "on" : "off", otaEnabled ? "Enabled" : "Disabled",
      OTA_KEY
    );

    if (historyHtml.length() > 0) {
      off += snprintf(body.get() + off, n - off,
        "<h3>Previous Boot History</h3><div class='card'>%s</div>", historyHtml.c_str());
    }

    snprintf(body.get() + off, n - off,
      "<div class='actions' style='margin-top:18px;'>"
      "<form action='/reboot' method='POST'><button class='danger' type='submit'>&#128260; Reboot</button></form>"
      "<form action='/clear-logs' method='POST' onsubmit='return confirm(\"Clear all crash history and flash timers?\");'>"
      "<button class='warn' type='submit'>&#128465; Clear Logs</button></form>"
      "</div>"
      "<a class='back' href='/main'>&larr; Back to dashboard</a>"
    );

    server.sendHeader("Connection", "close");
    server.send(200, "text/html; charset=utf-8", wrapPage("Crash Logs & Debug", "&#128295;", body.get(), POLL_SCRIPT));
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
      "<h2>&#128465; Logs Cleared</h2>"
      "<p style='color:var(--text-dim);'>Crash history and flash timers have been reset. Redirecting&hellip;</p>";
    String page = wrapPage("Logs Cleared", "&#128465;", body,
      "<script>setTimeout(function(){window.location.href='/debug';},2000);</script>");
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
      "<h2>&#128260; Rebooting&hellip;</h2>"
      "<p style='color:var(--text-dim);'>The ESP32 is restarting. Please wait 5 seconds before refreshing.</p>";
    String page = wrapPage("Rebooting...", "&#128260;", body,
      "<script>setTimeout(function(){window.location.href='/main';},5000);</script>");
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

void setup() {
  setCpuFrequencyMhz(240);
  initServo();
  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle); delay(300); myservo.detach();

  loadBootHistory();
  recordBootEvent();

  uint32_t wifi_start = millis();
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.setSleep(false);
  WiFi.config(local_IP, gateway, subnet, primaryDNS);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) delay(200);
  wifi_connect_ms = millis() - wifi_start;

microlink_config_t ml_conf = {0};
  ml_conf.auth_key = TAILSCALE_KEY;
  ml_conf.device_name = TAILSCALE_HOST;
  ml_conf.advertise_routes = "192.168.1.50/32";
  ml_conf.enable_derp = true;
  ml_conf.enable_stun = true;
  ml_conf.enable_disco = true;
  ml_conf.max_peers = 8;

  ml = microlink_init(&ml_conf);   // now a global - /debug can query it any time
  microlink_start(ml);             // non-blocking, connects in background

  configTime(19800, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

  if (MDNS.begin("esp32")) MDNS.addService("http", "tcp", 80);

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
  server.onNotFound(handleNotFound);
  server.begin();

  // Run the HTTP server on its own task (Core 0, pri 4) so a slow TCP
  // connection over Tailscale DERP can never block loop(), OTA, or the servo.
  // WebServer is not thread-safe, but we guarantee only this task calls it,
  // so there is no data race.
  xTaskCreatePinnedToCore(
    [](void*) {
      for (;;) {
        server.handleClient();
        vTaskDelay(pdMS_TO_TICKS(2));
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

  vTaskDelay(pdMS_TO_TICKS(10));
}