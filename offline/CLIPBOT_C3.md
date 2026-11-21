# Lucy Cat OS for Clipbot ESP32-C3 Super Mini

This target preserves the original Lucy Cat OS renderer (24 behaviours x 5 variants = 120 clips) and uses the hardware configuration already tested on Clipbot.

| Device | Pin |
| --- | --- |
| SH1106 OLED SDA | GPIO21 |
| SH1106 OLED SCL | GPIO20 |
| MAX98357A DIN | GPIO3 |
| MAX98357A BCLK | GPIO4 |
| MAX98357A LRC/WS | GPIO5 |
| OLED VCC | 3.3 V |
| Amplifier VIN | 5 V (adequately rated supply) |
| Grounds | Common |

The existing Lucy volume setting is a percentage, **not** the 10000 PCM amplitude used by the earlier 1 kHz speaker test. This build does not promise safe continuous power for an 8-ohm 0.5 W speaker. Start at low volume. Lucy is silent during Wi-Fi setup and plays its greeting only after station Wi-Fi connects. The Lucy-Setup access point remains available according to Lucy settings.

## Build

From `offline/` run:

```sh
pio run -e lucy_clipbot_c3
```

GitHub Actions publishes `lucy-clipbot-c3-app.bin` as a build artifact when the build job succeeds. Build and hardware validation are required before calling it compatible.

## OTA caveat — important

This environment selects the dual-app-slot `min_spiffs.csv` layout rather than `huge_app.csv`, but **a web firmware upload cannot replace an ESP32 partition table**. Your already-installed Web OTA updater must have a suitable OTA layout, and its **actual** OTA slot must be large enough for the compiled app. The installed bootloader, partition table and OTA updater must agree. An app image built under this configuration is not automatically compatible with the updater's current partition table.

Check the current partition map on the running device before uploading. If the current device has incompatible partitions, stop: a standard application Web OTA update cannot correct the partition table. Never send `*-usb.bin`, `bootloader.bin`, or `partitions.bin` to the Web OTA app endpoint.

The first target is to display Lucy's real cat animations on SH1106. The INMP441 microphone is not yet integrated.

## Automatic cat sounds and chat response

On the Clipbot build Lucy automatically cycles through the original 120 procedural
animations. Once initial networking is ready, she also performs occasional
low-duty-cycle synthesized meows, chirps, purrs, and chatter, synchronized with
matching animations on the Home screen (roughly every 9–17 seconds). If a
Wi-Fi station is configured, cat audio remains muted until it connects; in
AP-only mode, no station connection is needed. A configured station disconnect
also temporarily mutes further new sounds.

Text sent through the built-in `/say` web interface or USB Serial console
opens the Companion screen with Lucy's reply and triggers a short cat sound.
This is **not** speech synthesis: the response is displayed as text, and the
MAX98357A plays a cat vocalization. Lucy cannot yet listen to the INMP441
microphone or pronounce arbitrary responses. Use Lucy's existing Speaker Volume
setting to mute or adjust the output. Fresh devices default to volume 35/100;
saved user volume settings are retained.

For the 8-ohm, 0.5 W speaker, monitor for distortion or heating. Volume is
a firmware relative control, not a verified physical loudness/power limit.

## Spoken reply integration (optional)

For accurate general answers, configure a reachable **AI model server** in Lucy Settings. The
on-device offline intent model is intentionally small and cannot answer arbitrary questions.
A missing or unreachable AI server causes offline fallback. A TTS server changes the sound
of a reply but does not improve factual accuracy.

This Clipbot branch can stream speech to the ESP32 MAX98357A:
- Run `python -m pip install edge-tts` on a local Windows PC, and install FFmpeg on PATH.
- Run `python tools/clipbot_tts_server.py` on that PC. Internet access is required for Edge TTS.
- Connect Clipbot and the PC to the same trusted Wi-Fi network.
- On Lucy's `http://192.168.4.1/settings` page, enable **Speak text replies**,
  and set the TTS server URL to `http://PC-LAN-IP:8765`.
- Configure the separate Lucy AI model endpoint if you want general-question answering.
- Send a chat message through the web console or serial. Text stays on the OLED,
  and the ESP requests 16 kHz signed-16-bit mono PCM speech via HTTP and plays it
  on its I2S amplifier. The server must return a Content-Length.
- A TTS failure leaves the reply visible but cannot provide spoken audio.
  Check Serial for diagnostics. The server binds all interfaces by default;
  only run on a trusted LAN and block untrusted access in your PC firewall.

The speech server is **not installed on ESP32**. It must remain running for
spoken replies. Arbitrary English/Marathi/Hindi pronunciation depends on
the chosen TTS voice; the default is English. The ESP does not run a neural
speech synthesizer locally. Listening through INMP441 is a separate feature.

For an 8-ohm 0.5 W speaker start at low volume: TTS samples are digitally
attenuated by `volume / 200`. The actual acoustic/electrical power remains
unverified.

## PC-independent operating modes

1. **Offline:** On loss of Wi-Fi or internet, Lucy keeps rendering the cat and
   running the built-in keyword/intent chat. It still produces synthesized cat
   noises without connecting to a PC, using local audio. It cannot reliably
   answer general questions or speak arbitrary sentences with no network.
2. **Wi-Fi + cloud:** Configure a reachable HTTPS Ollama-compatible chat gateway
   using Lucy's existing AI model settings, plus a separate HTTPS PCM TTS gateway
   (Clipbot Speech settings). These gateways may run on a VPS/cloud host and do
   not require a home PC to remain powered on. They are not provisioned by the
   firmware. Their availability, cost and correctness depend on the provider.
3. **Local PC:** Point the same AI and TTS settings at a local computer while
   it is available. Settings are editable through Lucy's Wi-Fi web interface
   and are persisted in NVS. Switch back to cloud URLs when leaving that LAN.

### Remote voice gateway (no home PC)

Run `tools/clipbot_tts_server.py` on a trusted Linux VPS with Python,
FFmpeg and `edge-tts`, set the `CLIPBOT_TTS_TOKEN` environment variable
to a long random secret, and place Caddy or another correctly configured
reverse proxy in front of localhost:8765 to provide HTTPS and a certificate
that chains to ISRG Root X1. Enter the public `https://...` endpoint and
the token in Lucy's web Settings. The firmware validates the TLS certificate;
it does not disable certificate checks. NTP synchronization is required before
HTTPS requests. Keep the cloud server updated; avoid exposing port 8765.

The `/tts?text=` URL includes the reply in the HTTPS request URI. The token
is sent as `X-Clipbot-Token`. Be mindful of URL logging and provider privacy.
The current service obtains synthesized audio from an internet TTS provider;
therefore it is not an offline-only solution.

**No permanent remote chat-control channel is included.** The web console
remains a LAN/Clipbot-AP interface. Reaching the device from another network
requires a separate authenticated cloud-relay design. Do not forward ESP32
port 80 to the internet.

### MicroDuck architecture reference

Pollen Robotics/Hugging Face's MicroDuck runs on a Rockchip RK3566 Linux board,
not ESP32-C3. Its updater, control daemons, speech and remote-access designs
are architectural references only. No MicroDuck binary or servo/ONNX Linux
runtime has been merged here.


## Persistent Web OTA
Clipbot now includes an authenticated firmware update page at
http://192.168.4.1/ota while connected to its Lucy-Setup hotspot,
or http://DEVICE-LAN-IP/ota while connected to its local network.
The username is clipbot. A unique random password is generated once,
saved in NVS, and shown on the OLED for 9.5 seconds during boot.
Never expose the HTTP admin page on the public internet.

The running firmware must have a valid second OTA app slot and the new
application image must fit that slot. An application OTA cannot change
the partition table. For future Web OTA updates, always select the
lucy-clipbot-c3-app.bin artifact, not the original lucy-cat-c3-mini
or a merged USB image. The updater is included in each Clipbot build.
Do not interrupt power during an upload.

## OpenCode bridge online brain (Clipbot-only)

The `lucy_clipbot_c3` target now uses only the
`vikasdocker/opencode-with-lucky/bridge/server.js` protocol for ONLINE chat.
Its POST `/chat` request contains a `messages` array with user/assistant
turns, sends `X-Bridge-Token` from the stored Lucy AI token, and reads the
response's `reply` field. GET `/health` is used by the serial AI diagnostics.

**The bridge chooses the model.** The Clipbot firmware does not send a model
name and does not directly call Ollama or any second online AI endpoint.
The bridge may itself switch between primary and fallback models according
to its own `config.json`; that is server-side behavior.

The default URL follows the source repository documentation:
`https://chat.blueflutex.in`. It is not verified to be active or accessible
to you. The bridge must actually be running. Get its correct URL and token
from the server operator, never store the token in GitHub or chat.

Connect to `Lucy-Setup`, open `http://192.168.4.1/settings`,
enable Online AI brain, set the OpenCode bridge base URL and matching token,
then save. The existing `AI` command checks `/health` (which may be public);
test a chat request too to confirm authentication. Without a reachable bridge
Lucy retains her limited offline keyword brain and cat animations.

The existing `/mcp` endpoint remains unchanged, with read-only
`lucy_status` and `lucy_animations` tools. MCP is separate from AI
inference; this patch does not make OpenCode itself an MCP endpoint.
Speech output still requires the separately configured TTS gateway.
The original bridge normally runs on a PC with OpenCode; deploying the bridge
on an always-on server is necessary to remove that dependency.

## Zero-config Worker target (current default)

This supersedes the historical OpenCode bridge setup above. The Clipbot SH1106 build automatically connects to `https://sparkling-fog-fdc8.vikasshu7.workers.dev` after Wi-Fi setup, with `gpt-4o-mini` configured on the Worker. There are no ordinary-user AI/MCP settings. Serial AI commands remain for debugging; TLS verification cannot be disabled.

Cloud requests run in a background task. The main loop keeps serving buttons, animations, web requests and OTA. Lucy gives a local fallback immediately, then updates the conversation when the cloud reply arrives. Failed requests retry on later conversation turns after a 30-second backoff. No PC, Ollama or local gateway is required for chat. Optional historical TTS integrations remain separate from this cloud chat path.

Both firmware variants need a private `LUCY_DEVICE_TOKEN` header generated with `node tools/write_device_token.js`; the same independent token must exist as a Worker secret. Public CI builds omit it and cannot authenticate online. Never publish private token-bearing images or embed an OpenAI key. See [the complete guide](../worker/ZERO_CONFIG.md) for provisioning, build commands, rollout blockers, security limits and physical acceptance tests.

SH1106 SDA/SCL remain GPIO21/20; MAX98357A BCLK/LRC/DIN remain GPIO4/5/3. Clipbot retains two OTA slots using `min_spiffs.csv`. Verify the currently installed partition table and available slot before any OTA, and obtain explicit approval before flashing or deploying production Worker changes.
