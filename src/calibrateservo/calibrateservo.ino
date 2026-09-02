#include <ESP32Servo.h>

const int servoPin = 1; 
Servo calibratorServo;

void setup() {
  Serial.begin(115200);
  
  // Initialize PWM timers for ESP32-S3
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  calibratorServo.setPeriodHertz(50);
  
  // Attach servo with standard pulse widths
  calibratorServo.attach(servoPin, 500, 2400);
  
  // Move to a safe neutral starting position
  calibratorServo.write(90);
  
  Serial.println("\n==========================================");
  Serial.println("   SERVO CALIBRATION TOOL INITIATED");
  Serial.println("==========================================");
  Serial.println("1. Ensure your Serial Monitor is set to 115200 baud.");
  Serial.println("2. Set line ending to 'Newline' or 'Both NL & CR'.");
  Serial.println("3. Type an angle between 0 and 180 and press Enter.\n");
  Serial.println("GOAL:");
  Serial.println(" - Find 'restAngle': Hovering just above the button.");
  Serial.println(" - Find 'pressAngle': Pushing the button fully, but NOT buzzing/stalling.");
  Serial.println("==========================================\n");
}

void loop() {
  if (Serial.available() > 0) {
    String input = Serial.readStringUntil('\n');
    input.trim(); 
    
    if (input.length() > 0) {
      int newAngle = input.toInt();
      
      // Basic validation
      if (newAngle == 0 && input != "0") {
        Serial.println("[!] Invalid input. Please enter a number.");
      } else if (newAngle >= 0 && newAngle <= 180) {
        calibratorServo.write(newAngle);
        Serial.print("[+] Moved to angle: ");
        Serial.println(newAngle);
      } else {
        Serial.println("[!] Out of bounds. Enter an angle between 0 and 180.");
      }
    }
  }
}