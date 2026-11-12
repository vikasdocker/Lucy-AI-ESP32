#pragma once
#include <Arduino.h>

// Provisional classic ESP32 DevKit V1 + TB6612FNG pin map.
// Verify board and wiring before connecting motors. GPIO 34-39 are input-only.
namespace LucyConfig {
  constexpr uint8_t MOTOR_A_IN1 = 26;
  constexpr uint8_t MOTOR_A_IN2 = 27;
  constexpr uint8_t MOTOR_A_PWM = 25;
  constexpr uint8_t MOTOR_B_IN1 = 32;
  constexpr uint8_t MOTOR_B_IN2 = 33;
  constexpr uint8_t MOTOR_B_PWM = 14;
  constexpr uint8_t MOTOR_STBY = 13;
  constexpr uint8_t DEFAULT_PWM = 110; // 0..255; tune only after bench testing.
  constexpr uint32_t COMMAND_TIMEOUT_MS = 700;
  constexpr uint32_t SERIAL_BAUD = 115200;
}
