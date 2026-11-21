#pragma once

// ESP32-C3 Mini/SuperMini, 4 MB flash. GPIO18/19 stay free for native USB.
// MAX98357A: DIN=3, BCLK=4, LRC=5. OLED: SDA=6, SCL=7.
#if defined(CONFIG_IDF_TARGET_ESP32C3)
#if defined(LUCY_CLIPBOT_C3)
#define LUCY_BOARD_NAME "Clipbot ESP32-C3 Super Mini"
#define LUCY_SDA 21
#define LUCY_SCL 20
#else
#define LUCY_BOARD_NAME "ESP32-C3 Mini"
#define LUCY_SDA 6
#define LUCY_SCL 7
#endif
#define LUCY_BUTTON 10
#define LUCY_BUTTON_UP -1
#define LUCY_BUTTON_BACK -1
#define LUCY_SD_ENABLED 0
#define LUCY_I2S_DIN 3
#define LUCY_I2S_BCLK 4
#define LUCY_I2S_LRC 5
#else
#define LUCY_BOARD_NAME "ESP32 DevKit"
#define LUCY_SDA 21
#define LUCY_SCL 22
#define LUCY_BUTTON 17
#define LUCY_BUTTON_UP 16
#define LUCY_BUTTON_BACK 4
#define LUCY_SD_ENABLED 1
// Leave classic-board audio disabled until an amplifier is wired.
#define LUCY_I2S_DIN -1
#define LUCY_I2S_BCLK -1
#define LUCY_I2S_LRC -1
#endif

// No invented battery readings: enable only after checking the sense circuit.
// For a 1-cell Li-ion + 100k/100k divider: pin 0, divider 2000 (2.000x).
#ifndef LUCY_BATTERY_PIN
#define LUCY_BATTERY_PIN -1
#endif
#ifndef LUCY_BATTERY_DIVIDER_MILLI
#define LUCY_BATTERY_DIVIDER_MILLI 2000
#endif
#define LUCY_VERSION "0.3.0"
