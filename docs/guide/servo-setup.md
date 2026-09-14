# Servo Horn Setup & Centering

Before attempting to actuate any buttons, mounting the servo arm (horn) correctly is critical. Standard micro-servos (SG90 / MG90S) physically sweep across a 180° arc. Aligning the horn properly ensures maximum torque and travel in the desired direction.

---

> [!CAUTION]
> **Do not attach or screw down the servo horn before powering on the ESP32!**
> If you attach the horn randomly and the motor initializes, it may immediately hit its internal physical hard-stop, stripping the nylon/brass gears or overheating the H-bridge.

---

## 3-Step Mounting Procedure

```
    STEP 1: Power On              STEP 2: Align Splines             STEP 3: Tighten Safely
   ┌─────────────────┐             ┌─────────────────┐             ┌─────────────────┐
   │ Center to 90°   │             │ Press Horn at   │             │ Hold Horn With  │
   │ Motor shaft     │   ======>   │ 90° onto teeth  │   ======>   │ Fingers to Kill │
   │ locks at center │             │ pointing out    │             │ Screwdriver Drag│
   └─────────────────┘             └─────────────────┘             └─────────────────┘
```

### Step 1: Center the Motor (90°)
1. Flash and power on the ESP32 with the servo motor connected to **GPIO 1**, **5V**, and **GND**.
2. Open the calibration tool:
   - **Web Browser:** Visit `http://192.168.1.50/calibrate`
   - **Terminal CLI:** Run `bash <(curl -s http://192.168.1.50/calibrate)`
3. On an uncalibrated device, the firmware commands the servo shaft to **`90°`** (the exact mechanical center).
4. You will feel the motor briefly engage and lock at the central position.

### Step 2: Press Horn onto Splines (at 90°)
1. While the shaft is held at 90°, take the plastic/aluminum single-arm horn and gently press it onto the splined gear shaft with your fingers pointing roughly perpendicular to the servo casing (pointing toward your switch).
2. The teeth (splines) will interlock with the horn at that exact angle. This gives you roughly ~90° of clockwise travel and ~90° of counter-clockwise travel.

### Step 3: Power Off & Tighten Screw
1. **Unplug or power down the ESP32** before tightening the center retaining screw.
2. **Hold the horn firmly with your thumb and fingers** while tightening the screw with a jeweler's screwdriver.
3. *Why holding is crucial:* Holding the horn directly absorbs all screwdriver torque, preventing the internal gear teeth from taking twisting force when the motor is unpowered.
4. Power the ESP32 back on.

---

## Calibrating Angles

Once the horn is securely fastened, open the Web UI (`/calibrate`) or the Terminal CLI:

1. **Rest Angle:** Set the position where the arm hovers 1–2 mm just above the button without pressing it (idle state). The arm moves in real-time as you adjust the dial.
2. **Press Angle:** Adjust the dial so the horn presses down firmly on the switch. 
   > [!WARNING]
   > Make sure the horn isn't pushing down excessively hard. If you hear the servo buzzing or humming continuously, back off the angle by 2–4 degrees.
3. **Press Duration:** Set how many milliseconds (typically 200–400ms) the arm holds the switch down before returning to rest.
4. **Test Tap:** Tap the **Test Tap** button to execute a full press-and-return cycle.
5. **Save:** Tap **Save Calibration** to permanently store the calibrated values in NVS flash memory.
