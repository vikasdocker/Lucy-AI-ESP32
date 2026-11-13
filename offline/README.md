# Lucy Offline OS — ESP32 DevKit V1

This is a separate offline firmware target for the existing Lucy-AI-ESP32 repository. It does not replace the current AP+STA/web/Cloudflare firmware.

## Current build: v0.1.0

Implemented:
- SSD1306 128x64 OLED home screen and application menu
- Three debounced physical buttons
- DS3231 RTC detection and clock screen
- MicroSD detection and boot log
- Settings persisted in ESP32 NVS
- English/Hindi/Marathi response selection (initial romanized response pack)
- Basic deterministic keyword/intent conversation engine via USB serial console
- Snake game
- No Wi-Fi, HTTP, Cloudflare Worker, API key, cloud service or LLM dependency

Not yet implemented:
- Microphone capture and real speech-to-text
- Speaker playback and multilingual TTS
- Full Devanagari font renderer
- Tic Tac Toe and Memory Match gameplay
- Live weather (requires external data); local sensor readings are a later hardware add-on

**Voice honesty:** Classic ESP32 DevKit V1 does not have enough resources for unrestricted Hindi/Marathi/English speech recognition. This v0.1 firmware is the offline UI/brain foundation; microphone and speaker hardware drivers plus a constrained command recognizer are the next milestone. The serial console is provided to exercise the conversation engine now.

## Build

From this folder:

```bash
python -m pip install platformio
pio run
```

Board: `esp32dev` / classic ESP32-WROOM-32.

## Wiring

| Module | ESP32 |
|---|---|
| SSD1306 SDA | GPIO 21 |
| SSD1306 SCL | GPIO 22 |
| DS3231 SDA/SCL | GPIO 21 / GPIO 22 (shared I2C) |
| UP button | GPIO 16 to GND |
| SELECT button | GPIO 17 to GND |
| BACK button | GPIO 4 to GND |
| MicroSD SCK | GPIO 18 |
| MicroSD MISO | GPIO 19 |
| MicroSD MOSI | GPIO 23 |
| MicroSD CS | GPIO 13 |
| MicroSD power | 3.3V-compatible module supply |

Buttons use internal pull-ups; connect each button between its GPIO and GND.

The OLED and DS3231 share the I2C bus. Confirm the OLED address (normally 0x3C) and use a 3.3V-compatible SD module. Do not use GPIO5 as SD chip-select because it is an ESP32 strapping pin.

## Controls

- Home: SELECT opens apps; UP opens Companion
- Menu: UP moves; SELECT opens; BACK returns
- Companion: UP cycles language; SELECT shows a sample response; BACK returns
- Games: UP selects; SELECT starts; BACK returns
- Snake: UP/SELECT turn; BACK exits; SELECT restarts after game over
- Settings: UP changes language; SELECT toggles the future voice-output setting; BACK returns

## Serial test console

Open serial monitor at 115200 baud and type one line:
- `hello`
- `how are you`
- `joke`
- `time`
- `name`

The serial console currently exercises the local response engine. It is not a microphone speech recognizer.

## RTC time

If the DS3231 has lost power, set its time once through the serial console using:

```
SETTIME 2026-11-13 17:30:00
```

Use the actual current local date/time when setting it. The firmware deliberately does not invent a time if the RTC battery is missing or the RTC reports lost power.

## Flash

Build artifact: `lucy-offline-esp32dev.bin`.

Use USB serial flashing for this first offline target. Do not OTA-flash it onto the legacy firmware until partition compatibility and recovery have been checked.
