# Zero-configuration Lucy cloud brain

Lucy connects over HTTPS directly to `https://sparkling-fog-fdc8.vikasshu7.workers.dev` using `gpt-4o-mini`. The PC, LAN gateway, Ollama and tunnels are unnecessary. Users enter only Wi-Fi credentials. OpenAI credentials belong exclusively in Cloudflare's `OPENAI_API_KEY` secret.

## Firmware and protocols

- Classic: the browser POSTs form-encoded `messages` (a JSON object containing `messages` and `stream:false`) to the ESP32 `/api/chat`. A background task uses the ESP32 station internet connection and private device credential to call Worker `/api/chat`. The browser polls local GET `/api/chat` while it receives 202. This works when the phone only has Lucy's hotspot connection. Local greeting/joke/companion replies handle failures.
- C3: a background FreeRTOS task calls Worker `/api/chat`; a queue transfers completion back to the main loop. Animation, buttons, Wi-Fi, web server and OTA continue running. The local brain replies immediately; the OLED and browser update when a cloud reply arrives. The browser polls `/chat-result` after a 202 from `/say`. One cloud turn runs at a time. Failure opens a 30-second retry circuit breaker.
- Worker POST `/api/chat`: `x-lucy-token`, JSON `{messages,stream:false}` -> `{model,created_at,message:{role:"assistant",content},done:true,done_reason:"stop"}`.
- Worker GET `/api/tags`: same authentication -> `{models:[{name,model}]}`.
- Worker POST `/`: legacy `{messages}` -> `{reply}`. Allowed origins are `http://192.168.4.1` and `http://edsoft.local`. Authentication is required by default. Only an explicit `ALLOW_LEGACY_BROWSER="true"` enables the old unauthenticated browser API. Leave it disabled: Origin is not authentication and can be forged outside a browser. New classic firmware uses the local proxy instead.
- Streaming requests are explicitly rejected. Request bodies are byte-limited even without Content-Length, messages are bounded, client system messages are discarded, and provider errors never expose credentials.

## Private provisioning and build

A maintainer provisions a random independent `LUCY_DEVICE_TOKEN` of 24–95 visible non-space ASCII characters in both the private firmware build and Worker secret. This is manufacturer setup, not an end-user Wi-Fi setup step. Public CI firmware has no credential and cannot provide online chat. A shared token is appropriate only for this controlled personal prototype: extracted firmware reveals it. Public distribution requires per-device enrollment, revocation and spending controls.

In a trusted private PowerShell session, set `LUCY_DEVICE_TOKEN` through your secret manager or hidden input. Never paste it into a command argument, chat, git, logs, PR, or public artifact. Then:

```powershell
node tools/write_device_token.js
node tools/gen_root_ca.js
pio run -e esp32dev
pio run -d offline -e lucy_clipbot_c3
Remove-Item Env:LUCY_DEVICE_TOKEN
Remove-Item -LiteralPath offline/src/lucy_device_config.h
```

The generated header is git-ignored and shared by both firmware variants. Delete it even if a build fails. Token-bearing binaries remain private and must not be uploaded by public CI. The OpenAI key must never enter either build. CI refuses a generated credential header.

## TLS and hardware

Both cloud paths use CA and hostname verification, bounded connection/read/handshake timeouts, and SNTP time after station connection. Chat fails locally until time is valid. The serial insecure-TLS command cannot disable verification. The root generator uses Node's public trust store: ISRG Root X1/X2, GTS Root R1/R4, DigiCert Global Root G2. Root updates do not replace live device verification.

Clipbot retains SH1106 SDA GPIO21 / SCL GPIO20 and MAX98357A BCLK GPIO4 / LRC GPIO5 / DIN GPIO3. The existing `min_spiffs.csv` OTA layout and upload handlers remain. Never automatically flash. Before OTA, compare the installed partition table and available OTA slot against the actual image size; application OTA does not change the partition table. Other C3/offline targets retain their existing board and partition selections.

## Approval-gated rollout and remaining blockers

No production Worker deployment or physical flashing is authorized by this change. After code review and explicit approval, the operator must set/rotate `OPENAI_API_KEY`, set the matching `LUCY_DEVICE_TOKEN`, retain the `LUCY_RATE_LIMITER` binding, and deploy with Wrangler. Do not enable legacy public browser access. Treat previously disclosed provider credentials as compromised and rotate them without copying them into this repository.

Prior context reported exhausted OpenAI API credits; this task does not prove the current billing balance or a successful live model response. A funded API project, permitted model, correct Worker secrets, Cloudflare deployment/binding availability, and live authentication are prerequisites. Mock tests cannot establish these. The rate limiter fails closed when missing but is regional and eventually consistent, not an accounting or hard spending cap: https://developers.cloudflare.com/workers/runtime-apis/bindings/rate-limit/ . Provider budget alerts alone must not be represented as hard caps.

Physical acceptance still requires approval and an actual board: fresh private flash -> enter Wi-Fi once -> reply with PC off -> reboot without prompts. Check phone on AP-only internet, Wi-Fi loss/recovery, DNS/NTP/TLS failure, exhausted quota, OLED/button responsiveness during timeout, simultaneous speaker playback, heap stability over repeated conversations, and OTA with the installed partition layout. USB/serial pin conflicts and actual speaker/display wiring must be checked on hardware.

Run `node tools/test_lucy_worker.mjs`, `node tools/test_device_token.mjs`, `pio run -e esp32dev`, `pio run -d offline` (all three environments), then `python tools/test_companion.py` with `LUCY_TEST_ENV` selecting the C3 build dependencies.
