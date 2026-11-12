#pragma once
#include <Arduino.h>

class MotorController {
public:
  void begin();
  void stop();
  void drive(int leftPwm, int rightPwm);
  void tick();
private:
  void setMotor(uint8_t in1, uint8_t in2, uint8_t pwmPin, int speed);
  uint32_t lastCommandMs_ = 0;
  bool enabled_ = false;
};
