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
- Tiny on-device intent model (int8, ~17 KB weights, 9 classes) for phrasing robustness — see below
- Self-learning word generator: an order-3 character Markov chain that invents new sentences and words as you chat — see below
- Optional local AI bridge: chat through a model running on your PC (Ollama over Lucy's own WiFi AP) with automatic fallback to the offline brain — see below
- Snake game
- Offline-first: no cloud API key, no MCP, no telemetry. This fork adds a WiFi softAP web console and an optional AI bridge. The bridge talks either to a model on your PC over Lucy's own AP, or — if you configure Wi-Fi — through a token-protected gateway you host (see below). Remove the Wi-Fi credentials and it is fully offline again.

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
- `make words` (or `invent a sentence`, `tell me something new`) — generate fresh text
- `brain stats` / `brain save` / `brain reset` — inspect, persist, or wipe the generator
- `AI` — show AI bridge status, endpoint, model and a live ping
- `AI on` / `AI off` — use the model for chat, or stay fully local (saved in NVS)
- `AI url <http(s)://...>` — point Lucy at the public gateway (saved)
- `AI host <ip> [port]` — point Lucy at a LAN Ollama server (saved)
- `AI token <secret>` — gateway token (saved, never echoed)
- `AI insecure on|off` — skip TLS verification (escape hatch)
- `AI model <name>` — change the model (saved)
- `WIFI <ssid> [password]` / `WIFI status` / `WIFI off` — station mode for internet chat (2.4 GHz)
- `LANG en|hi|mr` — set the local fallback language without buttons

The serial console currently exercises the local response engine. It is not a microphone speech recognizer.

## Tiny on-device intent model

Fork addition: a ~17 KB int8 model runs inside the firmware and classifies paraphrases the fixed keywords would miss ("good morning", "how is it going", "say something funny", "catch you later", "dhanyavaad", ...).

- Architecture: hashed char bigram+trigram features (256 bins) -> 64 ReLU -> 9 intents (greeting, how-are-you, bored, joke, time, name, thanks, bye, other)
- Weights: int8 in flash, float32 biases, int32 accumulation; no TFLite/MicroPython/runtime dependency — ~17k MACs per inference on-device
- Decision order: whole-word keywords first (fast, deterministic), then model if confidence >= 0.55, then the generic language replies
- Retrain: `python tools/train_intent.py` (needs NumPy) — edits the dataset there, regenerates `offline/src/intent_model.h`, then rebuild. Holdout accuracy ~99%, int8-simulated ~99.7% as of this dataset.

Firmware size impact: model weights 16,960 bytes int8 (~+19 KB flash total, 0 RAM beyond a small stack frame).

**Not an LLM.** This chip (classic ESP32, 520 KB SRAM, no PSRAM, 4 MB flash) cannot run a neural generative model — the smallest published ESP32 LLM demos need an ESP32-S3 with 8 MB PSRAM. This model maps phrasing to the existing local response pack; it does not generate text. The statistical generator below does generate text, but it is not a language model.

## Self-learning word generator

Fork addition: Lucy can now make up its own sentences and words with **no training phase and no neural network** — just a character Markov chain that grows as you talk to it.

- **How it works:** order-3 character Markov chain with order-2 backoff. It records which character followed the previous three characters across everything it has seen; generation walks those transitions, so it recombines real fragments into new sentences and sometimes new words ("stening", "laterned", "toget").
- **Seed:** ~130 short sentences in Lucy's voice are baked into flash and learned on first boot (1,866 order-3 transitions), so generation works before you type anything.
- **Live learning:** every serial message and every web-console message is learned immediately. There is no dataset, no labels, no training run — it learns from you.
- **Ask for words:** say `make words`, `invent a sentence`, `tell me something new`, or `make up something`; the web console chat box works the same way.
- **Memory:** 1024 buckets x 8 slots x 8 bytes = 65,536 bytes of static RAM (35% total). Inference is a few table lookups per character; generation takes milliseconds.
- **Persistence:** the raw table is stored in SPIFFS at `/lucy/brain.bin` (66 KB), autosaved 5 minutes after the last new message, on `brain save`, and on reset. A rewrite takes ~2-4 s because SPIFFS needs a retry; that is why saves are infrequent.
- **Commands:** `brain stats` (transitions, learned messages, generated count, filesystem usage), `brain save`, `brain reset` (back to the built-in seed only).

Honest limitations: order-3 character statistics produce word salad, not reasoning. Output is English-ish, sometimes multilingual because the seed includes romanized Hindi/Marathi, and meaning is incidental. It cannot answer questions or follow a topic. For real generative language you need an ESP32-S3 with PSRAM and a genuine tiny LLM.

Tuning knobs live at the top of `offline/src/markov.cpp`: `MARKOV_BUCKETS`, `MARKOV_SLOTS`, the `SEED_TEXT` corpus, the sentence-length probability in `walkChain()`, and `SAVE_INTERVAL_MS`. `tools/` has no trainer for this on purpose — there is nothing to train.

## AI bridge (PC-hosted model)

Lucy can borrow a real 7B model from your PC. The PC runs [Ollama](https://ollama.com) on the GPU; the firmware sends `POST /api/chat`, keeps a short rolling history, and falls back to the offline brain the moment the server is unreachable.

Two transports, one API:

| Mode | Endpoint | Works when |
|---|---|---|
| **Public** (recommended) | `https://lucy.blueflutex.in` (token gateway) | Lucy is on any Wi-Fi with internet |
| LAN | `http://<pc>:11434` (Ollama directly) | the PC has joined `Lucy-Setup` |

Measured on the development machine (i5-13450HX + RTX 3050 6 GB, `qwen2.5:7b` Q4):

| | |
|---|---|
| Model | qwen2.5:7b, 4.7 GB |
| VRAM | ~5.0 / 6.1 GB |
| Speed | **0.6 s** for a short reply once loaded; ~6 s cold load |
| Public round trip | **~1.7 s** including Cloudflare + TLS + the model |
| Quality | coherent multi-sentence chat, follows context for the last 3 turns |

### Where the public model lives

```
Lucy (any Wi-Fi)
   -> https://lucy.blueflutex.in        Cloudflare edge, real TLS certificate
   -> cloudflared tunnel (this PC)
   -> 127.0.0.1:8788                    gateway/server.js (token + rate limit)
   -> 127.0.0.1:11434                   Ollama, qwen2.5:7b, GPU
```

The gateway is not optional ceremony: **Ollama has no authentication**. Forwarding port 11434 would let anyone who scans your IP use your GPU. The gateway is the only public surface, it requires a token, and it can only forward to two Ollama endpoints (`/api/chat`, `/api/tags`).

### One-time setup

1. Generate the gateway token (48 random hex chars, written to the git-ignored `gateway/config.json`, never printed):
   ```powershell
   node gateway\setup.js
   ```
2. Start everything — Ollama, gateway, tunnel, and firewall hardening (elevated):
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\start_lucy_public.ps1
   ```
   It is idempotent: run it again after any reboot, or add it to Task Scheduler at logon.

   For a permanent setup that survives reboots (and needs no elevated window), register three logon tasks once:
   ```powershell
   powershell -ExecutionPolicy Bypass -File tools\install_lucy_tasks.ps1
   ```
   Tasks `Lucy Ollama`, `Lucy Gateway`, `Lucy Tunnel` start in the background on every logon, restart on failure, and do nothing if the ports are already taken. It also sets `OLLAMA_HOST=0.0.0.0:11434` in the user environment so the Ollama desktop app binds every interface. Remove them with `-Remove`.
3. Provision Lucy over USB. **This prompt asks for your Wi-Fi locally**, so the password and token never appear in chat or shell history:
   ```powershell
   python tools\provision_lucy_ai.py COM5
   ```
   It sends `WIFI <ssid> <password>`, `AI url https://lucy.blueflutex.in`, `AI token <secret>`, `AI insecure off`, `AI on`, waits for the join, pings the gateway and asks Lucy one test question.
4. Verify later with:
   ```powershell
   python tools\test_lucy_ai.py COM5
   ```

**2.4 GHz only.** The ESP32 has no 5 GHz radio. If your router publishes a combined SSID, make sure the 2.4 GHz band is enabled; a phone hotspot works too. Username/password networks (WPA2-Enterprise) are not supported.

On the development machine this bit us: the PC's only network, `LordOfThePings`, is 5 GHz-only — so Lucy cannot join it. Instead the PC runs a **2.4 GHz Windows Mobile Hotspot** (`ESP32-Chat`, created by `start-hotspot.ps1` from the other project) which shares the PC's 5 GHz uplink. Lucy joins the hotspot as `192.168.137.x`, traffic goes hotspot → PC → Cloudflare tunnel → gateway. The PC's internet stays up; nothing about the PC's own browsing is interrupted.

Watch the link with `WIFI status` (expected: `connected to 'ESP32-Chat'`). The ESP32 occasionally re-associates on the hotspot; the firmware prints `WiFi: connected as ...` when it does and auto-reconnects in the background, and during a drop `AI` simply falls back to the offline brain. Once back on the network, the next message uses the model again.

### LAN-only mode (no internet, no token)

Useful when the PC is briefly connected to `Lucy-Setup`:

```
AI host 192.168.4.2 11434
AI on
```

Settings persist in NVS. Check the active mode with `AI`.

### Behavior

- Only clock questions (`time`, `clock`), `make words`, and `brain` commands stay local — the model cannot read the RTC.
- The last 3 exchanges are sent as context, so follow-up questions work.
- If the server is off, unreachable, or slow past the timeout (15 s LAN / 45 s public), Lucy answers from the built-in keyword/intent/Markov brain. A 30 s circuit breaker keeps chat snappy.
- Replies are requested in under 40 words and romanized Latin letters, so the 128x64 OLED and serial console stay readable.
- The system prompt keeps Lucy honest: no internet access, browsing, or live data.
- The AP stays up in station mode (AP+STA), so `http://192.168.4.1` keeps working while Lucy is on your home Wi-Fi.

### Serial commands

| Command | Purpose |
|---|---|
| `AI` | status: endpoint, model, token length, TLS mode, Wi-Fi, live ping |
| `AI on` / `AI off` | use the model when reachable, or stay fully local |
| `AI url <http(s)://...>` | gateway or Ollama base URL |
| `AI host <ip> [port]` | shorthand for a LAN Ollama server |
| `AI token <secret>` | gateway token (hidden from output, stored in NVS) |
| `AI insecure on\|off` | disable certificate verification (escape hatch only) |
| `AI model <name>` | model name to request |
| `WIFI <ssid> [password]` | join a 2.4 GHz network, AP stays up |
| `WIFI status` / `WIFI off` | inspect or forget the station link |
| `LANG en\|hi\|mr` | fallback language |

### Security model

- **Only the gateway is public.** It binds `127.0.0.1:8788`, so the tunnel is the sole way in. Port 11434 is never forwarded and the firewall rule accepts private addresses only.
- **Token:** 48 hex chars, constant-time compare, accepted as `x-lucy-token` or `Authorization: Bearer`. It is never logged or printed.
- **TLS is verified**, not just encrypted: the firmware pins the ISRG Root X1/X2 anchors in `offline/src/tls_root_ca.h` (regenerate with `node tools/gen_root_ca.js`).
- **Limits:** 60 requests/minute per IP, 2 concurrent, 64 KB body cap, 20 messages, 4,000 chars each, `num_predict` clamped to 200, and the model is fixed server-side.
- `/health` is public but reveals only service name, uptime, model name, and busy count.
- **Residual risks:** a token embedded in firmware is readable from a stolen board (`node gateway/setup.js --rotate`, then re-provision). `Lucy-Setup` is an open AP, so anyone in range can open the web console and chat through the bridge. Use `WIFI off` when you do not need it, and keep the PC's own firewall on.

### Limits

- The PC is the brain: it must stay awake, online, and running Ollama + gateway + tunnel. No PC, no LLM.
- `qwen2.5:7b` is the sweet spot for 6 GB VRAM; larger models spill to CPU, 3B is faster but weaker.
- Cloudflare's certificate chain can change; if TLS starts failing after months, regenerate `tls_root_ca.h` (or use `AI insecure on` temporarily).
- No streaming: replies arrive as one JSON body, which is fine at 0.6 s but means no token-by-token output.

## Web console (no display needed)

This fork starts an open WiFi softAP on boot. Join the **`Lucy-Setup`** network from any phone/laptop and open **http://192.168.4.1**:

- Live mirror of the 128x64 screen on a canvas (framebuffer polled at ~4 Hz)
- **UP / SELECT / BACK** virtual buttons drive the real OS (menus, Snake, Settings…) exactly as if pressed physically
- Chat box talks to the same local `offlineReply()` brain as the serial console (`hello`, `how are you`, `joke`, `time`, `name`, `make words`). Messages typed here also teach the word generator. When the AI bridge is on and the PC is connected, replies come from the PC model.

Endpoints (plain HTTP, GET): `/` (page), `/frame.bin` (raw 1024-byte framebuffer), `/press?btn=up|select|back`, `/say?text=<urlencoded>` (plain-text reply).

Limitations, on purpose: the console has no authentication (the AP is open, so keep the setup in a trusted place), and there is no MCP/JSON-RPC. The web console can relay chat to the AI bridge when one is configured. If you want the AP password-protected, say so — one `WiFi.softAP("Lucy-Setup", "password")` line.

## RTC time

If the DS3231 has lost power, set its time once through the serial console using:

```
SETTIME 2026-11-13 17:30:00
```

Use the actual current local date/time when setting it. The firmware deliberately does not invent a time if the RTC battery is missing or the RTC reports lost power.

## Flash

Build artifact: `lucy-offline-esp32dev.bin`.

Use USB serial flashing for this first offline target. Do not OTA-flash it onto the legacy firmware until partition compatibility and recovery have been checked.
