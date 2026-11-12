# Lucy AI — Wheeled Robot Architecture

Lucy is a Microduck-inspired wheeled companion built around the existing classic ESP32 and working Lucy Cloudflare Worker/OpenAI chat backend.

## Design principle

Reuse Microduck's modular robotics ideas: separated control, perception, media, configuration and health responsibilities. Do not attempt to run its Linux/RK3566-specific binaries or services on the ESP32. The ESP32 is a real-time controller, not a Linux SBC.

## System boundaries

- Cloud brain: existing Cloudflare Worker + OpenAI API for natural-language conversation and high-level intent.
- ESP32 controller: motor outputs, sensor sampling, local state, watchdogs and safety decisions.
- Motor driver: TB6612FNG dual H-bridge, subject to motor voltage/current compatibility.
- Perception: local distance sensors first; optional separate ESP32-CAM later.
- Voice: I2S microphone and I2S amplifier are later tasks; speech recognition/TTS can initially use cloud services.
- Display: SSD1306 OLED face and status animations, later milestone.

## Firmware modules

- core: scheduler, event queue, robot state and configuration.
- robot: differential drive, motor abstraction, encoders and odometry.
- sensors: distance and battery sensors.
- ai: authenticated cloud client, conversation and intent parsing.
- voice: microphone capture and speaker playback.
- display: OLED renderer and expressions.
- connectivity: Wi-Fi, diagnostics and OTA.
- safety: emergency stop, command timeout, watchdog and fault manager.

The initial robot_firmware project is intentionally isolated from the existing src/main.cpp chatbot firmware. This prevents unverified robot hardware experiments from breaking the working ESP32 chatbot.

## Command contract

Future high-level command envelope:

    {
      "command": "robot.move",
      "linear": 0.12,
      "angular": 0.0,
      "duration_ms": 400,
      "request_id": "unique-id"
    }

The ESP32 must validate every field and translate the request to bounded wheel speeds. Cloud/AI messages must never directly set GPIO pins.

Future commands: robot.move, robot.turn, robot.stop, robot.look, robot.speak, robot.face, robot.health, robot.mode.

## Safety invariants

1. Motor driver standby is LOW at boot.
2. Invalid, stale or expired movement commands stop the motors.
3. Local command timeout stops motors even if Wi-Fi/cloud is unavailable.
4. A physical emergency stop disables motor-driver power independently of AI.
5. Obstacle detection and battery faults override all motion requests.
6. Movement remains disabled after reboot until explicit local enable/arm.
7. Test with wheels lifted and current-limited power before floor testing.

## Network security

Do not expose a public unauthenticated movement endpoint. Add device authentication, command sequence numbers, replay protection, rate limits and a local stop path before enabling remote movement. CORS is not authentication.

## Hardware assumptions

The first scaffold assumes a classic ESP32 DevKit V1, two brushed DC gear motors with encoders, and a TB6612FNG. Pin assignments in robot_firmware/include/robot_config.h are provisional and must be verified against the exact board and wiring.
