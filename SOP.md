# SOP: Lucy AI ESP32 — Agent Handoff & Project Playbook

**Audience:** any AI agent (or new human) asked to build, debug, or extend this project.
**Repo:** https://github.com/vikasdocker/Lucy-AI-ESP32.git — branch **`offline-os-v1`** (commit `e0e407b` at time of writing). **All work must land on `offline-os-v1` unless the owner says otherwise.**
**Path on disk:** `C:\Users\vikas\OneDrive\Desktop\Lucy-AI-ESP32`

Read this whole document before touching anything. When something here conflicts with reality, trust reality and **ask the owner** — do not invent.

---

## 1. What this project is

**Lucy** is a companion firmware for an ESP32 that is **offline-first**: it must work with zero network access, zero cloud accounts, and must never *require* a server. On top of that, this fork added a **local, PC-hosted LLM bridge** so Lucy can hold real conversations from **anywhere with internet**, without ever putting a raw model API on the internet.

Non-goals: Lucy is not a product, not multi-device, not a cloud service. The development machine (i5-13450HX, RTX 3050 6 GB, 16 GB RAM, Windows 11) is the model host.

---

## 2. Non-negotiable rules for the agent

1. **Offline-first.** Never remove the offline brain (keyword rules + tiny intent model + Markov generator). The model bridge must be optional; if the PC is unreachable, Lucy must answer from its offline brain.
2. **Never expose port `11434`** (Ollama) to the internet. The only public entrance is the token-protected gateway on `127.0.0.1:8788`, reached through the Cloudflare tunnel. No exceptions.
3. **Secrets never enter git, never appear in output.** `gateway/config.json` holds the gateway token and is git-ignored. Never print the token, the Wi-Fi passwords, or the hotspot passphrase. If you need to pass a secret to Lucy, use `tools/provision_lucy_ai.py`, which keeps it off the command line.
4. **Never force-flash without a backup.** A full 4 MB flash backup lives at `C:\Users\vikas\OneDrive\Desktop\lucy-offline-esp32dev-backup-fullflash.bin`. The working build is at `C:\Users\vikas\OneDrive\Desktop\lucy-build\LucyOffline`. App image is flashed at `0x10000`.
5. **Don't break the PC's internet.** Lucy's hotspot path must not drop this machine's 5 GHz uplink or any running task.
6. **Don't merge or push to other branches, and don't rewrite the owner's GitHub.** Push only `offline-os-v1`, and only when the owner asks.
7. **No hallucinated hardware or features.** If the owner asks for something not present (microphone speech-to-text, on-device LLM on classic ESP32, Devanagari font, etc.), say so plainly; do not claim it exists or fake it.
8. **Verify everything you build** with the headless harness (serial + web console + `tools/test_lucy_ai.py`), never by assuming the device "will work".

---

## 3. Hardware inventory

| Item | Detail |
|---|---|
| Board | ESP32-D0WD-V3 DevKit V1, rev 3.1, MAC `e0:8c:fe:32:ef:98` |
| USB | CP210x on **COM5**, 115200 8N1 |
| Flash | 4 MB, partition scheme **huge_app** (app0 `0x10000`/`0x300000`, SPIFFS `0x310000`/`0xE0000`) |
| OLED | SSD1306 128×64, I²C (`Wire.begin(21, 22)`) |
| RTC | DS3231 on the same I²C bus; time set once via `SETTIME` serial command |
| SD | SPI (`SPI.begin(18, 19, 23, …)`), boot log at `/lucy/boot.log`; SPIFFS brain at `/lucy/brain.bin` |
| Buttons | UP / SELECT / BACK, `INPUT_PULLUP`, debounced in `pollButton()` |
| PC host | i5-13450HX, RTX 3050 6 GB VRAM, 16 GB RAM, 34 GB free disk, Windows 11 (26100), Python 3.14.3, Node v24.14.0 |
| 5 GHz network | `LordOfThePings` — **ESP32 cannot join it; 5 GHz only.** The ESP32 needs a 2.4 GHz network. |
| 2.4 GHz path | This PC runs the Windows Mobile Hotspot **"ESP32-Chat"** (2.4 GHz, shared by another project too). Lucy joins it as `192.168.137.235` at the time of writing. Created by `start-hotspot.ps1` in `C:\Users\vikas\esp32-opencode-chat\bridge`. |

---

## 4. What is built and verified (current state)

### 4.1 Firmware capabilities — all flashed on the device

| Feature | Status |
|---|---|
| Offline keyword/intent conversation brain (English/Hindi/Marathi romanized) | ✅ tested |
| On-device intent classifier (~17 KB int8, 9 classes, trainable via `tools/train_intent.py`) | ✅ integrated, paraphrases work |
| Self-learning Markov generator (order-3 char chain + order-2 backoff) | ✅ invents words, persists to SPIFFS, autosaves |
| Wi-Fi **softAP web console** `Lucy-Setup` → http://192.168.4.1 (framebuffer mirror, virtual buttons, chat box) | ✅ works |
| RTC (DS3231) clock screen + `SETTIME` | ✅ works |
| Snake / menus / boot screens | ✅ works |
| **AI bridge to PC-hosted LLM** (Ollama `qwen2.5:7b`) | ✅ works, configurable |
| **LAN mode**: `AI host 192.168.4.2 11434` (PC on `Lucy-Setup`) | ✅ wired, not yet live-tested (requires moving this PC's Wi-Fi, which would drop the agent) |
| **Public mode**: `AI url https://lucy.blueflutex.in` (token + verified TLS, via hotspot) | ✅ **verified end-to-end** — e.g. `What is the capital of Japan?` → `Tokyo…`, ~4–7 s |
| Serial commands: `AI`, `AI on/off`, `AI url`, `AI host`, `AI model`, `AI token`, `AI insecure`, `WIFI <ssid> [pass]`, `WIFI status`, `WIFI off`, `LANG en\|hi\|mr`, `SETTIME`, `brain stats|save|reset` | ✅ tested |
| NVS persistence of AI URL/token/Wi-Fi credentials | ✅ survives reboot |
| Offline fallback with 30 s circuit breaker | ✅ works — model outage → canned replies, instant |
| AP+STA coexistence (Lucy keeps `Lucy-Setup` while on the hotspot) | ✅ works |

### 4.2 PC-side services

| Piece | Location / process | Notes |
|---|---|---|
| Ollama `qwen2.5:7b` | `ollama serve`, bound `0.0.0.0:11434`, CUDA, ~5.0 GB VRAM | ~0.6 s warm, ~6 s cold load |
| **Model gateway** (auth proxy) | `gateway/server.js` on `127.0.0.1:8788` | Token auth (timing-safe compare), 60 req/min/IP, 2 concurrent, body/message clamps, forces `qwen2.5:7b`, strips streaming |
| **Public tunnel** | `cloudflared`, tunnel `esp32-chat` → `lucy.blueflutex.in` → `127.0.0.1:8788` | Also `chat.blueflutex.in` → `127.0.0.1:8787` (owner's other app) |
| **Persistence** | Scheduled tasks **`Lucy Ollama`**, **`Lucy Gateway`**, **`Lucy Tunnel`** at logon | auto-restart 3×, run as limited; uninstall with `tools\install_lucy_tasks.ps1 -Remove` |
| Firewall | `Lucy AI (Ollama)` inbound TCP 11434, **private addresses only** | Port 11434 never forwarded |
| Logs | `gateway\gateway.log`, `gateway\gateway.err.log` (UTF-8, no tokens) | rotating handled by you, not auto |

### 4.3 Public API contract (`lucy.blueflutex.in`)

| Endpoint | Method | Auth | Notes |
|---|---|---|---|
| `/health` | GET | public | `{ok, service, uptime, model, busy}` — deliberately minimal |
| `/api/tags` | GET | `x-lucy-token` or Bearer | what Lucy pings |
| `/api/chat` | POST | token | Ollama chat shape; model fixed server-side; returns `{model, message:{role, content}, done:true}` |
| `/chat` | POST | token | `{reply, model}` convenience alias |

Request validation and model clamping are intentional — do not loosen them without a security review.

### 4.4 Verified end-to-end on the device

- Serial: clean boot, no panic, `AI bridge: on (…, token 48 chars)` , `ping: reachable`, `Tokyo…` from the model over the internet.
- Conversation memory: 3 turns of rolling context.
- LAN/local fallback over Wi-Fi drop: recovers automatically within ~30 s, offline brain answers in between.

---

## 5. How to build, flash, and test

### 5.1 Build & flash (headless, do not use the Arduino GUI)

```powershell
$src = "C:\Users\vikas\OneDrive\Desktop\Lucy-AI-ESP32\offline\src"
$dst = "C:\Users\vikas\OneDrive\Desktop\lucy-build\LucyOffline"
Copy-Item "$src\main.cpp" "$dst\LucyOffline.ino" -Force
Copy-Item "$src\ai_client.cpp","$src\ai_api.h","$src\markov.cpp","$src\markov_api.h","$src\intent_model.cpp","$src\intent_api.h","$src\intent_model.h","$src\tls_root_ca.h" $dst -Force
& "C:\Program Files\Arduino CLI\arduino-cli.exe" compile --fqbn esp32:esp32:esp32 `
    --board-options PartitionScheme=huge_app --build-path "$dst\build" $dst
python -m esptool --port COM5 write-flash --flash-size detect -z 0x10000 `
    "$dst\build\LucyOffline.ino.bin"
```
Latest build: **1,221,531 B flash (38 % of 3 MB), 206,512 B free RAM**.

### 5.2 Serial smoke test (PySerial one-liner used throughout)

Open COM5 at 115200, wait ≥5 s after opening, `reset_input_buffer()`, then newline-command. Long commands occasionally take a few seconds; don't send at >200 ms intervals.

Automated:
```powershell
python tools\test_lucy_ai.py COM5     # AI status + 3 chat questions, prints timing
```

### 5.3 Web console

Join the open `Lucy-Setup` Wi-Fi network → http://192.168.4.1 — framebuffer mirror, virtual UP/SELECT/BACK, chat box (all goes through the same `offlineReply()` path).

### 5.4 Register/restore the persistence tasks

```powershell
powershell -ExecutionPolicy Bypass -File tools\install_lucy_tasks.ps1         # register + start
powershell -ExecutionPolicy Bypass -File tools\install_lucy_tasks.ps1 -Remove # unregister
powershell -ExecutionPolicy Bypass -File tools\start_lucy_public.ps1          # one-off bring-up
```

---

## 6. Architecture

### LAN (offline fallback)
```
Lucy (AP-only, 192.168.4.1) ──joins──► PC (192.168.4.x) ──HTTP──► Ollama 127.0.0.1:11434
```
### Public (primary path today)
```
Lucy (any 2.4 GHz Wi-Fi with internet, e.g. "ESP32-Chat" hotspot)
   └─► TLS, verified (ISRG X1/X2 root CA pinned in tls_root_ca.h)
        └─► Cloudflare edge (lucy.blueflutex.in)
             └─► cloudflared tunnel (esp32-chat)
                  └─► 127.0.0.1:8788  gateway/server.js (token auth, rate limit)
                       └─► 127.0.0.1:11434  Ollama qwen2.5:7b (GPU)
```
LLM **never** lives on the ESP32. A full ESP32-S3 + PSRAM would be required; that is out of scope.

---

## 7. Security model — read before changing anything

| Surface | Posture |
|---|---|
| `lucy.blueflutex.in/health` | Public, reveals only uptime/model/busy count |
| `lucy.blueflutex.in/api/*` | Token required (`x-lucy-token`, constant-time compare) |
| Gateway bind | `127.0.0.1` only — tunnel is the sole public route |
| Ollama bind | `0.0.0.0:11434` (needed for LAN mode) but firewall allows private ranges only |
| Firmware TLS | **verified**, not just encrypted, via pinned ISRG X1/X2 roots (regenerate with `tools/gen_root_ca.js`) |
| Gateway token | 48 hex chars, stored in `gateway/config.json` (git-ignored), also in NVS on the device |
| Rate/concurrency | 60 req/min per IP, 2 concurrent model calls, body ≤64 KB, messages ≤20, 4000 chars each, `num_predict` ≤200 |
| Known residual risks | Token embedded in firmware is extractable from flash; `Lucy-Setup` AP is open; the *other* project's public bridge on `chat.blueflutex.in` still uses the repo's **default placeholder token** (flagged to owner, unfixed) |

**Token rotation:** `node gateway/setup.js --rotate`, then `python tools/provision_lucy_ai.py COM5`. The old token dies immediately.

---

## 8. Operational runbook

### 8.1 After a PC reboot
1. Windows logs in → **Lucy Ollama / Lucy Gateway / Lucy Tunnel** tasks start automatically (verify `Get-ScheduledTask "Lucy *"` → `Running`).
2. **The 2.4 GHz Mobile Hotspot `ESP32-Chat` is NOT a scheduled task** — start it manually with `C:\Users\vikas\esp32-opencode-chat\bridge\start-hotspot.ps1` (unsigned internal tool, fine). Without it, Lucy falls back to offline replies.
3. Sanity check: `Invoke-RestMethod https://lucy.blueflutex.in/health` → expect `model: qwen2.5:7b`.

### 8.2 Lucy was rebooted / fresh flash
Settings persist in NVS (endpoint, token, Wi-Fi, language, sound, timers). On boot Lucy prints `AI bridge:` and `WiFi:` lines; watch for `ping: reachable` after a minute of settling.

### 8.3 Re-provision Lucy (new token / new network)
```powershell
python tools\provision_lucy_ai.py COM5          # interactive, prompts for SSID/pass locally
```
Never type the password into chat.

### 8.4 Verify chat on Lucy right now

Open serial terminal (e.g. `python -m serial.tools.miniterm COM5 115200`), wait for boot, type `AI` (expect `ping: reachable`), then `hello`, then `What is the capital of Japan?` — answers are model-generated.

### 8.5 Break the system on purpose (fallback demo)
`AI off` → instant canned replies. `AI on` → model returns. Or sleep the PC → Lucy falls back automatically, then recovers.

### 8.6 Recover from a bad flash
```powershell
python -m esptool --port COM5 write-flash --flash-size detect -z 0x10000 `
    "C:\Users\vikas\OneDrive\Desktop\lucy-offline-esp32dev-backup-fullflash.bin"
```
That is the 4 MB full backup of the device **as it was before any of this work**.

### 8.7 Something broke — where to look
| Symptom | Check first |
|---|---|
| `AI` prints `unreachable` | tasks running? hotspot running? PC asleep? `Get-ScheduledTask "Lucy *"` |
| Instant canned replies | offline fallback engaged → same causes |
| `HTTP 401` in `AI` | token mismatch → re-run provisioning with current `gateway/config.json` |
| Gateway 502 | `Lucy Gateway` task down or Ollama not answering → `tools\start_lucy_public.ps1` |
| `gateway.err.log` shows `EADDRINUSE` | another process holds 8788 → kill it, restart task |
| Lucy not joining hotspot | `WIFI status`; re-run `start-hotspot.ps1`; power cycle Lucy |

---

## 9. Known issues and quirks (real, observed)

1. **`LordOfThePings` is 5 GHz-only** — ESP32 cannot join it. The ESP32 uses the 2.4 GHz `ESP32-Chat` hotspot (shared with the owner's other project).
2. **ESP32↔hotspot link occasionally drops** (observed a state-6 disconnect). Firmware auto-reconnects and prints `WiFi: connected as …`; offline brain answers during drops.
3. **`tools/run_gateway.ps1` uses `Start-Process … -Wait -PassThru`** because `*>>` corrupts UTF-8 logs into UTF-16 garbage. Don't "simplify" it back.
4. **Ollama desktop app and the `Lucy Ollama` task fight over port 11434** — the wrapper skips starting if the port answers, so either wins. `OLLAMA_HOST=0.0.0.0:11434` is set in the user environment for this reason.
5. **Task `Lucy Gateway` showed stale `Ready` while PID-fed gateway ran** — occurred because the wrapper and node lost association; `RestartCount=3` does not cover that class of failure for the wrapper. If in doubt, restart the task and verify `Running`.
6. **Markov output is word salad by design.** It invents text from statistics, not semantics. It is not meant to answer questions.
7. **Model sees only the last 3 turns** of context; keep prompts short.
8. **System prompt restricts output to ASCII Latin** (fixed an accidental Chinese-glyph output from the model). Do not widen it without re-testing on the OLED/serial.
9. **Cold model load ≈6 s**, warm ≈0.6 s, public round trip ≈1.7–7 s depending on route. This is expected.
10. **`offline/.pio/`** and `lucy-build/` contain build junk that is git-ignored on purpose.
11. **`Lucy Gateway` is not auto-started by the hotspot** — the hotspot is a Windows setting, not a task; see §8.1. This is the single most likely cause of "Lucy fell back to offline" after a reboot.

---

## 10. Where the work came from & what the owner wants

### 10.1 Proven goal (in the owner's own words, across the build)
- "Can we run a lite model on this PC and use it in Lucy so Lucy can talk perfectly? Any small model?"
- "Can we expose this model to internet so Lucy can access it from anywhere?"
- "Ok, push to github."

### 10.2 Direction for the next agent (owner's intent, not yet fully implemented)
1. **Lucy should feel like it can hold a real conversation** — it now does, via the PC-hosted `qwen2.5:7b`; keep optimizing latency/quality within the 6 GB VRAM budget.
2. **Lucy must remain a self-contained offline companion** — do not make the LLM mandatory; do not strip the offline brain; do not require cloud SaaS.
3. **Keep the device reachable from anywhere** — the public gateway is the intended route; a phone hotspot or any 2.4 GHz Wi-Fi with internet is fine.
4. **Owner is non-expert on infra** — automate, document, and make failures self-explanatory. Ask before destructive changes.

### 10.3 Open ideas that belong to the owner (do not assume any of these)
- Voice (STT/TTS) needs an ESP32-S3 with PSRAM — not on this board.
- On-device LLM — not on classic ESP32.
- Migrating to ESP32-S3 — not discussed beyond a note; ask before buying.
- Auto-registering the hotspot as a logon task — obvious gap, not yet done, needs owner approval.
- Rotating the other project's default bridge token — flagged twice, unanswered.

---

## 11. Open questions to bring to the owner (before building)

1. Should the `ESP32-Chat` hotspot be registered as a logon task so Lucy survives reboots fully automatically? (Currently Step 1 in §8.1 is manual.)
2. Should the other project's public bridge token be rotated? (Out of this repo's scope, but the owner may want it.)
3. Is it acceptable that chat round trips go via Cloudflare to this machine, or does the owner want a LAN-only fallback for reliability?
4. Should Lucy remember conversations across reboots (SPIFFS/SD), or is 3-turn RAM context enough?

---

## 12. Quick file map

```
offline/                         firmware (Arduino-IDE .ino copy of main.cpp lives in lucy-build\)
  src/main.cpp                   OS, command routing, reply order, web console, buttons, menus
  src/ai_client.cpp/.h           HTTPS/HTTP client; TLS, token, circuit breaker, 3-turn history
  src/tls_root_ca.h              ISRG X1/X2 pinned roots for TLS verification
  src/markov.cpp/.h              self-learning order-3 word generator
  src/intent_model.cpp/.h/.h     int8 intent classifier (train with tools/train_intent.py)
gateway/                         PC-side public proxy (keeps Ollama private)
  server.js                      token auth, rate limit, /api/chat /api/tags /health
  setup.js                       generate gateway/config.json (random token)
  config.json                    LOCAL ONLY, git-ignored
  config.example.json            placeholder template
tools/
  start_lucy_public.ps1          one-off bring-up: ollama+gateway+tunnel+firewall
  install_lucy_tasks.ps1         register logon tasks (-Remove to undo)
  run_ollama.ps1 / run_gateway.ps1   task wrappers (skip if port busy)
  provision_lucy_ai.py           push Wi-Fi + token to Lucy over USB, verify join
  test_lucy_ai.py                smoke test (AI status + 3 questions)
  gen_root_ca.js                 regenerate tls_root_ca.h from the OS trust store
  train_intent.py                retrain the tiny intent model
  start_lucy_ai.ps1              LAN-only mode helper
README.md, offline/README.md     end-user documentation
.gitignore                       gateway/config.json, logs, .pio, build output
```

---

## 13. Commit hygiene for this repo

- Branch: `offline-os-v1` only.
- `git add -A` is safe **only because** `.gitignore` excludes `gateway/config.json`, logs and build junk — keep it that way.
- Verified the repo contains **no token, no password, no binary**. Keep it that way. Before any push, run `git add -n -A` (dry-run) and eyeball the file list.
- Never commit full-flash dumps; never commit `C:\Users\vikas\OneDrive\Desktop\lucy-offline-esp32dev-backup-fullflash.bin` to this repo.
- CRLF warnings are cosmetic; don't chase them.

---

## 14. One-line mental model for the next agent

> Lucy is an offline pet with a GPU brain on standby.
> If the PC is reachable, Lucy borrows it; if not, Lucy never noticed anything was missing.
> The tunnel, the gateway and the token exist so that someone you trust can call the brain; they do not make the brain live on the internet.

*Document last updated 2026-11-14 against commit `e0e407b` on `offline-os-v1`. If any claim here no longer matches the repo, the repo wins — fix the doc.*
