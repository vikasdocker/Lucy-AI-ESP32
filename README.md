# Lucy AI – ESP32 Mobile Chatbot

Firmware target: classic ESP32 DevKit / ESP32-WROOM-32 (4 MB flash assumed).

## What it does

- Creates a WPA2 Wi-Fi access point: `Lucy_AI`
- Gateway: `192.168.4.1`
- Serves a mobile chatbot UI at `http://192.168.4.1/`
- Provides an OTA firmware update page at `http://192.168.4.1/update`
- Calls a secure Cloudflare Worker proxy; the OpenAI key is never stored on the ESP32.

## Build without a laptop

GitHub Actions builds the firmware after every push to the default branch. On GitHub mobile/web:
1. Open this repository → Actions.
2. Open **Build Lucy AI ESP32 firmware** and wait for a green successful run.
3. Open the run → Artifacts → download `lucy-ai-esp32-firmware`.
4. Extract the ZIP in the iPhone Files app. It contains `lucy-ai-esp32dev.bin`.

## Important hardware / OTA warning

The board photo appears to show an ESP32-WROOM-32 DevKit, but the exact flash size and the currently installed firmware's partition table cannot be read from the photo. The project uses an OTA-capable 4 MB partition layout. Do NOT upload this firmware over the existing OTA page until you have confirmed the existing firmware supports compatible OTA application partitions and the board has 4 MB flash. A wrong partition layout can make OTA fail or require USB serial recovery. If the existing firmware is not known, use a USB-serial programmer (which normally requires a computer) for the first flash.

## Install

1. Power the board.
2. Connect phone Wi-Fi to `Lucy_AI`; password: `LucyAI123`.
3. Open `http://192.168.4.1/`.
4. Open **Firmware update** at `http://192.168.4.1/update` only after confirming OTA compatibility.
5. Select `lucy-ai-esp32dev.bin` and upload. Keep power connected.

Change the default AP password in `src/main.cpp` before distributing or using the device in an untrusted environment.

## Configure the AI backend

1. Create a Cloudflare Worker using `worker/worker.js` as its code.
2. Add a Worker secret named `OPENAI_API_KEY` with your OpenAI API key. Never put this key in GitHub source, firmware, or the browser.
3. Deploy the Worker and copy its HTTPS `workers.dev` URL.
4. Connect the phone to Lucy_AI, open `http://192.168.4.1/`, tap **AI settings**, paste the Worker URL, and save.
5. Send a message. The phone needs mobile data while connected to the ESP32 access point. On some phones, the Wi-Fi network without internet may prevent internet requests; if so, use AP+STA mode with an internet-connected router or a separate network bridge.

The Worker only accepts browser requests whose Origin is `http://192.168.4.1`. CORS is not authentication; for public deployment add an authenticated device/session token, rate limiting, abuse controls, and spend limits. This starter Worker has basic payload validation but no per-user authentication.

OpenAI API billing is separate from ChatGPT subscriptions.
