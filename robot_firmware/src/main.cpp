#include <Arduino.h>
#include "motor_controller.h"
#include "robot_config.h"

MotorController motors;

static void printHelp() {
  Serial.println();
  Serial.println("Lucy Wheeled Robot - bench test console");
  Serial.println("Commands: w=forward, s=back, a=left, d=right, x=STOP");
  Serial.println("WARNING: Raise wheels off the floor before testing.");
  Serial.println("Each movement command expires automatically.");
}

void setup() {
  Serial.begin(LucyConfig::SERIAL_BAUD);
  delay(250);
  motors.begin();
  Serial.println("Lucy robot controller booted. Motors are in standby.");
  printHelp();
}

void loop() {
  motors.tick();
  if (!Serial.available()) {
    delay(5);
    return;
  }

  const char command = static_cast<char>(Serial.read());
  const int speed = LucyConfig::DEFAULT_PWM;

  switch (command) {
    case 'w': case 'W':
      motors.drive(speed, speed);
      Serial.println("Forward command accepted");
      break;
    case 's': case 'S':
      motors.drive(-speed, -speed);
      Serial.println("Reverse command accepted");
      break;
    case 'a': case 'A':
      motors.drive(-speed, speed);
      Serial.println("Left turn command accepted");
      break;
    case 'd': case 'D':
      motors.drive(speed, -speed);
      Serial.println("Right turn command accepted");
      break;
    case 'x': case 'X': case ' ':
      motors.stop();
      Serial.println("STOP");
      break;
    case '\r': case '\n':
      break;
    default:
      Serial.println("Unknown command. Use w/s/a/d/x.");
      break;
  }
}
