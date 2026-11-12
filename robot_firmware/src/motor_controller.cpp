#include "motor_controller.h"
#include "robot_config.h"

namespace {
int clampPwm(int value) {
  return constrain(value, -255, 255);
}
}

void MotorController::begin() {
  pinMode(LucyConfig::MOTOR_A_IN1, OUTPUT);
  pinMode(LucyConfig::MOTOR_A_IN2, OUTPUT);
  pinMode(LucyConfig::MOTOR_A_PWM, OUTPUT);
  pinMode(LucyConfig::MOTOR_B_IN1, OUTPUT);
  pinMode(LucyConfig::MOTOR_B_IN2, OUTPUT);
  pinMode(LucyConfig::MOTOR_B_PWM, OUTPUT);
  pinMode(LucyConfig::MOTOR_STBY, OUTPUT);

  digitalWrite(LucyConfig::MOTOR_STBY, LOW);
  analogWrite(LucyConfig::MOTOR_A_PWM, 0);
  analogWrite(LucyConfig::MOTOR_B_PWM, 0);
  stop();
}

void MotorController::setMotor(uint8_t in1, uint8_t in2, uint8_t pwmPin, int speed) {
  speed = clampPwm(speed);
  digitalWrite(in1, speed > 0 ? HIGH : LOW);
  digitalWrite(in2, speed < 0 ? HIGH : LOW);
  analogWrite(pwmPin, abs(speed));
}

void MotorController::drive(int leftPwm, int rightPwm) {
  if (!enabled_) {
    enabled_ = true;
    digitalWrite(LucyConfig::MOTOR_STBY, HIGH);
  }
  setMotor(LucyConfig::MOTOR_A_IN1, LucyConfig::MOTOR_A_IN2,
           LucyConfig::MOTOR_A_PWM, leftPwm);
  setMotor(LucyConfig::MOTOR_B_IN1, LucyConfig::MOTOR_B_IN2,
           LucyConfig::MOTOR_B_PWM, rightPwm);
  lastCommandMs_ = millis();
}

void MotorController::stop() {
  analogWrite(LucyConfig::MOTOR_A_PWM, 0);
  analogWrite(LucyConfig::MOTOR_B_PWM, 0);
  digitalWrite(LucyConfig::MOTOR_A_IN1, LOW);
  digitalWrite(LucyConfig::MOTOR_A_IN2, LOW);
  digitalWrite(LucyConfig::MOTOR_B_IN1, LOW);
  digitalWrite(LucyConfig::MOTOR_B_IN2, LOW);
  digitalWrite(LucyConfig::MOTOR_STBY, LOW);
  enabled_ = false;
}

void MotorController::tick() {
  if (enabled_ && millis() - lastCommandMs_ > LucyConfig::COMMAND_TIMEOUT_MS) {
    stop();
  }
}
