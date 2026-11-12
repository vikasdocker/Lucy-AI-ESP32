# Lucy AI Wheeled Robot — Hardware Plan

## Initial prototype BOM

| Subsystem | Proposed part | Notes |
|---|---|---|
| Controller | Existing classic ESP32 DevKit V1 / ESP32-WROOM-32 | Confirm exact board |
| Chassis | 2WD differential-drive chassis + caster | Battery low and centered |
| Motors | 2 x DC geared motors with quadrature encoders | Match voltage and stall current |
| Motor driver | TB6612FNG | Verify continuous/peak current |
| Obstacle sensor | VL53L1X ToF (or VL53L0X) | Front-facing initially |
| Display | SSD1306 128x64 I2C OLED | 3.3 V logic-compatible |
| Microphone | INMP441 I2S | Later milestone |
| Audio output | MAX98357A I2S amplifier + compatible speaker | Later milestone |
| Camera | Separate ESP32-CAM module | Optional |
| Battery | Protected 2-cell Li-ion pack with suitable BMS | Select after motor measurements |
| Logic regulator | Regulated 5 V buck, sized for ESP32/audio peaks | Separate motor rail |
| Safety | Physical latching motor disconnect / E-stop | Independent motor energy cutoff |

## Power architecture

- Battery to fuse to motor driver VM, sized for motor stall current.
- Battery to regulated logic buck to ESP32 VIN/5V input only if the board input specification permits.
- Signal grounds share a common reference; keep motor-current return paths away from sensitive microphone/sensor ground where practical.
- Never power motors from ESP32 3V3.
- Never connect a 2S battery directly to ESP32 3V3.
- Check buck current/thermal rating, BMS discharge rating, fuse, wire gauge and motor stall current before assembly.
- Add local bulk capacitance near the motor driver and decoupling near logic modules.

## Provisional TB6612FNG pin map

| Signal | ESP32 GPIO |
|---|---:|
| AIN1 | 26 |
| AIN2 | 27 |
| PWMA | 25 |
| BIN1 | 32 |
| BIN2 | 33 |
| PWMB | 14 |
| STBY | 13 |

This map is a starting point only. Check conflicts with the exact board, boot strapping and peripherals. Confirm motor direction on a lifted chassis at low PWM.

## Bring-up order

1. Power ESP32 from USB only; verify serial boot.
2. Connect driver logic ground and logic supply with motor supply disconnected; verify STBY stays LOW.
3. Verify GPIO mapping.
4. Connect one motor with wheels lifted and current-limited supply.
5. Verify forward/reverse polarity at low PWM.
6. Repeat for second motor.
7. Verify timeout stop and physical E-stop.
8. Add encoders, ToF, OLED, battery sensing, then voice/camera.

Do not install this scaffold on the currently working chatbot device until hardware is assembled and tested. This is a separate PlatformIO project and does not replace existing firmware.
