#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Preferences.h>
#include "web_pages.h"

// Default servo calibration metrics as requested: rest: 90 (center), press: 100, duration: 200
static const int DEFAULT_REST_ANGLE = 90;
static const int DEFAULT_PRESS_ANGLE = 100;
static const int DEFAULT_PRESS_DURATION_MS = 200;

// Shared global state (defined in main.cpp)
extern Servo myservo;
extern const int servoPin;
extern int restAngle;
extern int pressAngle;
extern int pressDurationMs;
extern bool isCalibrated;
extern volatile bool pendingTestTap;
extern int testRestAngle;
extern int testPressAngle;
extern int testDurationMs;
extern volatile bool isHoldActive;
extern unsigned long holdStartTimeMs;
extern TaskHandle_t loopTaskHandle;

/**
 * @brief Load calibration data from NVS namespace "servo_cal"
 */
inline void loadCalibration() {
  Preferences p;
  p.begin("servo_cal", true);
  isCalibrated = p.getBool("calibrated", false);
  if (isCalibrated) {
    restAngle = p.getInt("rest_angle", DEFAULT_REST_ANGLE);
    pressAngle = p.getInt("press_angle", DEFAULT_PRESS_ANGLE);
    pressDurationMs = p.getInt("press_dur", DEFAULT_PRESS_DURATION_MS);
    if (restAngle < 0 || restAngle > 180 || pressAngle < 0 || pressAngle > 180) {
      restAngle = DEFAULT_REST_ANGLE;
      pressAngle = DEFAULT_PRESS_ANGLE;
    }
  } else {
    restAngle = DEFAULT_REST_ANGLE;
    pressAngle = DEFAULT_PRESS_ANGLE;
    pressDurationMs = DEFAULT_PRESS_DURATION_MS;
  }
  p.end();
}

/**
 * @brief Save calibration metrics to NVS and mark device calibrated
 */
inline bool saveCalibration(int rest, int press, int dur) {
  if (rest < 0 || rest > 180 || press < 0 || press > 180 || dur < 50 || dur > 5000) {
    return false;
  }
  restAngle = rest;
  pressAngle = press;
  pressDurationMs = dur;
  isCalibrated = true;

  Preferences p;
  p.begin("servo_cal", false);
  p.putBool("calibrated", true);
  p.putInt("rest_angle", restAngle);
  p.putInt("press_angle", pressAngle);
  p.putInt("press_dur", pressDurationMs);
  p.end();

  // Ensure servo rests at the new rest angle
  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle);
  int travelDelay = max(60, abs(pressAngle - restAngle) * 3);
  delay(travelDelay);
  myservo.detach();

  return true;
}

/**
 * @brief Reset calibration data in NVS and restore initial uncalibrated defaults
 */
inline void resetCalibration() {
  Preferences p;
  p.begin("servo_cal", false);
  p.clear();
  p.end();

  isCalibrated = false;
  restAngle = DEFAULT_REST_ANGLE;
  pressAngle = DEFAULT_PRESS_ANGLE;
  pressDurationMs = DEFAULT_PRESS_DURATION_MS;

  myservo.attach(servoPin, 500, 2400);
  myservo.write(restAngle);
  delay(150);
  myservo.detach();
}

/**
 * @brief Generate the bash script served to curl clients for interactive calibration
 */
inline String generateCurlCalibrateScript(const String &host) {
  String script = F(R"raw(#!/bin/bash
HOST="{{HOST}}"
LIVE=$(curl -s http://$HOST/api/live 2>/dev/null)
CUR_CAL=$(echo "$LIVE" | grep -o '"cal":[01]' | cut -d: -f2)
CUR_REST=$(echo "$LIVE" | grep -o '"s_rest":[-0-9]*' | cut -d: -f2)
CUR_PRESS=$(echo "$LIVE" | grep -o '"s_press":[-0-9]*' | cut -d: -f2)
CUR_DUR=$(echo "$LIVE" | grep -o '"s_dur":[0-9]*' | cut -d: -f2)
if [[ -z "$CUR_REST" ]]; then CUR_REST=90; fi
if [[ -z "$CUR_PRESS" ]]; then CUR_PRESS=100; fi
if [[ -z "$CUR_DUR" ]]; then CUR_DUR=200; fi
ORIG_REST=$CUR_REST
REST=$CUR_REST
PRESS=$CUR_PRESS
DUR=$CUR_DUR
while true; do
  clear
  echo '=========================================='
  echo '      SERVO CALIBRATION TOOL (ESP32)      '
  echo '=========================================='
  if [[ "$CUR_CAL" == "1" ]]; then
    echo ' Status: CALIBRATED'
  else
    echo ' Status: UNCALIBRATED (Initial Setup)'
  fi
  echo " Current Config: Rest=${REST}° | Press=${PRESS}° | Duration=${DUR}ms"
  echo '=========================================='
  if [[ "$CUR_CAL" == "1" ]]; then
    echo ' [1] Start Guided Calibration (Steps 1-3)'
    echo ' [T] Test Current Tap'
    echo ' [R] Reset Calibration Data'
    echo ' [X] Exit'
    echo '=========================================='
    echo ''
    mopt=""
    read -n 1 -s -p "Select an option: " mopt </dev/tty
    echo ''
    if [[ "$mopt" == "r" || "$mopt" == "R" ]]; then
      rconf=""
      read -n 1 -s -p 'Are you sure you want to clear calibration data? [y/N]: ' rconf </dev/tty
      echo ''
      if [[ "$rconf" == "y" || "$rconf" == "Y" ]]; then
        curl -s -X POST "http://$HOST/api/calibrate/reset"
        echo -e '\n[+] Calibration data cleared. Device reset to uncalibrated.\n'
        sleep 1.5
        exit 0
      fi
      continue
    elif [[ "$mopt" == "t" || "$mopt" == "T" ]]; then
      echo '[*] Running test tap...'
      curl -s -X POST "http://$HOST/api/calibrate/test?rest=$REST&press=$PRESS&dur=$DUR"
      echo '[+] Test completed.'
      sleep 1.5
      continue
    elif [[ "$mopt" == "x" || "$mopt" == "X" ]]; then
      echo 'Exiting.'
      exit 0
    elif [[ "$mopt" != "1" && "$mopt" != "c" && "$mopt" != "C" ]]; then
      continue
    fi
  fi
  echo ''
  while true; do
    echo '[ Step 1/3: Rest Angle ] (Hovering just above button)'
    read -p "Enter rest angle (0 to 180, 'r' to reset, 'q' to cancel) [$REST]: " input </dev/tty
    if [[ "$input" == "r" || "$input" == "R" ]]; then
      rconf=""
      read -n 1 -s -p 'Are you sure you want to clear calibration data? [y/N]: ' rconf </dev/tty
      echo ''
      if [[ "$rconf" == "y" || "$rconf" == "Y" ]]; then
        curl -s -X POST "http://$HOST/api/calibrate/reset"
        echo -e '\n[+] Calibration data cleared. Device reset to uncalibrated.\n'
        sleep 1.5
        exit 0
      fi
      continue
    fi
    if [[ "$input" == "q" || "$input" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ -n "$input" ]]; then
      if [[ "$input" =~ ^[0-9]+$ ]] && [ "$input" -ge 0 ] && [ "$input" -le 180 ]; then
        REST=$input
      else
        echo '[!] Invalid angle. Enter a number between 0 and 180.'
        continue
      fi
    fi
    curl -s -X POST "http://$HOST/api/calibrate/move?angle=$REST" >/dev/null
    echo "[+] Servo driven to ${REST}°."
    conf=""
    read -n 1 -s -p "Confirm rest angle (${REST}°)? [y: next / n: retry / q: cancel]: " conf </dev/tty
    echo ''
    if [[ "$conf" == "q" || "$conf" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ "$conf" == "y" || "$conf" == "Y" ]]; then break; fi
  done
  echo ''
  while true; do
    echo '[ Step 2/3: Press Angle ] (Pushing button fully, not buzzing)'
    read -p "Enter press angle (0 to 180, 'q' to cancel) [$PRESS]: " input </dev/tty
    if [[ "$input" == "q" || "$input" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ -n "$input" ]]; then
      if [[ "$input" =~ ^[0-9]+$ ]] && [ "$input" -ge 0 ] && [ "$input" -le 180 ]; then
        PRESS=$input
      else
        echo '[!] Invalid angle. Enter a number between 0 and 180.'
        continue
      fi
    fi
    echo "[*] Previewing press stroke to ${PRESS}°..."
    curl -s -X POST "http://$HOST/api/calibrate/move?angle=$PRESS" >/dev/null
    sleep 0.35
    curl -s -X POST "http://$HOST/api/calibrate/move?angle=$REST" >/dev/null
    echo "[+] Servo stroke completed."
    conf=""
    read -n 1 -s -p "Confirm press angle (${PRESS}°)? [y: next / n: retry / q: cancel]: " conf </dev/tty
    echo ''
    if [[ "$conf" == "q" || "$conf" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ "$conf" == "y" || "$conf" == "Y" ]]; then
      break
    fi
  done
  echo ''
  while true; do
    echo '[ Step 3/3: Press Duration ] (Hold duration in ms)'
    read -p "Enter press duration (50-3000 ms, 'q' to cancel) [$DUR]: " input </dev/tty
    if [[ "$input" == "q" || "$input" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ -n "$input" ]]; then
      if [[ "$input" =~ ^[0-9]+$ ]] && [ "$input" -ge 50 ] && [ "$input" -le 3000 ]; then
        DUR=$input
      else
        echo '[!] Invalid duration. Enter a number between 50 and 3000.'
        continue
      fi
    fi
    echo "[*] Executing tap for $DUR ms (Rest: ${REST}°, Press: ${PRESS}°)..."
    curl -s -X POST "http://$HOST/api/calibrate/test?rest=$REST&press=$PRESS&dur=$DUR" >/dev/null
    echo '[+] Tap test complete.'
    conf=""
    read -n 1 -s -p "Confirm press duration ($DUR ms)? [y: next / n: retry / q: cancel]: " conf </dev/tty
    echo ''
    if [[ "$conf" == "q" || "$conf" == "Q" ]]; then
      echo '[*] Cancelling calibration... returning servo to saved rest position.'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Calibration cancelled.'
      exit 0
    fi
    if [[ "$conf" == "y" || "$conf" == "Y" ]]; then break; fi
  done
  echo ''
  while true; do
    clear
    echo '=========================================='
    echo '       CALIBRATION REVIEW & ACTIONS       '
    echo '=========================================='
    echo " Rest Angle     : ${REST}°"
    echo " Press Angle    : ${PRESS}°"
    echo " Press Duration : ${DUR} ms"
    echo '------------------------------------------'
    echo ' [S] Save Calibration'
    echo ' [T] Test Again'
    echo ' [V] Type in Another Value'
    echo ' [C] Restart Whole Calibration'
    echo ' [R] Reset Calibration Data'
    echo ' [X] Exit without Saving'
    echo '=========================================='
    opt=""
    read -n 1 -s -p "Select an option: " opt </dev/tty
    echo ''
    if [[ "$opt" == "s" || "$opt" == "S" ]]; then
      echo '[*] Saving calibration data to ESP32...'
      curl -s -X POST "http://$HOST/api/calibrate/save?rest=$REST&press=$PRESS&dur=$DUR"
      echo -e '\n[+] Calibration saved successfully!\n'
      sleep 1.5
      exit 0
    elif [[ "$opt" == "t" || "$opt" == "T" ]]; then
      echo '[*] Running test tap...'
      curl -s -X POST "http://$HOST/api/calibrate/test?rest=$REST&press=$PRESS&dur=$DUR"
      echo '[+] Test completed.'
      sleep 1
    elif [[ "$opt" == "v" || "$opt" == "V" ]]; then
      echo ''
      echo 'Select metric to change:'
      echo " [1] Rest Angle (${REST}°)"
      echo " [2] Press Angle (${PRESS}°)"
      echo " [3] Press Duration (${DUR} ms)"
      echo ' [B] Back to Review'
      vopt=""
      read -n 1 -s -p 'Select metric: ' vopt </dev/tty
      echo ''
      if [[ "$vopt" == "1" ]]; then
        read -p "Enter new Rest Angle (0 to 180) [$REST]: " nval </dev/tty
        if [[ "$nval" =~ ^[0-9]+$ ]] && [ "$nval" -ge 0 ] && [ "$nval" -le 180 ]; then
          REST=$nval
          curl -s -X POST "http://$HOST/api/calibrate/move?angle=$REST" >/dev/null
          echo "[+] Servo moved to ${REST}°."
          sleep 1
        fi
      elif [[ "$vopt" == "2" ]]; then
        read -p "Enter new Press Angle (0 to 180) [$PRESS]: " nval </dev/tty
        if [[ "$nval" =~ ^[0-9]+$ ]] && [ "$nval" -ge 0 ] && [ "$nval" -le 180 ]; then
          PRESS=$nval
          curl -s -X POST "http://$HOST/api/calibrate/move?angle=$PRESS" >/dev/null
          echo "[+] Servo moved to ${PRESS}°."
          sleep 1
          curl -s -X POST "http://$HOST/api/calibrate/move?angle=$REST" >/dev/null
        fi
      elif [[ "$vopt" == "3" ]]; then
        read -p "Enter new Press Duration (50-3000 ms) [$DUR]: " nval </dev/tty
        if [[ "$nval" =~ ^[0-9]+$ ]] && [ "$nval" -ge 50 ] && [ "$nval" -le 3000 ]; then
          DUR=$nval
          curl -s -X POST "http://$HOST/api/calibrate/test?rest=$REST&press=$PRESS&dur=$DUR" >/dev/null
          echo '[+] Tap test complete.'
          sleep 1
        fi
      fi
    elif [[ "$opt" == "c" || "$opt" == "C" ]]; then
      break
    elif [[ "$opt" == "r" || "$opt" == "R" ]]; then
      rconf=""
      read -n 1 -s -p 'Are you sure you want to clear calibration data? [y/N]: ' rconf </dev/tty
      echo ''
      if [[ "$rconf" == "y" || "$rconf" == "Y" ]]; then
        curl -s -X POST "http://$HOST/api/calibrate/reset"
        echo -e '\n[+] Calibration data cleared. Device reset to uncalibrated.\n'
        sleep 1.5
        exit 0
      fi
    elif [[ "$opt" == "x" || "$opt" == "X" ]]; then
      echo '[*] Returning servo to saved rest position...'
      curl -s -X POST "http://$HOST/api/calibrate/move?angle=$ORIG_REST" >/dev/null
      echo 'Exiting calibration.'
      exit 0
    fi
  done
done
)raw");
  script.replace("{{HOST}}", host);
  return script;
}

/**
 * @brief Stream the HTML calibration page for web browsers
 */
inline void sendCalibratePage(WebServer &server, int curRest, int curPress, int curDur, bool calibrated) {
  String statusBadge = calibrated 
    ? "<span class='pill on'>Calibrated</span>" 
    : "<span class='pill warn'>Initial Setup Needed</span>";

  String backLinkHtml = calibrated 
    ? "<a class='back' href='/main'>&larr; Back to Dashboard</a>" 
    : "";

  String revertBtnHtml = calibrated
    ? "<button type='button' id='btn-revert' title='Revert to saved metrics'><svg width='15' height='15' viewBox='0 0 24 24' fill='none' stroke='currentColor' stroke-width='2.2' stroke-linecap='round' stroke-linejoin='round'><path d='M3 12a9 9 0 1 0 9-9 9.75 9.75 0 0 0-6.74 2.74L3 8'/><path d='M3 3v5h5'/></svg></button>"
    : "";

  String body;
  body.reserve(4096);

  body += "<style>"
          ".cal-header-row{display:flex;justify-content:space-between;align-items:center;margin:28px 0 10px;}"
          ".cal-header-row h3{margin:0;padding:0;}"
          "#btn-revert{width:28px !important;min-width:28px !important;max-width:28px !important;height:28px !important;min-height:28px !important;max-height:28px !important;padding:0 !important;margin:0 !important;background:none !important;border:none !important;color:var(--on-surface-v);cursor:pointer;border-radius:50% !important;display:inline-flex;align-items:center;justify-content:center;box-shadow:none !important;backdrop-filter:none !important;-webkit-backdrop-filter:none !important;transition:color .25s ease,transform .35s cubic-bezier(0.4,0,0.2,1);outline:none;}"
          "#btn-revert:hover{color:var(--primary) !important;background:none !important;box-shadow:none !important;transform:rotate(-360deg);}"
          "#btn-revert:active{transform:scale(0.85) rotate(-360deg);}"
          ".dial-grid{display:flex;justify-content:space-around;align-items:flex-start;gap:12px;padding:22px 14px 16px;border-bottom:1px solid rgba(255,255,255,0.06);flex-wrap:wrap;}"
          ".dial-box{flex:1;min-width:140px;max-width:240px;display:flex;flex-direction:column;align-items:center;text-align:center;}"
          ".dial-title{font-size:14px;font-weight:600;color:var(--on-surface);margin-bottom:2px;}"
          ".dial-sub{font-size:11px;color:var(--on-surface-v);margin-bottom:12px;white-space:nowrap;}"
          ".dial-svg-wrap{position:relative;width:150px;height:150px;touch-action:none;user-select:none;-webkit-user-select:none;cursor:grab;}"
          ".dial-svg-wrap:active{cursor:grabbing;}"
          ".dial-svg{width:100%;height:100%;overflow:visible;}"
          ".dial-bg{fill:none;stroke:rgba(255,255,255,0.08);stroke-width:8;stroke-linecap:round;}"
          ".dial-bar{fill:none;stroke:var(--primary);stroke-width:8;stroke-linecap:round;filter:drop-shadow(0 0 6px rgba(138,180,248,0.35));}"
          ".dial-thumb{fill:#ffffff;stroke:var(--primary);stroke-width:3.5;filter:drop-shadow(0 0 5px rgba(138,180,248,0.6));transition:transform .1s ease;}"
          ".dial-center{position:absolute;top:50%;left:50%;transform:translate(-50%,-50%);display:flex;align-items:center;justify-content:center;pointer-events:auto;z-index:10;touch-action:auto;}"
          ".dial-badge{display:inline-flex;align-items:center;justify-content:center;background:var(--surface-c);border:1px solid var(--surface-border);border-radius:12px;padding:4px 10px;min-width:64px;box-shadow:inset 0 1px 3px rgba(0,0,0,0.35);transition:border-color .2s,box-shadow .2s;cursor:text;pointer-events:auto;touch-action:auto;user-select:text;-webkit-user-select:text;}"
          ".dial-badge:focus-within{border-color:var(--primary);box-shadow:0 0 10px rgba(138,180,248,0.25);}"
          ".cal-dial-input{background:transparent !important;border:none !important;color:var(--primary);font-family:'SF Mono',Menlo,Consolas,monospace;font-size:17px;font-weight:700;padding:0 !important;margin:0 !important;text-align:center !important;min-width:1.2ch;max-width:5ch;field-sizing:content;outline:none;-webkit-appearance:none;-moz-appearance:textfield;appearance:none;line-height:1.2;cursor:text;user-select:text;-webkit-user-select:text;pointer-events:auto;touch-action:auto;}"
          ".cal-dial-input::-webkit-inner-spin-button,.cal-dial-input::-webkit-outer-spin-button{-webkit-appearance:none;margin:0;}"
          ".dial-deg{font-size:13px;font-weight:700;color:var(--primary);opacity:0.85;margin-left:1px;line-height:1.2;user-select:none;-webkit-user-select:none;}"
          ".dial-limits{position:absolute;bottom:2px;left:0;right:0;display:flex;justify-content:space-between;padding:0 10px;font-size:9.5px;color:var(--on-surface-v);pointer-events:none;font-family:'SF Mono',Menlo,monospace;}"
          ".cal-row{display:flex;flex-direction:column;align-items:stretch;padding:16px 20px;border-bottom:1px solid rgba(255,255,255,0.06);margin:0;}"
          ".cal-row:first-child{border-top-left-radius:19px;border-top-right-radius:19px;}"
          ".cal-row:last-child{border-bottom:none;border-bottom-left-radius:19px;border-bottom-right-radius:19px;}"
          ".cal-num-box{display:inline-flex;align-items:center;background:var(--surface-c);border:1px solid var(--surface-border);border-radius:10px;padding:3px 8px;box-shadow:inset 0 1px 3px rgba(0,0,0,0.3);}"
          ".cal-num-box:focus-within{border-color:var(--primary);box-shadow:0 0 10px rgba(138,180,248,0.25);}"
          ".cal-num-raw{background:transparent !important;border:none !important;color:var(--primary);font-family:'SF Mono',Menlo,Consolas,monospace;font-size:14px;font-weight:700;padding:0 !important;margin:0 !important;text-align:right;width:48px;outline:none;-moz-appearance:textfield;appearance:textfield;}"
          ".cal-num-raw::-webkit-inner-spin-button,.cal-num-raw::-webkit-outer-spin-button{-webkit-appearance:none;margin:0;}"
          ".cal-slider{-webkit-appearance:none !important;appearance:none !important;width:100% !important;height:6px !important;background:rgba(255,255,255,0.1);border-radius:3px !important;outline:none !important;margin:8px 0 !important;padding:0 !important;cursor:pointer !important;touch-action:pan-y !important;}"
          ".cal-slider::-webkit-slider-runnable-track{width:100%;height:6px;background:transparent;border-radius:3px;border:none;}"
          ".cal-slider::-webkit-slider-thumb{-webkit-appearance:none !important;appearance:none !important;height:18px;width:18px;border-radius:50%;background:#ffffff;border:2px solid var(--primary);box-shadow:0 0 8px rgba(138,180,248,0.6);cursor:pointer;margin-top:-6px;transition:transform .15s ease,box-shadow .15s ease;}"
          ".cal-slider::-webkit-slider-thumb:active{transform:scale(1.2);box-shadow:0 0 14px rgba(138,180,248,0.9);}"
          ".cal-slider::-moz-range-track{width:100%;height:6px;background:rgba(255,255,255,0.1);border-radius:3px;border:none;}"
          ".cal-slider::-moz-range-thumb{height:18px;width:18px;border-radius:50%;background:#ffffff;border:2px solid var(--primary);box-shadow:0 0 8px rgba(138,180,248,0.6);cursor:pointer;border:none;}"
          ".btn-hold{width:100%;background:linear-gradient(135deg,rgba(138,180,248,0.18) 0%,rgba(59,130,246,0.1) 100%);border:2px solid rgba(138,180,248,0.38);color:var(--primary);font-size:14.5px;font-weight:700;height:54px;border-radius:27px;user-select:none;-webkit-user-select:none;touch-action:none;display:inline-flex;align-items:center;justify-content:center;gap:10px;cursor:pointer;transition:all .15s ease;}"
          ".btn-hold.holding{background:linear-gradient(135deg,rgba(52,211,153,0.35) 0%,rgba(16,185,129,0.2) 100%) !important;border-color:var(--success) !important;color:var(--success) !important;box-shadow:0 0 25px rgba(52,211,153,0.45) !important;transform:scale(0.97);}"
          "</style>";

  body += "<h1>&#127919; Servo Calibration</h1>";
  body += "<div style='text-align:center;margin-bottom:20px;'>" + statusBadge + "</div>";
  body += "<div class='cal-header-row'><h3>Configuration Metrics</h3>" + revertBtnHtml + "</div>";
  body += "<div class='card' style='padding:0;overflow:hidden;'>";

  // Circular Dials Row for Rest and Press Angles
  body += "<div class='dial-grid no-copy'>";

  // Rest Angle Dial
  body += "<div class='dial-box' id='rest-dial-box'>";
  body += "<div class='dial-title'>Rest Angle</div>";
  body += "<div class='dial-sub'>Hovering Just Above Button</div>";
  body += "<div class='dial-svg-wrap' id='rest-dial-wrap'>";
  body += "<svg class='dial-svg' viewBox='0 0 160 160'>";
  body += "<path class='dial-bg' d='M 60.16 134.50 A 58 58 0 1 1 99.84 134.50'/>";
  body += "<path class='dial-bar' id='rest-dial-bar' d=''/>";
  body += "<circle class='dial-thumb' id='rest-dial-thumb' cx='80' cy='22' r='8.5'/>";
  body += "</svg>";
  body += "<div class='dial-center'>";
  body += "<div class='dial-badge'>";
  body += "<input type='text' inputmode='numeric' pattern='[0-9]*' id='rest-num' value='" + String(curRest) + "' class='cal-dial-input' style='width:3.2ch;text-align:center;font-variant-numeric:tabular-nums;'>";
  body += "<span class='dial-deg'>&deg;</span>";
  body += "</div></div>";
  body += "<div class='dial-limits'>";
  body += "<span>0&deg;</span><span>180&deg;</span>";
  body += "</div></div></div>";

  // Press Angle Dial
  body += "<div class='dial-box' id='press-dial-box'>";
  body += "<div class='dial-title'>Press Angle</div>";
  body += "<div class='dial-sub'>Pushing Button Fully</div>";
  body += "<div class='dial-svg-wrap' id='press-dial-wrap'>";
  body += "<svg class='dial-svg' viewBox='0 0 160 160'>";
  body += "<path class='dial-bg' d='M 60.16 134.50 A 58 58 0 1 1 99.84 134.50'/>";
  body += "<path class='dial-bar' id='press-dial-bar' d=''/>";
  body += "<circle class='dial-thumb' id='press-dial-thumb' cx='80' cy='22' r='8.5'/>";
  body += "</svg>";
  body += "<div class='dial-center'>";
  body += "<div class='dial-badge'>";
  body += "<input type='text' inputmode='numeric' pattern='[0-9]*' id='press-num' value='" + String(curPress) + "' class='cal-dial-input' style='width:3.2ch;text-align:center;font-variant-numeric:tabular-nums;'>";
  body += "<span class='dial-deg'>&deg;</span>";
  body += "</div></div>";
  body += "<div class='dial-limits'>";
  body += "<span>0&deg;</span><span>180&deg;</span>";
  body += "</div></div></div>";

  body += "</div>"; // End dial-grid

  // Press Duration Row (below dials)
  body += "<div class='cal-row no-copy'>";
  body += "<div style='display:flex;justify-content:space-between;align-items:center;margin-bottom:6px;'>";
  body += "<div><div style='font-size:14px;font-weight:600;color:var(--on-surface);'>Press Duration</div>";
  body += "<div style='font-size:11.5px;color:var(--on-surface-v);'>Hold Duration During Tap</div></div>";
  body += "<div class='cal-num-box'>";
  body += "<input type='text' inputmode='numeric' pattern='[0-9]*' id='dur-num' value='" + String(curDur) + "' class='cal-num-raw'>";
  body += "<span style='font-size:12px;font-weight:600;color:var(--on-surface-v);margin-left:4px;'>ms</span>";
  body += "</div></div>";
  body += "<div style='margin-top:4px;'>";
  body += "<input type='range' id='dur-range' min='50' max='3000' step='10' value='" + String(curDur) + "' class='cal-slider'>";
  body += "</div></div>";

  body += "</div>"; // End card

  // Custom Live Press & Hold Button (SEPARATE CARD)
  body += "<h3>Live Manual Control</h3>";
  body += "<div class='card' style='padding:20px;text-align:center;'>";
  body += "<button type='button' id='btn-hold' class='btn-hold'>";
  body += "&#128071; Press &amp; Hold</button>";
  body += "<div style='font-size:12px;color:var(--on-surface-v);margin-top:12px;line-height:1.4;'>";
  body += "Hold to press servo live to Press Angle<br>Release to return to Rest Angle</div>";
  body += "</div>";

  // Action Buttons: Save, Test, Reset (SEPARATED FROM PRESS & HOLD BUTTON)
  body += "<h3>Calibration Actions</h3>";
  body += "<div class='card' style='padding:18px 20px;'>";
  body += "<div class='actions' style='margin-top:0;margin-bottom:12px;'>";
  body += "<button type='button' id='btn-test' class='primary' style='background:rgba(138,180,248,0.16);color:var(--primary);'>&#9654; Test Tap</button>";
  body += "<button type='button' id='btn-save' style='background:rgba(52,211,153,0.18);border-color:rgba(52,211,153,0.35);color:var(--success);'>&#10004; Save</button>";
  body += "</div>";
  body += "<div class='actions' style='margin-top:0;'>";
  body += "<button type='button' id='btn-reset' class='danger'>&#128465; Reset Calibration</button>";
  body += "</div>";
  body += "</div>";

  body += backLinkHtml;

  String calScript = F(
    "<script>"
    "(function(){"
    "function initCal(){"
      "var savedRest={{REST}},savedPress={{PRESS}},savedDur={{DUR}};"
      "var hasSaved=false;"
      "var rNum=document.getElementById('rest-num');"
      "var pNum=document.getElementById('press-num');"
      "var dNum=document.getElementById('dur-num');"
      "var dRng=document.getElementById('dur-range');"
      "var holdBtn=document.getElementById('btn-hold');"
      "var testBtn=document.getElementById('btn-test');"
      "var saveBtn=document.getElementById('btn-save');"
      "var resetBtn=document.getElementById('btn-reset');"
      "var revertBtn=document.getElementById('btn-revert');"
      "if(!rNum||!pNum||!dNum||!dRng||!holdBtn||!testBtn||!saveBtn||!resetBtn)return;"

      "var lastDHaptic=dRng.value;"

      "function hapticTick(val,prevVal){"
        "if(val!==prevVal){"
          "if(navigator.vibrate){try{navigator.vibrate(6);}catch(e){}}"
        "}"
      "}"

      // Update linear slider gradient fill
      "function updateSliderFill(val){"
        "var pct=((val-50)/(3000-50))*100;"
        "dRng.style.background='linear-gradient(to right,var(--primary) 0%,var(--primary) '+pct+'%,rgba(255,255,255,0.1) '+pct+'%,rgba(255,255,255,0.1) 100%)';"
      "}"
      "updateSliderFill(parseInt(dRng.value,10)||savedDur);"

      // Debounced angle update for rest position
      "var mvTm=null;"
      "function moveLive(deg){"
        "clearTimeout(mvTm);"
        "mvTm=setTimeout(function(){fetch('/api/calibrate/move?angle='+deg,{method:'POST'}).catch(function(){});},75);"
      "}"

      // Return servo to saved resting state on exit without saving
      "function returnToRest(){"
        "if(!hasSaved){"
          "fetch('/api/calibrate/move?angle='+savedRest,{method:'POST',keepalive:true}).catch(function(){});"
        "}"
      "}"
      "var backLink=document.querySelector('.back');"
      "if(backLink){backLink.addEventListener('click',function(){returnToRest();});}"
      "window.addEventListener('pagehide',returnToRest);"
      "window.addEventListener('beforeunload',returnToRest);"

      // Circular Dial Renderer
      "function setDial(type,angle){"
        "var bar=document.getElementById(type+'-dial-bar');"
        "var thumb=document.getElementById(type+'-dial-thumb');"
        "var num=document.getElementById(type+'-num');"
        "if(!bar||!thumb||!num)return;"
        "if(angle>180)angle=180;if(angle<0)angle=0;"
        "num.value=angle;"
        "var phi=((angle-90)/90)*160;"
        "var rad=phi*(Math.PI/180);"
        "var x=(80+58*Math.sin(rad)).toFixed(2);"
        "var y=(80-58*Math.cos(rad)).toFixed(2);"
        "thumb.setAttribute('cx',x);"
        "thumb.setAttribute('cy',y);"
        "if(angle<=0){bar.setAttribute('d','');}"
        "else{"
          "var largeArc=(angle>101)?1:0;"
          "bar.setAttribute('d','M 60.16 134.50 A 58 58 0 '+largeArc+' 1 '+x+' '+y);"
        "}"
      "}"

      // Circular Dial Controller with Bottom Gap Barrier
      "function bindDial(type,isRest){"
        "var wrap=document.getElementById(type+'-dial-wrap');"
        "var num=document.getElementById(type+'-num');"
        "if(!wrap||!num)return;"
        "var dragging=false;"
        "var curAng=parseInt(num.value,10);if(isNaN(curAng))curAng=90;"
        "var lastHaptic=curAng;"

        "function getAngle(e){"
          "var rect=wrap.getBoundingClientRect();"
          "var cx=rect.left+rect.width/2;"
          "var cy=rect.top+rect.height/2;"
          "var cxEv=e.clientX,cyEv=e.clientY;"
          "if(e.touches&&e.touches.length>0){cxEv=e.touches[0].clientX;cyEv=e.touches[0].clientY;}"
          "var dx=cxEv-cx,dy=cyEv-cy;"
          "if(dx*dx+dy*dy<36)return curAng;"

          // Angle from 12 o'clock in degrees (-180 to +180)
          "var deg=Math.atan2(dx,-dy)*(180/Math.PI);"

          // Barrier Protection:
          // Prevent crossing the bottom gap from 0 deg into 180 deg
          "if(curAng<45&&deg>100){return 0;}"
          // Prevent crossing the bottom gap from 180 deg into 0 deg
          "if(curAng>135&&deg<-100){return 180;}"

          // Gap clamping (bottom 40 deg gap: |deg| > 160)
          "if(Math.abs(deg)>160){return deg<0?0:180;}"

          "var ang=Math.round(90+(deg/160)*90);"
          "if(ang>180)ang=180;if(ang<0)ang=0;"
          "return ang;"
        "}"

        "function onDown(e){"
          "if(e.target&&e.target.closest&&e.target.closest('.dial-center'))return;"
          "e.preventDefault();dragging=true;"
          "curAng=parseInt(num.value,10);if(isNaN(curAng))curAng=90;"
          "var a=getAngle(e);"
          "curAng=a;"
          "setDial(type,a);"
          "if(isRest)moveLive(a);"
          "hapticTick(a,lastHaptic);lastHaptic=a;"
        "}"
        "function onMove(e){"
          "if(!dragging)return;"
          "e.preventDefault();"
          "var a=getAngle(e);"
          "curAng=a;"
          "setDial(type,a);"
          "if(isRest)moveLive(a);"
          "hapticTick(a,lastHaptic);lastHaptic=a;"
        "}"
        "function onUp(e){dragging=false;}"

        "wrap.addEventListener('pointerdown',onDown);"
        "window.addEventListener('pointermove',onMove);"
        "window.addEventListener('pointerup',onUp);"
        "window.addEventListener('pointercancel',onUp);"

        "var ctr=wrap.querySelector('.dial-center');"
        "if(ctr){"
          "ctr.addEventListener('pointerdown',function(e){e.stopPropagation();});"
          "ctr.addEventListener('mousedown',function(e){e.stopPropagation();});"
          "ctr.addEventListener('touchstart',function(e){e.stopPropagation();},{passive:true});"
        "}"

        "num.addEventListener('focus',function(){this.select();});"
        "var b=num.parentElement;"
        "if(b){"
          "b.addEventListener('pointerdown',function(e){e.stopPropagation();});"
          "b.addEventListener('click',function(e){num.focus();num.select();});"
        "}"

        "num.addEventListener('input',function(){"
          "var v=parseInt(this.value,10);"
          "if(!isNaN(v)){"
            "if(v>180)v=180;if(v<0)v=0;"
            "curAng=v;"
            "setDial(type,v);"
            "if(isRest)moveLive(v);"
            "hapticTick(v,lastHaptic);lastHaptic=v;"
          "}"
        "});"
        "num.addEventListener('change',function(){"
          "var v=parseInt(this.value,10);"
          "if(isNaN(v))v=90;if(v>180)v=180;if(v<0)v=0;"
          "this.value=v;curAng=v;setDial(type,v);"
          "if(isRest)moveLive(v);"
        "});"
      "}"

      // Initialize dials
      "setDial('rest',savedRest);"
      "setDial('press',savedPress);"
      "bindDial('rest',true);"
      "bindDial('press',false);"

      // Revert to saved metrics handler with smooth animation
      "var revAnim=null;"
      "function animateRevert(){"
        "if(revAnim)cancelAnimationFrame(revAnim);"
        "var sRest=parseInt(rNum.value,10);if(isNaN(sRest))sRest=90;"
        "var sPress=parseInt(pNum.value,10);if(isNaN(sPress))sPress=100;"
        "var sDur=parseInt(dRng.value,10)||savedDur;"
        "var dRest=savedRest-sRest;"
        "var dPress=savedPress-sPress;"
        "var dDur=savedDur-sDur;"
        "if(dRest===0&&dPress===0&&dDur===0){"
          "__showToast('Already at saved metrics');"
          "return;"
        "}"
        "if(navigator.vibrate){try{navigator.vibrate(15);}catch(ex){}}"
        "var start=performance.now();"
        "var durMs=400;"
        "function step(now){"
          "var p=Math.min(1,(now-start)/durMs);"
          "var ease=1-Math.pow(1-p,3);"
          "var cRest=Math.round(sRest+dRest*ease);"
          "var cPress=Math.round(sPress+dPress*ease);"
          "var cDur=Math.round(sDur+dDur*ease);"
          "setDial('rest',cRest);"
          "setDial('press',cPress);"
          "dNum.value=cDur;dRng.value=cDur;"
          "updateSliderFill(cDur);"
          "if(p<1){"
            "revAnim=requestAnimationFrame(step);"
          "}else{"
            "revAnim=null;"
            "setDial('rest',savedRest);"
            "setDial('press',savedPress);"
            "dNum.value=savedDur;dRng.value=savedDur;"
            "updateSliderFill(savedDur);"
            "moveLive(savedRest);"
            "__showToast('Reverted to saved metrics');"
          "}"
        "}"
        "revAnim=requestAnimationFrame(step);"
      "}"
      "if(revertBtn){"
        "revertBtn.addEventListener('click',function(e){"
          "e.preventDefault();"
          "animateRevert();"
        "});"
      "}"

      // Press Duration Sync & Haptics
      "dRng.addEventListener('input',function(){"
        "dNum.value=this.value;"
        "updateSliderFill(this.value);"
        "hapticTick(this.value,lastDHaptic);"
        "lastDHaptic=this.value;"
      "});"
      "dNum.addEventListener('input',function(){"
        "var v=parseInt(this.value,10);"
        "if(!isNaN(v)){"
          "if(v>3000)v=3000;if(v<50)v=50;"
          "dRng.value=v;"
          "updateSliderFill(v);"
        "}"
      "});"
      "dNum.addEventListener('focus',function(){this.select();});"
      "dNum.addEventListener('change',function(){"
        "var v=parseInt(this.value,10);"
        "if(isNaN(v)||v<50)v=50;if(v>3000)v=3000;"
        "this.value=v;dRng.value=v;"
        "updateSliderFill(v);"
      "});"

      // Hold-to-press live logic
      "var isHolding=false;"
      "function onHoldStart(e){"
        "e.preventDefault();"
        "if(isHolding)return;"
        "isHolding=true;"
        "holdBtn.classList.add('holding');"
        "if(navigator.vibrate){try{navigator.vibrate(20);}catch(ex){}}"
        "var p=pNum.value;"
        "var r=rNum.value;"
        "fetch('/api/calibrate/hold?state=1&press='+p+'&rest='+r,{method:'POST'}).catch(function(){});"
      "}"
      "function onHoldEnd(e){"
        "if(!isHolding)return;"
        "isHolding=false;"
        "holdBtn.classList.remove('holding');"
        "if(navigator.vibrate){try{navigator.vibrate(12);}catch(ex){}}"
        "var r=rNum.value;"
        "fetch('/api/calibrate/hold?state=0&rest='+r,{method:'POST'}).catch(function(){});"
      "}"
      "holdBtn.addEventListener('pointerdown',onHoldStart);"
      "window.addEventListener('pointerup',onHoldEnd);"
      "window.addEventListener('pointercancel',onHoldEnd);"

      // Test tap button
      "testBtn.addEventListener('click',function(e){"
        "e.preventDefault();"
        "testBtn.disabled=true;"
        "if(navigator.vibrate){try{navigator.vibrate([25,20,25]);}catch(ex){}}"
        "__showToast('Testing tap...');"
        "var r=rNum.value;"
        "var p=pNum.value;"
        "var d=dNum.value||dRng.value;"
        "fetch('/api/calibrate/test?rest='+r+'&press='+p+'&dur='+d,{method:'POST'})"
        ".then(function(res){return res.text();})"
        ".then(function(t){__showToast('Tap test complete');})"
        ".catch(function(){__showToast('Test failed');})"
        ".finally(function(){testBtn.disabled=false;});"
      "});"

      // Save calibration button
      "saveBtn.addEventListener('click',function(e){"
        "e.preventDefault();"
        "saveBtn.disabled=true;"
        "if(navigator.vibrate){try{navigator.vibrate([30,30,50]);}catch(ex){}}"
        "__showToast('Saving...');"
        "var r=rNum.value;"
        "var p=pNum.value;"
        "var d=dNum.value||dRng.value;"
        "fetch('/api/calibrate/save?rest='+r+'&press='+p+'&dur='+d,{method:'POST'})"
        ".then(function(res){if(!res.ok)throw new Error('fail');return res.text();})"
        ".then(function(t){"
          "hasSaved=true;"
          "__showToast('Calibration Saved!');"
          "setTimeout(function(){window.location.href='/main';},1000);"
        "})"
        ".catch(function(){__showToast('Save failed');saveBtn.disabled=false;});"
      "});"

      // Reset calibration button
      "resetBtn.addEventListener('click',function(e){"
        "e.preventDefault();"
        "if(!confirm('Clear all calibration data and reset to factory defaults?'))return;"
        "resetBtn.disabled=true;"
        "if(navigator.vibrate){try{navigator.vibrate([20,40,20]);}catch(ex){}}"
        "__showToast('Resetting...');"
        "fetch('/api/calibrate/reset',{method:'POST'})"
        ".then(function(res){return res.text();})"
        ".then(function(t){"
          "hasSaved=true;"
          "__showToast('Calibration Reset');"
          "setTimeout(function(){window.location.reload();},1000);"
        "})"
        ".catch(function(){__showToast('Reset failed');resetBtn.disabled=false;});"
      "});"
    "}"
    "if(document.readyState==='loading'){document.addEventListener('DOMContentLoaded',initCal);}"
    "else{initCal();}"
    "})();"
    "</script>"
  );

  calScript.replace("{{REST}}", String(curRest));
  calScript.replace("{{PRESS}}", String(curPress));
  calScript.replace("{{DUR}}", String(curDur));

  sendWrappedPage(server, "Servo Calibration", "&#127919;", body.c_str(), calScript.c_str());
}

/**
 * @brief Register HTTP route handlers for calibration
 */
inline void registerCalibrationRoutes(WebServer &server) {
  // GET /calibrate -> Serves web page or cURL bash script
  server.on("/calibrate", HTTP_GET, [&server]() {
    server.sendHeader("Connection", "close");
    if (server.header("User-Agent").indexOf("curl") >= 0) {
      String host = server.header("Host");
      if (host.length() == 0) host = "esp32.local";
      String script = generateCurlCalibrateScript(host);
      server.send(200, "text/plain; charset=utf-8", script);
    } else {
      sendCalibratePage(server, restAngle, pressAngle, pressDurationMs, isCalibrated);
    }
  });

  // POST /api/calibrate/move?angle=X
  server.on("/api/calibrate/move", HTTP_POST, [&server]() {
    int angle = server.hasArg("angle") ? server.arg("angle").toInt() : restAngle;
    if (angle >= 0 && angle <= 180) {
      myservo.attach(servoPin, 500, 2400);
      myservo.write(angle);
      delay(80);
      myservo.detach();
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "Moved");
    } else {
      server.sendHeader("Connection", "close");
      server.send(400, "text/plain", "Invalid angle");
    }
  });

  // POST /api/calibrate/hold?state=1|0&press=X&rest=Y
  server.on("/api/calibrate/hold", HTTP_POST, [&server]() {
    int state = server.hasArg("state") ? server.arg("state").toInt() : 0;
    int p = server.hasArg("press") ? server.arg("press").toInt() : pressAngle;
    int r = server.hasArg("rest") ? server.arg("rest").toInt() : restAngle;
    p = constrain(p, 0, 180);
    r = constrain(r, 0, 180);

    if (state == 1) {
      isHoldActive = true;
      holdStartTimeMs = millis();
      myservo.attach(servoPin, 500, 2400);
      myservo.write(p);
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "Holding");
    } else {
      isHoldActive = false;
      myservo.write(r);
      int travelDelay = max(60, abs(p - r) * 3);
      delay(travelDelay);
      myservo.detach();
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "Released");
    }
  });

  // POST /api/calibrate/test?rest=X&press=Y&dur=Z
  server.on("/api/calibrate/test", HTTP_POST, [&server]() {
    int r = server.hasArg("rest") ? server.arg("rest").toInt() : restAngle;
    int p = server.hasArg("press") ? server.arg("press").toInt() : pressAngle;
    int d = server.hasArg("dur") ? server.arg("dur").toInt() : (server.hasArg("duration") ? server.arg("duration").toInt() : pressDurationMs);
    r = constrain(r, 0, 180);
    p = constrain(p, 0, 180);
    d = constrain(d, 50, 5000);

    testRestAngle = r;
    testPressAngle = p;
    testDurationMs = d;
    pendingTestTap = true;
    if (loopTaskHandle != nullptr) {
      xTaskNotifyGive(loopTaskHandle);
    }

    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "Tap queued");
  });

  // POST /api/calibrate/save?rest=X&press=Y&dur=Z
  server.on("/api/calibrate/save", HTTP_POST, [&server]() {
    int r = server.hasArg("rest") ? server.arg("rest").toInt() : restAngle;
    int p = server.hasArg("press") ? server.arg("press").toInt() : pressAngle;
    int d = server.hasArg("dur") ? server.arg("dur").toInt() : (server.hasArg("duration") ? server.arg("duration").toInt() : pressDurationMs);

    if (saveCalibration(r, p, d)) {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", "Saved");
    } else {
      server.sendHeader("Connection", "close");
      server.send(400, "text/plain", "Invalid parameters");
    }
  });

  // POST /api/calibrate/reset
  server.on("/api/calibrate/reset", HTTP_POST, [&server]() {
    resetCalibration();
    server.sendHeader("Connection", "close");
    server.send(200, "text/plain", "Reset complete");
  });
}
