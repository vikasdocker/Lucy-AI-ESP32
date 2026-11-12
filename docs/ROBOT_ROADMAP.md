# Lucy AI Wheeled Robot — Development Roadmap

## Phase 0 — Repository and hardware baseline
- Keep existing Lucy chatbot/OTA firmware unchanged.
- Create isolated robot_firmware PlatformIO project.
- Confirm exact ESP32 module, chassis, motor ratings, driver and battery.
- Review provisional GPIO map and power budget.

Exit criteria: reproducible build and approved hardware pin/power map.

## Phase 1 — Differential-drive bring-up
- TB6612FNG driver integration.
- Bounded left/right wheel PWM.
- Serial bench commands.
- Boot in motor standby.
- Command timeout and stop behavior.
- Verify with wheels raised and current-limited power.

Exit criteria: reliable forward/reverse/turn/stop and timeout on both wheels.

## Phase 2 — Feedback and safety
- Quadrature encoder reading.
- Wheel speed estimation and closed-loop speed control.
- Front ToF obstacle detection.
- Physical emergency stop.
- Battery voltage/fault monitor.
- Fault latch and watchdog.

Exit criteria: local safety can stop motion without Wi-Fi or cloud.

## Phase 3 — Lucy cloud command integration
- Preserve current Cloudflare Worker chat API.
- Add a separately authenticated robot command endpoint/protocol.
- Convert natural-language intents into strict typed commands.
- Validate command ranges, duration, sequence and operating mode locally.
- Add manual mode and command audit logs.

Exit criteria: Lucy can request bounded movement; ESP32 independently approves/rejects it.

## Phase 4 — OLED personality
- SSD1306 driver and face renderer.
- Idle, listening, thinking, speaking, happy and fault expressions.
- Wi-Fi, battery and connection status.

Exit criteria: stable display updates without blocking control loop.

## Phase 5 — Voice
- I2S microphone capture.
- Voice activity detection / push-to-talk first.
- Speech-to-text and text-to-speech service integration.
- I2S speaker playback.
- Audio interruption and physical mute control.

Exit criteria: complete hands-free conversation with a physical mute option.

## Phase 6 — Camera
- Optional separate ESP32-CAM.
- Authenticated image capture/transfer.
- Vision-capable model integration.
- Camera privacy indicator and user-controlled capture.

Exit criteria: Lucy can describe an explicitly requested image without blocking motor control.

## Phase 7 — Companion autonomy
- Manual/companion/explore modes.
- Obstacle-aware low-speed roaming.
- Encoder-based odometry.
- Docking experiments only after reliable localization and battery monitoring.
- Regression tests for Wi-Fi loss, cloud timeout, sensor fault and low battery.

Exit criteria: controlled indoor movement with local fail-safe behavior.

## Release gates

No phase is complete solely because it compiles. Require bench tests, wiring/power review, fault injection and documented results. Never make AI/cloud availability a prerequisite for stopping motors.
